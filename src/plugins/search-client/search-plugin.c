/*
 * Copyright (c) 2012, 2013, Intel Corporation
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *   * Redistributions of source code must retain the above copyright notice,
 *     this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *   * Neither the name of Intel Corporation nor the names of its contributors
 *     may be used to endorse or promote products derived from this software
 *     without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <errno.h>

#include <murphy/common/debug.h>

#include "srs/daemon/plugin.h"
#include "srs/daemon/client.h"

#define SEARCH_NAME    "search-client"
#define SEARCH_DESCR   "A trivial search plugin for SRS."
#define SEARCH_AUTHORS "Krisztian Litkey <kli@iki.fi>"
#define SEARCH_VERSION "0.0.1"

#define DICTIONARY     "general"
#define COMMAND        "google-chrome \"http://google.com/search?q=__url__\""

typedef struct {
    srs_plugin_t *self;                  /* us */
    srs_client_t *c;                     /* our client instance */
    const char   *dict;                  /* dictionary to use */
    const char   *cmd;                   /* search command template */
} search_t;



static int focus_cb(srs_client_t *c, srs_voice_focus_t focus)
{
    srs_context_t *srs = c->srs;
    search_t      *sch = c->user_data;
    const char    *state;

    MRP_UNUSED(srs);
    MRP_UNUSED(sch);

    switch (focus) {
    case SRS_VOICE_FOCUS_NONE:      state = "none";      break;
    case SRS_VOICE_FOCUS_SHARED:    state = "shared";    break;
    case SRS_VOICE_FOCUS_EXCLUSIVE: state = "exclusive"; break;
    default:                        state = "unknown";
    }

    mrp_debug("search plugin focus is now: %s", state);

    return TRUE;
}


static int url_encode(char *buf, size_t size, int ntoken, char **tokens)
{
    unsigned char *p, *s, L, H;
    int            l, i;

    p = (unsigned char *)buf;
    l = size;

    for (i = 0; i < ntoken; i++) {
        if (l <= 0) {
        overflow:
            errno = EOVERFLOW;
            return -1;
        }

        if (i != 0) {
            *p++ = '+';
            l--;
        }

        s = (unsigned char *)tokens[i];

        while (*s && l > 0) {
            switch (*s) {
            case '0'...'9':
            case 'A'...'Z':
            case 'a'...'z':
            case '-':
            case '_':
            case '.':
            case '~':
                *p++ = *s++;
                l--;
                break;
            default:
                if (l <= 3)
                    goto overflow;

                H = (*s & 0xf0) >> 4;
                L =  *s & 0x0f;

                if (/*0 <= H && */H <= 9) H += '0';
                else                      H += 'A' - 10;
                if (/*0 <= L && */L <= 9) L += '0';
                else                      L += 'A' - 10;

                p[0] = '%';
                p[1] = H;
                p[2] = L;

                p += 3;
                l -= 3;
                s++;
            }
        }
    }

    if (l <= 0)
        goto overflow;

    *p = '\0';

    return size - l;
}



static int command_cb(srs_client_t *c, int idx, int ntoken, char **tokens,
                      uint32_t *start, uint32_t *end, srs_audiobuf_t *audio)
{
    search_t *sch = (search_t *)c->user_data;
    char      qry[1024], cmd[8192];
    int       l;

    MRP_UNUSED(sch);

    MRP_UNUSED(idx);
    MRP_UNUSED(start);
    MRP_UNUSED(end);
    MRP_UNUSED(audio);

    mrp_debug("got search plugin command...");

    tokens += 2;
    ntoken -= 2;

    if (url_encode(qry, sizeof(qry), ntoken, tokens) > 0) {
        mrp_log_info("search-client got query: '%s'", qry);

        l = snprintf(cmd, sizeof(cmd), sch->cmd, qry);

        if (l < (int)sizeof(cmd)) {
            mrp_log_info("search-client executing '%s'", cmd);
            system(cmd);
        }
    }
    else
        mrp_log_error("search-client: URL encoding failed");

    return TRUE;
}

