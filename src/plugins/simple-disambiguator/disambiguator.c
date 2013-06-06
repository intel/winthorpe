#include <string.h>
#include <errno.h>

#include <murphy/common/macros.h>
#include <murphy/common/mm.h>
#include <murphy/common/log.h>
#include <murphy/common/debug.h>

#include "src/daemon/plugin.h"
#include "src/daemon/client.h"
#include "src/daemon/recognizer.h"

#define DISAMB_NAME     "simple-disambiguator"
#define DISAMB_INFO     "A test disambiguator."
#define DISAMB_AUTHORS  "Krisztian Litkey <kli@iki.fi>"
#define DISAMB_VERSION  "0.0.1"

#define MAX_DICT  256                    /* max. dictionary name length */
#define MAX_DEPTH 256                    /* max. token tree depth */


typedef enum {
    NODE_TYPE_UNKNOWN = 0,
    NODE_TYPE_NONE    = 0,
    NODE_TYPE_TOKEN,                     /* a command token node */
    NODE_TYPE_DICTIONARY,                /* dictionary operation node */
    NODE_TYPE_CLIENT,                    /* a client/command node */
} node_type_t;

typedef union {
    char *token;                         /* for NODE_TYPE_TOKEN */
    struct {                             /* for NODE_TYPE_CLIENT */
        srs_client_t *client;            /*     client */
        int           index;             /*     command index */
    } client;
    struct {                             /* for NODE_TYPE_DICTIONARY */
        srs_dict_op_t  op;               /*     operation */
        char          *dict;             /*     dictionary to switch or push */
    } dict;
} node_data_t;

typedef struct {
    mrp_list_hook_t hook;                /* hook to parent node */
    node_type_t     type;                /* node type */
    node_data_t     data;                /* type-specific node data */
    mrp_list_hook_t children;            /* child nodes */
} node_t;

typedef enum {
    MATCH_NONE = 0,                      /* no match */
    MATCH_EXACT,                         /* exact token match */
    MATCH_PREFIX,                        /* token prefix match */
} match_t;

typedef struct {
    mrp_list_hook_t       hook;          /* to list of candidates */
    srs_srec_candidate_t *src;           /* candidate from the backend */
    int                   tknidx;        /* token index */
    int                   nchange;       /* number changes to get a match */
    node_t               *nodes;         /* path through the tree */
    int                   nnode;         /* path length so far */
} candidate_t;

typedef struct {
    srs_plugin_t    *plugin;             /* that's us */
    node_t          *root;               /* command token tree */
    mrp_list_hook_t  candidates;         /* recognition candidates */
    candidate_t     *active;             /* candidate being processed */
} disamb_t;


static void disamb_del_client(srs_client_t *client, void *api_data);

static srs_dict_op_t parse_dictionary(const char *tkn, char *dict, size_t size)
{
    const char *s;
    char       *d;
    size_t      l;

    if (!strncmp(tkn, SRS_DICTCMD_PUSH  , l=sizeof(SRS_DICTCMD_PUSH) - 1) ||
        !strncmp(tkn, SRS_DICTCMD_SWITCH, l=sizeof(SRS_DICTCMD_PUSH) - 1)) {
        if (tkn[l] != '(')
            return SRS_DICT_OP_UNKNOWN;

        s = tkn + l + 1;
        d = dict;

        while (size-- > 1 && *s != ')' && *s) {
            *d++ = *s++;
        }

        if (!size) {
            mrp_log_error("Invalid dictionary, name too long: '%s'.", tkn);
            return SRS_DICT_OP_UNKNOWN;
        }

        if (*s != ')') {
            mrp_log_error("Invalid dictionary command: '%s'.", tkn);
            return SRS_DICT_OP_UNKNOWN;
        }

        *d = '\0';

        return tkn[2] == 's' ? SRS_DICT_OP_SWITCH : SRS_DICT_OP_PUSH;
    }

    if (!strcmp(tkn, SRS_DICTCMD_POP)) {
        dict[0] = '\0';
        return SRS_DICT_OP_POP;
    }

    return SRS_DICT_OP_UNKNOWN;
}


