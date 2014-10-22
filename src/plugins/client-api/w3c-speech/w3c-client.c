/*
 * Copyright (c) 2012-2014, Intel Corporation
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

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <getopt.h>
#include <stdarg.h>
#include <ctype.h>

#include <murphy/common/macros.h>
#include <murphy/common/debug.h>
#include <murphy/common/log.h>
#include <murphy/common/mm.h>
#include <murphy/common/mainloop.h>
#include <murphy/common/transport.h>
#include <murphy/common/json.h>

#include <breedline/breedline-murphy.h>

#define DEFAULT_SERVER "unxs:@winthorpe.w3c-speech"

/*
 * a W3C test client
 */

typedef struct {
    mrp_mainloop_t  *ml;                 /* our mainloop */
    brl_t           *brl;                /* breedline for terminal I/O */
    int              log_mask;           /* log verbosity mask */
    char            *server;             /* server (transport) address */
    const char      *atype;              /* resolved type */
    mrp_sockaddr_t   addr;               /* resolved address */
    socklen_t        alen;               /* resolved length */
    mrp_transport_t *t;                  /* transport to server */
    mrp_timer_t     *conntmr;            /* connection timer */
    int              connected : 1;      /* whether we have a connection */
    uint32_t         reqno;              /* request number */
} client_t;


typedef struct {
    const char  *command;
    int        (*handler)(client_t *c, int narg, char **args);
} command_t;

static int transport_connect(client_t *c);
static void transport_destroy(client_t *c);
static void connection_timer_start(client_t *c);
static void connection_timer_stop(client_t *c);
static void mainloop_quit(client_t *c, int exit_code);

static void execute_command(client_t *c, int narg, char **args);

static void set_prompt(client_t *c, const char *prompt)
{
    brl_set_prompt(c->brl, prompt);
}


static void show_prompt(client_t *c)
{
    if (c->brl != NULL)
        brl_show_prompt(c->brl);
}


static void hide_prompt(client_t *c)
{
    if (c->brl != NULL)
        brl_hide_prompt(c->brl);
}


static void print(client_t *c, const char *format, ...)
{
    va_list ap;

    hide_prompt(c);

    va_start(ap, format);
    vfprintf(stdout, format, ap);
    fputc('\n', stdout);
    fflush(stdout);
    va_end(ap);

    show_prompt(c);
}


static void client_set_defaults(client_t *c, const char *argv0)
{
    MRP_UNUSED(c);
    MRP_UNUSED(argv0);

    c->server   = mrp_strdup(DEFAULT_SERVER);
    c->log_mask = MRP_LOG_UPTO(MRP_LOG_INFO);
}


static void client_connected(client_t *c)
{
    c->connected = true;
    hide_prompt(c);
    set_prompt(c, "w3c-client");
    show_prompt(c);
}


static void client_disconnected(client_t *c)
{
    c->connected = false;
    hide_prompt(c);
    set_prompt(c, "disconnected");
    show_prompt(c);
}


static client_t *client_create(const char *argv0)
{
    client_t *c;

    c = mrp_allocz(sizeof(*c));

    if (c != NULL)
        client_set_defaults(c, argv0);

    return c;
}


static void client_destroy(client_t *c)
{
    mrp_free(c);
}


static int mainloop_create(client_t *c)
{
    c->ml = mrp_mainloop_create();

    if (c->ml != NULL)
        return 0;
    else {
        fprintf(stderr, "Failed to create mainloop.");
        exit(1);
    }
}


static int mainloop_run(client_t *c)
{
    if (c != NULL && c->ml != NULL)
        return mrp_mainloop_run(c->ml);
    else {
        errno = EINVAL;
        return -1;
    }
}


static void mainloop_quit(client_t *c, int exit_code)
{
    if (c != NULL && c->ml != NULL)
        mrp_mainloop_quit(c->ml, exit_code);
    else
        exit(exit_code);
}


static void mainloop_destroy(client_t *c)
{
    mrp_mainloop_destroy(c->ml);
    c->ml = NULL;
}