static int create_search(srs_plugin_t *plugin)
{
    search_t *sch;

    mrp_debug("creating search plugin");

    sch = mrp_allocz(sizeof(*sch));

    if (sch != NULL) {
        plugin->plugin_data = sch;
        return TRUE;
    }
    else
        return FALSE;
}


static int config_search(srs_plugin_t *plugin, srs_cfg_t *settings)
{
    search_t    *sch = (search_t *)plugin->plugin_data;
    static char  cmdbuf[1024];
    const char  *dict, *cmd;
    char        *p, *q;
    int          l, n, nobg;

    mrp_debug("configure search plugin");

    dict = srs_config_get_string(settings, "search.dictionary", DICTIONARY);
    cmd  = srs_config_get_string(settings, "search.command"   , COMMAND);

    q = (char *)cmd + strlen(cmd);
    while (q > cmd && (*q == ' ' || *q == '\t'))
        q--;
    if (*q != '&')
        nobg = TRUE;
    else
        nobg = FALSE;

    if ((q = strstr(cmd, "__url__")) == NULL) {
        mrp_log_error("Invalid search command '%s', has no __url__ tag.", cmd);
        return FALSE;
    }

    p = cmdbuf;
    l = sizeof(cmdbuf);
    n = q - cmd;

    n  = snprintf(p, l, "%*.*s", n, n, cmd);
    p += n;
    l -= n;

    if (l <= 0) {
    overflow:
        mrp_log_error("Invalid search command '%s', too long.", cmd);
        return FALSE;
    }

    n  = snprintf(p, l, "%%s");
    p += n;
    l -= n;

    if (l <= 0)
        goto overflow;

    q += strlen("__url__");
    n  = snprintf(p, l, "%s%s", q, nobg ? "&" : "");
    p += n;
    l -= n;

    if (l <= 0)
        goto overflow;

    sch->cmd  = cmdbuf;
    sch->dict = dict;

    mrp_log_info("search plugin dictionary: '%s'", sch->dict);
    mrp_log_info("search plugin command: '%s'", sch->cmd);

    return TRUE;
}


static int start_search(srs_plugin_t *plugin)
{
    static srs_client_ops_t ops = {
        .notify_focus   = focus_cb,
        .notify_command = command_cb
    };

    char           cmd1[1024], cmd2[1024];
    search_t      *sch    = plugin->plugin_data;
    char          *cmds[2]= { &cmd1[0], &cmd2[0] };
    int            ncmd   = (int)MRP_ARRAY_SIZE(cmds);
    srs_context_t *srs    = plugin->srs;

    srs_client_t  *c;
    const char    *name, *appcls, *id;

    mrp_debug("starting search plugin");

    name   = "search";
    appcls = "player";
    id     = "search";

    snprintf(cmd1, sizeof(cmd1), "search for __push_dict__(%s) *", sch->dict);
    snprintf(cmd2, sizeof(cmd2), "google for __push_dict__(%s) *", sch->dict);

    c = client_create(srs, SRS_CLIENT_TYPE_BUILTIN,
                      name, appcls, cmds, ncmd, id, &ops, sch);

    if (c == NULL)
        return FALSE;

    sch->self = plugin;
    sch->c    = c;

    client_request_focus(c, SRS_VOICE_FOCUS_SHARED);

    return TRUE;
}


static void stop_search(srs_plugin_t *plugin)
{
    search_t *sch = (search_t *)plugin->plugin_data;

    mrp_debug("stop search plugin");

    client_destroy(sch->c);
    sch->c = NULL;

    return;
}


static void destroy_search(srs_plugin_t *plugin)
{
    srs_context_t *srs = plugin->srs;
    search_t      *sch = (search_t *)plugin->plugin_data;

    MRP_UNUSED(srs);

    mrp_debug("destroy search plugin");

    mrp_free(sch);
}


SRS_DECLARE_PLUGIN(SEARCH_NAME, SEARCH_DESCR, SEARCH_AUTHORS, SEARCH_VERSION,
                   create_search, config_search, start_search, stop_search,
                   destroy_search)