static node_t *get_token_node(node_t *prnt, const char *token, int insert)
{
    mrp_list_hook_t *p, *n;
    node_t          *node, *any;
    int              cnt;

    cnt = 0;
    any = NULL;
    mrp_list_foreach(&prnt->children, p, n) {
        node = mrp_list_entry(p, typeof(*node), hook);

        if (node->type != NODE_TYPE_TOKEN) {
            errno = EINVAL;
            return NULL;
        }

        if (!strcmp(node->data.token, token)) {
            mrp_debug("found token node %s", token);
            return node;
        }

        if (!strcmp(node->data.token, SRS_TOKEN_WILDCARD))
            any = node;

        cnt++;
    }

    /*
     * wildcard node matches all tokens but only for pure lookups
     */

    if (!insert) {
        if (any != NULL)
            return any;
        else {
            errno = ENOENT;
            return NULL;
        }
    }

    /*
     * a wildcard node must be the only child of its parent
     */

    if (any != NULL || (cnt > 0 && !strcmp(token, SRS_TOKEN_WILDCARD))) {
        mrp_log_error("Wildcard/non-wildcard token conflict.");
        errno = EILSEQ;
        return NULL;
    }

    node = mrp_allocz(sizeof(*node));

    if (node == NULL)
        return NULL;

    mrp_list_init(&node->hook);
    mrp_list_init(&node->children);

    node->type = NODE_TYPE_TOKEN;
    node->data.token = mrp_strdup(token);

    if (node->data.token == NULL) {
        mrp_free(node);
        return NULL;
    }

    mrp_list_append(&prnt->children, &node->hook);

    mrp_debug("added token node %s", token);

    return node;
}


static node_t *get_dictionary_node(node_t *prnt, const char *token, int insert)
{
    srs_dict_op_t    op;
    char             dict[MAX_DICT];
    mrp_list_hook_t *p, *n;
    node_t          *node;

    if (token != NULL) {
        op = parse_dictionary(token, dict, sizeof(dict));

        if (op == SRS_DICT_OP_UNKNOWN) {
            errno = EINVAL;
            return NULL;
        }
    }
    else {
        if (insert) {
            errno = EINVAL;
            return NULL;
        }
    }

    if (prnt->type != NODE_TYPE_TOKEN) {
        errno = ECHILD;
        return NULL;
    }

    mrp_list_foreach(&prnt->children, p, n) {
        node = mrp_list_entry(p, typeof(*node), hook);

        if (!insert && token == NULL)
            if (node->type == NODE_TYPE_DICTIONARY)
                return node;

        if (node->type != NODE_TYPE_DICTIONARY || node->data.dict.op != op ||
            strcmp(dict, node->data.dict.dict) != 0) {
            errno = EILSEQ;
            return NULL;
        }
    }

    if (!insert) {
        errno = ENOENT;
        return NULL;
    }

    node = mrp_allocz(sizeof(*node));

    if (node == NULL)
        return NULL;

    mrp_list_init(&node->hook);
    mrp_list_init(&node->children);
    node->type = NODE_TYPE_DICTIONARY;
    node->data.dict.op = op;
    node->data.dict.dict = mrp_strdup(dict);

    if (node->data.dict.dict == NULL) {
        mrp_free(node);
        return NULL;
    }

    mrp_list_append(&prnt->children, &node->hook);

    mrp_debug("added dictionary node %s", token);

    return node;
}


static node_t *get_client_node(node_t *prnt, srs_client_t *client, int index)
{
    mrp_list_hook_t *p, *n;
    node_t          *node;

    mrp_list_foreach(&prnt->children, p, n) {
        node = mrp_list_entry(p, typeof(*node), hook);

        if (node->type != NODE_TYPE_CLIENT)
            return NULL;

        if (node->data.client.client == client &&
            node->data.client.index  == index)
            return node;
    }

    return NULL;
}