static void transport_recv_evt(mrp_transport_t *t, mrp_json_t *msg,
                               void *user_data)
{
    client_t   *c = (client_t *)user_data;
    const char *s;

    MRP_UNUSED(t);
    MRP_UNUSED(c);

    s = mrp_json_object_to_string(msg);

    print(c, "received message:");
    print(c, "  %s", s);
}


static void transport_recvfrom_evt(mrp_transport_t *t, mrp_json_t *msg,
                                   mrp_sockaddr_t *addr, socklen_t addrlen,
                                   void *user_data)
{
    MRP_UNUSED(t);
    MRP_UNUSED(msg);
    MRP_UNUSED(addr);
    MRP_UNUSED(addrlen);
    MRP_UNUSED(user_data);
}


static void transport_closed_evt(mrp_transport_t *t, int error, void *user_data)
{
    client_t *c = (client_t *)user_data;

    MRP_UNUSED(t);

    if (error != 0)
        print(c, "Connection to server closed with error %d (%s).", error,
              strerror(error));
    else
        print(c, "Connection to server closed.");

    transport_destroy(c);
    connection_timer_start(c);
}


static int transport_connect(client_t *c)
{
    static mrp_transport_evt_t evt = {
        { .recvjson     = transport_recv_evt,    },
        { .recvjsonfrom = transport_recvfrom_evt },
        .closed         = transport_closed_evt,
        .connection     = NULL
    };

    mrp_sockaddr_t  addr;
    socklen_t       alen;
    const char     *atype;
    int             flags;

    alen = mrp_transport_resolve(NULL, c->server, &addr, sizeof(addr), &atype);

    if (alen <= 0) {
        print(c, "Failed to resolve transport address '%s'.", c->server);
        return -1;
    }

    flags = MRP_TRANSPORT_MODE_JSON | MRP_TRANSPORT_REUSEADDR;

    if (c->t == NULL)
        c->t = mrp_transport_create(c->ml, atype, &evt, c, flags);

    if (c->t == NULL) {
        print(c, "Failed to create transport (for '%s').", c->server);
        return -1;
    }

    if (!mrp_transport_connect(c->t, &addr, alen))
        return -1;
    else {
        client_connected(c);
        return 0;
    }
}


static void transport_destroy(client_t *c)
{
    mrp_transport_destroy(c->t);
    c->t = NULL;
    client_disconnected(c);
}


static void try_connect(mrp_timer_t *t, void *user_data)
{
    client_t *c = (client_t *)user_data;

    if (transport_connect(c) == 0) {
        mrp_del_timer(t);
        c->conntmr = NULL;
    }
}


static int transport_send(client_t *c, mrp_json_t *msg)
{
    if (mrp_transport_sendjson(c->t, msg))
        return 0;
    else {
        errno = EIO;
        return -1;
    }
}


static void connection_timer_start(client_t *c)
{
    if (transport_connect(c) < 0)
        c->conntmr = mrp_add_timer(c->ml, 1500, try_connect, c);
}


static void connection_timer_stop(client_t *c)
{
    mrp_del_timer(c->conntmr);
    c->conntmr = NULL;
}


static void sighandler(mrp_sighandler_t *h, int signum, void *user_data)
{
    client_t *c = (client_t *)user_data;

    MRP_UNUSED(h);

    switch (signum) {
    case SIGINT:
        printf("Received SIGINT, exiting...");
        mainloop_quit(c, 0);
        break;

    case SIGTERM:
        printf("Received SIGTERM, exiting...");
        mainloop_quit(c, 0);
        break;
    }
}


static void setup_signals(client_t *c)
{
    mrp_add_sighandler(c->ml, SIGINT , sighandler, c);
    mrp_add_sighandler(c->ml, SIGTERM, sighandler, c);
}


static int split_input(char *input, int narg, char **args)
{
    int   n, l;
    char *p, *e;

    n = 0;
    p = input;
    e = NULL;

    while (*p) {
        while (*p == ' ' || *p == '\t')
            p++;

        args[n++] = p;

        if (n >= narg) {
            errno = EOVERFLOW;
            return -1;
        }

        while (*p && ((e && *p != *e) || (!e && (*p != ' ' && *p != '\t')))) {
            if (*p == '\\') {
                if ((l = strlen(p)) > 1) {
                    memmove(p, p + 1, l - 1);
                    goto next;
                }
                else {
                    *p = '\0';
                    break;
                }
            }

            if (!e && (*p == '\'' || *p == '"'))
                e = p;

        next:
            p++;
        }

        if (e && *p == *e)
            p++;

        if (*p)
            *p++ = '\0';

        e = NULL;

        mrp_debug("arg #%d: '%s'\n", n, args[n-1]);
    }

    return n;
}


static void terminal_cb(brl_t *brl, const char *input, void *user_data)
{
    client_t *c   = (client_t *)user_data;
    int       len = input ? strlen(input) + 1 : 0;
    char      buf[len], *args[64];
    int       narg;

    if (len > 1) {
        brl_add_history(brl, input);
        hide_prompt(c);

        strcpy(buf, input);
        narg = split_input(buf, MRP_ARRAY_SIZE(args), args);
        if (narg > 0)
            execute_command(c, narg, &args[0]);
        else
            print(c, "failed to parse input '%s'", input);

        show_prompt(c);
    }
}


static void terminal_setup(client_t *c)
{
    if ((c->brl = brl_create_with_murphy(fileno(stdin), "disconnected", c->ml,
                                         terminal_cb, c)) != NULL)
        brl_show_prompt(c->brl);
    else {
        fprintf(stderr, "Failed to set up terminal input.");
        exit(1);
    }
}


static void terminal_cleanup(client_t *c)
{
    if (c != NULL && c->brl != NULL) {
        brl_destroy(c->brl);
        c->brl = NULL;
    }
}


static void print_usage(const char *argv0, int exit_code, const char *fmt, ...)
{
    va_list     ap;
    const char *exe;

    if (fmt && *fmt) {
        va_start(ap, fmt);
        vprintf(fmt, ap);
        va_end(ap);
    }

    exe = strrchr(argv0, '/');

    printf("usage: %s [options]\n\n"
           "The possible options are:\n"
           "  -v, --verbose                  increase logging verbosity\n"
           "  -d, --debug                    enable debug messages\n"
           "  -h, --help                     show help on usage\n", exe);
    printf("\n");

    if (exit_code < 0)
        return;
    else
        exit(exit_code);
}


static void parse_cmdline(client_t *c, int argc, char **argv)
{
#   define OPTIONS "s:vd:h"
    struct option options[] = {
        { "server"    , required_argument, NULL, 's' },
        { "verbose"   , no_argument      , NULL, 'v' },
        { "debug"     , required_argument, NULL, 'd' },
        { "help"      , no_argument      , NULL, 'h' },
        { NULL, 0, NULL, 0 }
    };

    int opt;

    while ((opt = getopt_long(argc, argv, OPTIONS, options, NULL)) != -1) {
        switch (opt) {
        case 's':
            mrp_free(c->server);
            c->server = mrp_strdup(optarg);
            break;

        case 'v':
            c->log_mask <<= 1;
            c->log_mask  |= 1;
            mrp_log_set_mask(c->log_mask);
            break;

        case 'd':
            mrp_debug_set_config(optarg);
            mrp_debug_enable(TRUE);
            break;

        case 'h':
            print_usage(argv[0], -1, "");
            exit(0);
            break;

        default:
            print_usage(argv[0], EINVAL, "invalid option '%c'", opt);
        }
    }
}


static int check_connection(client_t *c, int notify)
{
    if (!c->connected) {
        if (notify)
            print(c, "Connection to server is down.");

        errno = ENOTCONN;
        return -1;
    }

    return 0;
}


static int cmd_get_timestamp(client_t *c, int narg, char **args)
{
    mrp_json_t *req;
    int         status;

    MRP_UNUSED(args);

    if (check_connection(c, TRUE) < 0)
        return -1;

    if (narg > 0)
        print(c, "Ignoring unused arguments...");

    if ((req = mrp_json_create(MRP_JSON_OBJECT)) != NULL) {
        mrp_json_add_integer(req, "reqno", c->reqno++);
        mrp_json_add_string (req, "type" , "timestamp");

        if (transport_send(c, req) < 0)
            status = -1;
        else
            status = 0;

        mrp_json_unref(req);
    }

    return status;
}