static int register_command(disamb_t *dis, srs_client_t *client, int index)
{
    srs_command_t   *cmd = client->commands + index;
    char            *tkn;
    int              i;
    node_t          *prnt, *node;

    prnt = dis->root;
    for (i = 0; i < cmd->ntoken; i++) {
        tkn = cmd->tokens[i];

        if (tkn[0] != '_') {
            node = get_token_node(prnt, tkn, TRUE);

            if (node == NULL) {
                if (errno == EINVAL)
                    mrp_log_error("Command #%d of client %s would introduce "
                                  "ambiguity.", index, client->id);

                return -1;
            }

            prnt = node;
        }
        else {
            node = get_dictionary_node(prnt, tkn, TRUE);

            if (node == NULL) {
                switch (errno) {
                case ECHILD:
                    mrp_log_error("Command #%d of client %s has dictionary "
                                  "operation following a nonregular token.",
                                  index, client->id);
                    break;
                case EILSEQ:
                    mrp_log_error("Command #%d of client %s would introduce "
                                  "ambiguous dictionary operations.",
                                  index, client->id);
                    break;
                }
                return -1;
            }

            prnt = node;
        }
    }

    node = mrp_allocz(sizeof(*node));

    if (node == NULL)
        return -1;

    mrp_list_init(&node->hook);
    mrp_list_init(&node->children);
    node->type = NODE_TYPE_CLIENT;
    node->data.client.client = client;
    node->data.client.index  = index;

    mrp_list_append(&prnt->children, &node->hook);

    mrp_debug("added client command %s/#%d", client->id, index);

    return 0;
}


static void unregister_command(disamb_t *dis, srs_client_t *client, int index)
{
    srs_command_t *cmd = client->commands + index;
    const char    *tkn;
    node_t        *stack[MAX_DEPTH], *prnt, *node;
    int            i;

    prnt = dis->root;
    for (i = 0; i < cmd->ntoken && i < (int)MRP_ARRAY_SIZE(stack); i++) {
        tkn = cmd->tokens[i];

        if (tkn[0] != '_')
            node = get_token_node(prnt, tkn, FALSE);
        else
            node = get_dictionary_node(prnt, tkn, FALSE);

        if (node == NULL)
            return;

        stack[i] = prnt = node;
    }

    if (i == cmd->ntoken) {
        node = get_client_node(prnt, client, index);

        if (node != NULL) {
            mrp_list_delete(&node->hook);
            mrp_free(node);

            mrp_debug("deleted client command node %s/#%d",
                      node->data.client.client->id, node->data.client.index);
        }
    }

    i--;

    while (i >= 0) {
        node = stack[i];

        if (mrp_list_empty(&node->children)) {
            mrp_list_delete(&node->hook);

            switch (node->type) {
            case NODE_TYPE_TOKEN:
                mrp_debug("deleting token node '%s'", node->data.token);
                mrp_free(node->data.token);
                break;

            case NODE_TYPE_DICTIONARY:
                mrp_debug("deleting dictionary node 0x%x(%s)",
                          node->data.dict.op, node->data.dict.dict);
                mrp_free(node->data.dict.dict);
                break;

            case NODE_TYPE_CLIENT: /* really should not happen */
                mrp_log_error("Unexpected client node %s/#%d !",
                              node->data.client.client->id,
                              node->data.client.index);
                break;
            default:
                break;
            }

            mrp_free(node);
        }

        i--;
    }
}


static void free_all_nodes(node_t *root)
{
    mrp_list_hook_t  nodes, *np, *nn, *cp, *cn;
    node_t          *node;
    int              change;

    mrp_list_init(&nodes);

    mrp_list_delete(&root->hook);
    mrp_list_append(&nodes, &root->hook);
    change = TRUE;

    /* collapse all nodes in the tree to a single list of nodes */
    while (change) {
        change = FALSE;

        mrp_list_foreach(&nodes, np, nn) {
            node = mrp_list_entry(np, typeof(*node), hook);

            mrp_list_foreach(&node->children, cp, cn) {
                mrp_list_delete(cp);
                mrp_list_append(&nodes, cp);
                change = TRUE;
            }
        }
    }

    /* now free all nodes */
    mrp_list_foreach(&nodes, np, nn) {
        node = mrp_list_entry(np, typeof(*node), hook);

        mrp_list_delete(&node->hook);

        switch (node->type) {
        case NODE_TYPE_TOKEN:
            mrp_debug("freeing token node '%s'", node->data.token);
            mrp_free(node->data.token);
            break;

        case NODE_TYPE_DICTIONARY:
            mrp_debug("freeing dictionary node '%s'", node->data.dict.dict);
            break;

        default:
            break;
        }

        mrp_free(node);
    }
}