static int parse_set(client_t *c, mrp_json_t *set, int narg, char **args)
{
    char var[64], *val;
    int  i, l;

    for (i = 0; i < narg; i++) {
        val = strchr(args[i], '=');

        if (val == NULL) {
            print(c, "Invalid variable initializer/setting '%s'.", args[i]);

            errno = EINVAL;
            return -1;
        }

        snprintf(var, sizeof(var), "%*.*s",
                 (int)(val - args[i]), (int)(val - args[i]), args[i]);
        val++;

        mrp_debug("* '%s' = '%s'\n", var, val);

        if (!strcmp(var, "grammars")) {
            mrp_json_t *arr = mrp_json_create(MRP_JSON_ARRAY);
            mrp_json_t *grm;
            char       *p, *n;

            for (p = val; p && *p; p = n) {
                n = strchr(p, ',');
                l = n ? (int)(n - p) : (int)strlen(p);

                mrp_json_array_append(arr,
                                      grm = mrp_json_create(MRP_JSON_OBJECT));

                mrp_json_add_string_slice(grm, "src"   , p, l);
                mrp_json_add_double      (grm, "weight", 1.0);

                n = n ? n + 1 : n;
            }

            mrp_json_add(set, var, arr);
        }
        else if (!strncmp(val, "int:", l=4))
            mrp_json_add_integer(set, var, strtol(val+l, NULL, 10));
        else if (!strncmp(val, "bln:", l=4))
            mrp_json_add_boolean(set, var, var[l] == '1' || var[l] == 't');
        else if (!strncmp(val, "str:", l=4))
            mrp_json_add_string(set, var, val+l);
        else if (!strncmp(val, "dbl:", l=4))
            mrp_json_add_double(set, var, strtod(val+l, NULL));
        else if (val[0] == '\'' || val[0] == '\"')
            mrp_json_add_string_slice(set, var, val + 1, strlen(val) - 2);
        else if (!strcasecmp(val, "true") || (!strcasecmp(val, "false")))
            mrp_json_add_boolean(set, var, *val == 't');
        else if (!strncmp(val, "strarr:", l=7)) {
            mrp_json_t *arr = mrp_json_create(MRP_JSON_ARRAY);
            char       *p, *n;

            for (p = val + l; p && *p; p = n) {
                n = strchr(p, ',');
                l = n ? (int)(n - p) : (int)strlen(p);

                mrp_json_array_append_string_slice(arr, p, l);

                n = n ? n + 1 : n;
            }

            mrp_json_add(set, var, arr);
        }
        else if (!strncmp(val, "intarr:", l=7)) {
            mrp_json_t *arr = mrp_json_create(MRP_JSON_ARRAY);
            char       *p, *n;

            for (p = val + l; p && *p; p = n) {
                n = strchr(p, ',');
                l = n ? (int)(n - p) : (int)strlen(p);

                mrp_json_array_append_integer(arr, strtol(p, NULL, 10));

                n = n ? n + 1 : n;
            }

            mrp_json_add(set, var, arr);
        }
        else if (!strncmp(val, "dblarr:", l=7)) {
            mrp_json_t *arr = mrp_json_create(MRP_JSON_ARRAY);
            char       *p, *n;

            for (p = val + l; p && *p; p = n) {
                n = strchr(p, ',');
                l = n ? (int)(n - p) : (int)strlen(p);

                mrp_json_array_append_double(arr, strtod(p, NULL));

                n = n ? n + 1 : n;
            }

            mrp_json_add(set, var, arr);
        }
        else if (!strncmp(val, "blnarr:", l=7)) {
            mrp_json_t *arr = mrp_json_create(MRP_JSON_ARRAY);
            char       *p, *n;

            for (p = val + l; p && *p; p = n) {
                n = strchr(p, ',');
                l = n ? (int)(n - p) : (int)strlen(p);

                mrp_json_array_append_boolean(arr, *p == 't');

                n = n ? n + 1 : n;
            }

            mrp_json_add(set, var, arr);
        }
        else if (isdigit(*val) || *val == '-' || *val == '+') {
            int     iv;
            double  dv;
            char   *end;

            iv = strtol(val, &end, 10);

            if (end && !*end) {
                mrp_json_add_integer(set, var, iv);
                continue;
            }

            if (end && *end == '.') {
                dv = strtod(val, &end);

                if (end && !*end) {
                    mrp_json_add_double(set, var, dv);
                    continue;
                }
            }

            mrp_json_add_string(set, var, val);
        }
        else
            mrp_json_add_string(set, var, val);
    }

    return 0;
}


static int cmd_create_recognizer(client_t *c, int narg, char **args)
{
    mrp_json_t *req, *set;
    int         status;

    if (check_connection(c, TRUE) < 0)
        return -1;

    if ((req = mrp_json_create(MRP_JSON_OBJECT)) != NULL) {
        mrp_json_add_integer(req, "reqno" , c->reqno++);
        mrp_json_add_string (req, "type"  , "create");
        mrp_json_add_string (req, "object", "recognizer");

        if (narg > 0) {
            mrp_json_add(req, "set", set = mrp_json_create(MRP_JSON_OBJECT));

            if (parse_set(c, set, narg, args) < 0)
                goto fail;
        }

        if (transport_send(c, req) < 0)
            status = -1;
        else
            status = 0;

        mrp_json_unref(req);

        return status;
    }

 fail:
    return -1;
}


static int cmd_delete_recognizer(client_t *c, int narg, char **args)
{
    mrp_json_t *req;
    int         i, id, status;

    if (narg <= 0) {
        print(c, "Can't delete recognizer, no ID given.");
        errno = EINVAL;

        return -1;
    }

    if (check_connection(c, TRUE) < 0)
        return -1;

    status = 0;
    for (i = 0; i < narg; i++) {
        id = strtoul(args[i], NULL, 10);

        if ((req = mrp_json_create(MRP_JSON_OBJECT)) != NULL) {
            mrp_json_add_integer(req, "reqno", c->reqno++);
            mrp_json_add_string (req, "type" , "delete");
            mrp_json_add_integer(req, "id"   , id);

            if (transport_send(c, req) < 0)
                status = -1;

            mrp_json_unref(req);
        }
    }

    return status;
}


static int cmd_set_recognizer(client_t *c, int narg, char **args)
{
    mrp_json_t *req, *set;
    int         id, status;

    if (narg < 2) {
        print(c, "Can't set variable, need ID, and variable assignment.");
        errno = EINVAL;
        return -1;
    }

    if (check_connection(c, TRUE) < 0)
        return -1;

    id = strtoul(args[0], NULL, 10);
    args++;
    narg--;

    if ((req = mrp_json_create(MRP_JSON_OBJECT)) == NULL)
        return -1;

    mrp_json_add_integer(req, "reqno", c->reqno++);
    mrp_json_add_string (req, "type" , "set");
    mrp_json_add_integer(req, "id"   , id);
    mrp_json_add        (req, "set"  , set = mrp_json_create(MRP_JSON_OBJECT));

    if (parse_set(c, set, narg, args) < 0) {
        errno  = EINVAL;
        status = -1;
    }
    else {
        if (transport_send(c, req) < 0)
            status = -1;
        else
            status = 0;
    }

    mrp_json_unref(req);

    return status;
}


static int cmd_start_recognizer(client_t *c, int narg, char **args)
{
    mrp_json_t *req;
    int         i, id, status;

    if (check_connection(c, TRUE) < 0)
        return -1;

    if (narg <= 0) {
        print(c, "Can't start recognizer, no ID given.");
        errno = EINVAL;

        return -1;
    }

    status = 0;
    for (i = 0; i < narg; i++) {
        id = strtoul(args[i], NULL, 10);

        if ((req = mrp_json_create(MRP_JSON_OBJECT)) != NULL) {
            mrp_json_add_integer(req, "reqno" , c->reqno++);
            mrp_json_add_string (req, "type"  , "invoke");
            mrp_json_add_string (req, "method", "start");
            mrp_json_add_integer(req, "id"    , id);

            if (transport_send(c, req) < 0)
                status = -1;

            mrp_json_unref(req);
        }
    }

    return status;
}