static int disamb_add_client(srs_client_t *client, void *api_data)
{
    disamb_t *dis = (disamb_t *)api_data;
    int       i;

    for (i = 0; i < client->ncommand; i++) {
        mrp_debug("registering client command %s/#%d", client->id, i);
        if (register_command(dis, client, i) != 0) {
            disamb_del_client(client, api_data);
            return -1;
        }
    }

    return 0;
}


static void disamb_del_client(srs_client_t *client, void *api_data)
{
    disamb_t *dis = (disamb_t *)api_data;
    int       i;

    for (i = 0; i < client->ncommand; i++) {
        mrp_debug("unregistering client command %s/#d", client->id, i);
        unregister_command(dis, client, i);
    }
}


static int disambiguate(srs_srec_utterance_t *utt, srs_srec_result_t **result,
                        void *api_data)
{
    disamb_t             *dis = (disamb_t *)api_data;
    srs_srec_candidate_t *src;
    srs_srec_result_t    *res;
    srs_srec_match_t     *m;
    const char           *tkn;
    mrp_list_hook_t      *p, *n;
    node_t               *node, *child, *prnt;
    int                   i, j, end, match;
    uint32_t              offs;

    mrp_debug("should disambiguate utterance %p", utt);

    /* XXX handling multiple candidates currently not implemented */
    if (utt->ncand > 1) {
        mrp_log_warning("Handling multiple candidates not implemented.");
        mrp_log_warning("Ignoring all but first candidate.");
    }

    src = utt->cands[0];
    res = *result;

    if (res != NULL) {
        if (res->type == SRS_SREC_RESULT_DICT)
            node = res->result.dict.state;
        else
            node = dis->root;
    }
    else {
        mrp_log_error("Expected result buffer not found.");
        return -1;
    }

    for (i = 0, match = TRUE; i < (int)src->ntoken && match; i++) {
        tkn = src->tokens[i].token;

        prnt = node;
        node = get_token_node(prnt, tkn, FALSE);

        if (node == NULL)
            node = get_token_node(prnt, SRS_TOKEN_WILDCARD, FALSE);

        if (node == NULL) {
            node = get_dictionary_node(prnt, NULL, FALSE);

            if (node != NULL) {
                mrp_debug("found dictionary node %s", node->data.dict.dict);

                res->type = SRS_SREC_RESULT_DICT;
                res->result.dict.op     = node->data.dict.op;
                res->result.dict.dict   = node->data.dict.dict;
                res->result.dict.state  = node;
                res->result.dict.rescan = (int)src->tokens[i].start;

                *result = res;

                return 0;
            }
            else
                match = FALSE;
        }
        else {
            mrp_debug("found matching node for %s", tkn);

            if (strcmp(node->data.token, SRS_TOKEN_WILDCARD))
                end = i;
            else
                end = (int)src->ntoken - 1;

            for (j = i; j <= end; j++) {
                if (mrp_reallocz(res->tokens, res->ntoken, res->ntoken + 1)) {
                    tkn = src->tokens[j].token;
                    res->tokens[res->ntoken] = mrp_strdup(tkn);

                    if (res->tokens[res->ntoken] == NULL)
                        return -1;
                    else
                        res->ntoken++;
                }

                offs = res->sampleoffs;
                if (mrp_reallocz(res->start, res->ntoken, res->ntoken + 1))
                    res->start[res->ntoken-1] = offs + src->tokens[j].start;
                else
                    return -1;

                if (mrp_reallocz(res->end, res->ntoken, res->ntoken + 1))
                    res->end[res->ntoken-1] = offs + src->tokens[j].end;
                else
                    return -1;
            }

            i = end;
        }
    }

    if (match && i == (int)src->ntoken) {
        res->type = SRS_SREC_RESULT_MATCH;
        mrp_list_init(&res->result.matches);

    search_clients:
        mrp_list_foreach(&node->children, p, n) {
            child = mrp_list_entry(p, typeof(*child), hook);

            /*
             * Notes:
             *   This is a kludge to get wildcard match also 0 tokens.
             *
             *   If we don't find an immediate client node, see if we can
             *   get to a client node by traversing the tree further but
             *   without consume any tokens (which we do not have). This
             *   can be accomplished in two ways:
             *     1) via an immediate wildcard node followed by a client
             *     2) via a dictionary and a wildcard node followed by a
             *        client
             */
            if (child->type != NODE_TYPE_CLIENT) {
                prnt = NULL;

                if (child->type == NODE_TYPE_TOKEN) {
                    if (!strcmp(child->data.token, SRS_TOKEN_WILDCARD))
                        prnt = child;
                }
                else if (child->type == NODE_TYPE_DICTIONARY) {
#if 1
                    mrp_debug("found dictionary node %s",
                              child->data.dict.dict);

                    res->type = SRS_SREC_RESULT_DICT;
                    res->result.dict.op     = child->data.dict.op;
                    res->result.dict.dict   = child->data.dict.dict;
                    res->result.dict.state  = child;
                    res->result.dict.rescan = (int)src->tokens[i-1].end;

                    *result = res;

                    return 0;
#else
                    prnt = get_token_node(child, SRS_TOKEN_WILDCARD, FALSE);
#endif
                }

                if (prnt != NULL) {
                    node = prnt;
                    goto search_clients;
                }

                mrp_log_error("Unexpected non-client node type 0x%x.",
                              node->type);
                continue;
            }

            m = mrp_allocz(sizeof(*m));

            if (m == NULL)
                return -1;

            mrp_list_init(&m->hook);
            m->client = child->data.client.client;
            m->index  = child->data.client.index;
            m->score  = src->score;
            m->fuzz   = 0;
            m->tokens = NULL;

            mrp_list_append(&res->result.matches, &m->hook);

            mrp_log_info("Found matching command %s/#%d.",
                         m->client->id, m->index);

            for (j = 0; j < res->ntoken; j++) {
                mrp_log_info("    actual token #%d: '%s'", j,
                             res->tokens[j]);
            }
        }

        *result = res;
    }

    return 0;
}