static int cmd_stop_recognizer(client_t *c, int narg, char **args)
{
    mrp_json_t *req;
    int         i, id, status;

    if (check_connection(c, TRUE) < 0)
        return -1;

    if (narg <= 0) {
        print(c, "Can't start recognizer, no ID given.");
        errno = EINVAL;

        return -1;
    }

    status = 0;
    for (i = 0; i < narg; i++) {
        id = strtoul(args[i], NULL, 10);

        if ((req = mrp_json_create(MRP_JSON_OBJECT)) != NULL) {
            mrp_json_add_integer(req, "reqno" , c->reqno++);
            mrp_json_add_string (req, "type"  , "invoke");
            mrp_json_add_string (req, "method", "stop");
            mrp_json_add_integer(req, "id"    , id);

            if (transport_send(c, req) < 0)
                status = -1;

            mrp_json_unref(req);
        }
    }

    return status;
}


static int cmd_abort_recognizer(client_t *c, int narg, char **args)
{
    mrp_json_t *req;
    int         i, id, status;

    if (check_connection(c, TRUE) < 0)
        return -1;

    if (narg <= 0) {
        print(c, "Can't abort recognizer, no ID given.");
        errno = EINVAL;

        return -1;
    }

    status = 0;
    for (i = 0; i < narg; i++) {
        id = strtoul(args[i], NULL, 10);

        if ((req = mrp_json_create(MRP_JSON_OBJECT)) != NULL) {
            mrp_json_add_integer(req, "reqno", c->reqno++);
            mrp_json_add_string (req, "type"  , "invoke");
            mrp_json_add_string (req, "method", "abort");
            mrp_json_add_integer(req, "id"   , id);

            if (transport_send(c, req) < 0)
                status = -1;

            mrp_json_unref(req);
        }
    }

    return status;
}


static int cmd_list_voices(client_t *c, int narg, char **args)
{
    mrp_json_t *req;
    const char *lang;
    int         status;

    switch (narg) {
    case 0: lang = NULL;    break;
    case 1: lang = args[0]; break;
    default:
        print(c, "list-voices expects either a single or no arguments.");
        errno = EINVAL;
        return -1;
    }

    if (check_connection(c, TRUE) < 0)
        return -1;

    if ((req = mrp_json_create(MRP_JSON_OBJECT)) == NULL)
        return -1;

    mrp_json_add_integer(req, "reqno" , c->reqno++);
    mrp_json_add_string (req, "type"  , "invoke");
    mrp_json_add_string (req, "method", "list-voices");
    if (lang != NULL)
        mrp_json_add_string(req, "lang", lang);

    if (transport_send(c, req) < 0)
        status = -1;
    else
        status = 0;

    mrp_json_unref(req);


    return status;
}


static int cmd_create_utterance(client_t *c, int narg, char **args)
{
    mrp_json_t *req, *set;
    int         status;

    if (check_connection(c, TRUE) < 0)
        return -1;

    if ((req = mrp_json_create(MRP_JSON_OBJECT)) != NULL) {
        mrp_json_add_integer(req, "reqno" , c->reqno++);
        mrp_json_add_string (req, "type"  , "create");
        mrp_json_add_string (req, "object", "utterance");

        if (narg > 0) {
            mrp_json_add(req, "set", set = mrp_json_create(MRP_JSON_OBJECT));

            if (parse_set(c, set, narg, args) < 0)
                goto fail;
        }

        if (transport_send(c, req) < 0)
            status = -1;
        else
            status = 0;

        mrp_json_unref(req);

        return status;
    }

 fail:
    return -1;
}


static int cmd_delete_utterance(client_t *c, int narg, char **args)
{
    mrp_json_t *req;
    int         i, id, status;

    if (narg <= 0) {
        print(c, "Can't delete utterance, no ID given.");
        errno = EINVAL;

        return -1;
    }

    if (check_connection(c, TRUE) < 0)
        return -1;

    status = 0;
    for (i = 0; i < narg; i++) {
        id = strtoul(args[i], NULL, 10);

        if ((req = mrp_json_create(MRP_JSON_OBJECT)) != NULL) {
            mrp_json_add_integer(req, "reqno", c->reqno++);
            mrp_json_add_string (req, "type" , "delete");
            mrp_json_add_integer(req, "id"   , id);

            if (transport_send(c, req) < 0)
                status = -1;

            mrp_json_unref(req);
        }
    }

    return status;
}


static int cmd_set_utterance(client_t *c, int narg, char **args)
{
    mrp_json_t *req, *set;
    int         id, status;

    if (narg < 2) {
        print(c, "Can't set variable, need ID, and variable assignment.");
        errno = EINVAL;
        return -1;
    }

    if (check_connection(c, TRUE) < 0)
        return -1;

    id = strtoul(args[0], NULL, 10);
    args++;
    narg--;

    if ((req = mrp_json_create(MRP_JSON_OBJECT)) == NULL)
        return -1;

    mrp_json_add_integer(req, "reqno", c->reqno++);
    mrp_json_add_string (req, "type" , "set");
    mrp_json_add_integer(req, "id"   , id);
    mrp_json_add        (req, "set"  , set = mrp_json_create(MRP_JSON_OBJECT));

    if (parse_set(c, set, narg, args) < 0) {
        errno  = EINVAL;
        status = -1;
    }
    else {
        if (transport_send(c, req) < 0)
            status = -1;
        else
            status = 0;
    }

    mrp_json_unref(req);

    return status;
}


static int cmd_speak_utterance(client_t *c, int narg, char **args)
{
    mrp_json_t *req;
    int         i, id, status;

    if (check_connection(c, TRUE) < 0)
        return -1;

    if (narg <= 0) {
        print(c, "Can't speak utterance, no ID given.");
        errno = EINVAL;

        return -1;
    }

    status = 0;
    for (i = 0; i < narg; i++) {
        id = strtoul(args[i], NULL, 10);

        if ((req = mrp_json_create(MRP_JSON_OBJECT)) != NULL) {
            mrp_json_add_integer(req, "reqno", c->reqno++);
            mrp_json_add_string (req, "type"  , "invoke");
            mrp_json_add_string (req, "method", "speak");
            mrp_json_add_integer(req, "id"   , id);

            if (transport_send(c, req) < 0)
                status = -1;

            mrp_json_unref(req);
        }
    }

    return status;
}


static int cmd_cancel_utterance(client_t *c, int narg, char **args)
{
    mrp_json_t *req;
    int         i, id, status;

    if (check_connection(c, TRUE) < 0)
        return -1;

    if (narg <= 0) {
        print(c, "Can't cancel utterance, no ID given.");
        errno = EINVAL;

        return -1;
    }

    status = 0;
    for (i = 0; i < narg; i++) {
        id = strtoul(args[i], NULL, 10);

        if ((req = mrp_json_create(MRP_JSON_OBJECT)) != NULL) {
            mrp_json_add_integer(req, "reqno", c->reqno++);
            mrp_json_add_string (req, "type"  , "invoke");
            mrp_json_add_string (req, "method", "cancel");
            mrp_json_add_integer(req, "id"   , id);

            if (transport_send(c, req) < 0)
                status = -1;

            mrp_json_unref(req);
        }
    }

    return status;
}


static int cmd_pause_utterance(client_t *c, int narg, char **args)
{
    mrp_json_t *req;
    int         i, id, status;

    if (check_connection(c, TRUE) < 0)
        return -1;

    if (narg <= 0) {
        print(c, "Can't pause utterance, no ID given.");
        errno = EINVAL;

        return -1;
    }

    status = 0;
    for (i = 0; i < narg; i++) {
        id = strtoul(args[i], NULL, 10);

        if ((req = mrp_json_create(MRP_JSON_OBJECT)) != NULL) {
            mrp_json_add_integer(req, "reqno", c->reqno++);
            mrp_json_add_string (req, "type"  , "invoke");
            mrp_json_add_string (req, "method", "pause");
            mrp_json_add_integer(req, "id"   , id);

            if (transport_send(c, req) < 0)
                status = -1;

            mrp_json_unref(req);
        }
    }

    return status;
}