static int create_disamb(srs_plugin_t *plugin)
{
    srs_disamb_api_t api = {
    add_client:   disamb_add_client,
    del_client:   disamb_del_client,
    disambiguate: disambiguate,
    };

    srs_context_t *srs = plugin->srs;
    disamb_t      *dis;

    mrp_debug("creating disambiguator");

    dis = mrp_allocz(sizeof(*dis));

    if (dis != NULL) {
        mrp_list_init(&dis->candidates);
        dis->plugin = plugin;
        dis->root   = mrp_allocz(sizeof(*dis->root));

        if (dis->root != NULL) {
            mrp_list_init(&dis->root->hook);
            mrp_list_init(&dis->root->children);
            dis->root->type = NODE_TYPE_TOKEN;

            if (srs_register_disambiguator(srs, DISAMB_NAME, &api, dis) == 0) {
                plugin->plugin_data = dis;
                return TRUE;
            }
            else
                mrp_free(dis->root);
        }
        else
            mrp_free(dis);
    }

    return FALSE;
}


static int config_disamb(srs_plugin_t *plugin, srs_cfg_t *settings)
{
    srs_cfg_t *cfg;
    int        n, i;

    MRP_UNUSED(plugin);

    mrp_debug("configuring disambiguator");

    n = srs_collect_config(settings, "disambiguator.", &cfg);

    mrp_debug("found %d configuration keys%s", n, n ? ":" : "");
    for (i = 0; i < n; i++)
        mrp_debug("    %s = %s", cfg[i].key, cfg[i].value);

    srs_free_config(cfg);

    return TRUE;
}


static int start_disamb(srs_plugin_t *plugin)
{
    MRP_UNUSED(plugin);

    mrp_debug("starting disambiguator plugin");

    return TRUE;
}


static void stop_disamb(srs_plugin_t *plugin)
{
    MRP_UNUSED(plugin);

    mrp_debug("stopping disambiguator plugin");

    return;
}


static void destroy_disamb(srs_plugin_t *plugin)
{
    srs_context_t *srs = plugin->srs;
    disamb_t      *dis = (disamb_t *)plugin->plugin_data;

    mrp_debug("destroying disambiguator plugin");

    if (dis != NULL) {
        srs_unregister_disambiguator(srs, DISAMB_NAME);
        free_all_nodes(dis->root);
        mrp_free(dis);
    }
}


SRS_DECLARE_PLUGIN(DISAMB_NAME, DISAMB_INFO, DISAMB_AUTHORS, DISAMB_VERSION,
                   create_disamb, config_disamb, start_disamb, stop_disamb,
                   destroy_disamb)