static int cmd_resume_utterance(client_t *c, int narg, char **args)
{
    mrp_json_t *req;
    int         i, id, status;

    if (check_connection(c, TRUE) < 0)
        return -1;

    if (narg <= 0) {
        print(c, "Can't resume utterance, no ID given.");
        errno = EINVAL;

        return -1;
    }

    status = 0;
    for (i = 0; i < narg; i++) {
        id = strtoul(args[i], NULL, 10);

        if ((req = mrp_json_create(MRP_JSON_OBJECT)) != NULL) {
            mrp_json_add_integer(req, "reqno", c->reqno++);
            mrp_json_add_string (req, "type"  , "invoke");
            mrp_json_add_string (req, "method", "resume");
            mrp_json_add_integer(req, "id"   , id);

            if (transport_send(c, req) < 0)
                status = -1;

            mrp_json_unref(req);
        }
    }

    return status;
}


static int cmd_connect(client_t *c, int narg, char **args)
{
    MRP_UNUSED(narg);
    MRP_UNUSED(args);

    if (!c->connected)
        if (c->conntmr == NULL)
            connection_timer_start(c);

    return 0;
}


static int cmd_disconnect(client_t *c, int narg, char **args)
{
    MRP_UNUSED(narg);
    MRP_UNUSED(args);

    if (c->connected)
        transport_destroy(c);

    return 0;
}


static int cmd_quit(client_t *c, int narg, char **args)
{
    MRP_UNUSED(narg);
    MRP_UNUSED(args);

    print(c, "Exiting...");
    mrp_mainloop_quit(c->ml, 0);

    return 0;
}


static void execute_command(client_t *c, int narg, char **args)
{
    static command_t commands[] = {
        { "get-timestamp"       , cmd_get_timestamp        },
        { "create-recognizer"   , cmd_create_recognizer    },
        { "delete-recognizer"   , cmd_delete_recognizer    },
        { "set-recognizer"      , cmd_set_recognizer       },
        { "start-recognizer"    , cmd_start_recognizer     },
        { "stop-recognizer"     , cmd_stop_recognizer      },
        { "abort-recognizer"    , cmd_abort_recognizer     },
        { "list-voices"         , cmd_list_voices          },
        { "create-utterance"    , cmd_create_utterance     },
        { "delete-utterance"    , cmd_delete_utterance     },
        { "set-utterance"       , cmd_set_utterance        },
        { "speak-utterance"     , cmd_speak_utterance      },
        { "cancel-utterance"    , cmd_cancel_utterance     },
        { "pause-utterance"     , cmd_pause_utterance      },
        { "resume-utterance"    , cmd_resume_utterance     },
        { "connect"             , cmd_connect              },
        { "disconnect"          , cmd_disconnect           },
        { "quit"                , cmd_quit                 },
        { "exit"                , cmd_quit                 },
        { NULL, NULL }
    };

    command_t *cmd;
    size_t     len;

    len = strlen(args[0]);
    for (cmd = commands; cmd->command != NULL; cmd++) {
        if (!strncmp(args[0], cmd->command, len)) {
            cmd->handler(c, narg - 1, args + 1);
            return;
        }
    }

    print(c, "Unknown command '%s'...", args[0]);
}


int main(int argc, char *argv[])
{
    client_t *c;

    if ((c = client_create(argv[0])) != NULL) {
        parse_cmdline(c, argc, &argv[0]);

        mainloop_create(c);
        setup_signals(c);
        terminal_setup(c);
        connection_timer_start(c);

        mainloop_run(c);

        connection_timer_stop(c);
        terminal_cleanup(c);
        mainloop_destroy(c);

        client_destroy(c);
    }

    return 0;
}
