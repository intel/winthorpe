/*
 * Copyright (c) 2014, Intel Corporation
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
#include <stdarg.h>

#include <murphy/common/macros.h>
#include <murphy/common/debug.h>
#include <murphy/common/log.h>
#include <murphy/common/mm.h>
#include <murphy/common/mainloop.h>
#include <murphy/common/transport.h>
#include <murphy/common/json.h>

#include "srs/daemon/plugin.h"
#include "srs/daemon/client.h"

#include "w3c-server.h"
#include "w3c-message.h"
#include "w3c-protocol.h"

#define W3C_PLUGIN  "w3c-speech"
#define W3C_DESCR   "W3C speech API plugin for Winthorpe."
#define W3C_AUTHORS "Krisztian Litkey <kli@iki.fi>"
#define W3C_VERSION "0.0.1"


/*
 * plugin runtime context
 */

typedef struct {
    srs_plugin_t    *self;               /* our plugin instance */
    const char      *address;            /* transport address to listen on */
    int              sock;               /* or existing socket for transport */
    const char      *grammar_dir;        /* grammar directory */
    mrp_transport_t *lt;                 /* transport we listen on */
    mrp_list_hook_t  connections;        /* open connections */
    int              next_id;            /* next client id */
} w3c_server_t;


/*
 * a connection from the WRT
 */

typedef struct {
    mrp_list_hook_t  hook;               /* to list of connections */
    w3c_server_t    *s;                  /* W3C plugin */
    mrp_transport_t *t;                  /* transport to WRT */
    mrp_list_hook_t  clients;            /* clients for this connection */
} w3c_connection_t;


/*
 * requested client state
 */

typedef enum {
    W3C_RECOGNIZER_NONE,                 /* client in initial inactive state */
    W3C_RECOGNIZER_STOP,                 /* client has called 'start' */
    W3C_RECOGNIZER_START,                /* client has called 'stop' */
    W3C_RECOGNIZER_ABORT,                /* client has called 'abort' */
} w3c_state_t;


typedef enum {
    W3C_EVENT_NONE        = 0x000,
    W3C_EVENT_START       = 0x001,
    W3C_EVENT_END         = 0x002,
    W3C_EVENT_RESULT      = 0x004,
    W3C_EVENT_NOMATCH     = 0x008,
    W3C_EVENT_ERROR       = 0x010,
    W3C_EVENT_AUDIOSTART  = 0x020,
    W3C_EVENT_AUDIOEND    = 0x040,
    W3C_EVENT_SOUNDSTART  = 0x080,
    W3C_EVENT_SOUNDEND    = 0x100,
    W3C_EVENT_SPEECHSTART = 0x200,
    W3C_EVENT_SPEECHEND   = 0x400,
} w3c_event_t;


typedef struct {
    mrp_list_hook_t    hook;             /* to list of clients */
    w3c_connection_t  *conn;             /* connection to WRT */
    int                id;               /* client id */
    srs_client_t      *c;                /* associated client */
    w3c_state_t        req;              /* requested client state */
    char              *name;             /* client name */
    char              *appclass;         /* client application class */
    w3c_event_t        events;           /* mask of events to deliver */
    char             **grammars;         /* client grammars */
    int                ngrammar;         /* number of grammars */
    char              *lang;             /* client language */
    bool               continuous;       /* client in continuous mode */
    bool               interim;          /* deliver interim results */
    int                max_alt;          /* max. alternatives to deliver */
    char              *service;          /* recognition service URI */
} w3c_client_t;



#define message(...) mrp_json_build(MRP_JSON_OBJ(__VA_ARGS__, NULL))

static int reply_status(mrp_transport_t *t, int reqno, int status, ...)
{
    mrp_json_t *rpl;
    va_list     ap;
    const char *key;
    int         type;

    if ((rpl = mrp_json_create(MRP_JSON_OBJECT)) != NULL) {
        mrp_json_add_integer(rpl, "reqno" , reqno);
        mrp_json_add_string (rpl, "type"  , "status");
        mrp_json_add_integer(rpl, "status", status);

        va_start(ap, status);

        while ((key = va_arg(ap, const char *)) != NULL) {
            switch ((type = va_arg(ap, int))) {
            case MRP_JSON_BOOLEAN:
                mrp_json_add_boolean(rpl, key, va_arg(ap, int));
                break;
            case MRP_JSON_STRING:
                mrp_json_add_string(rpl, key, va_arg(ap, const char *));
                break;
            case MRP_JSON_INTEGER:
                mrp_json_add_integer(rpl, key, va_arg(ap, int));
                break;
            case MRP_JSON_DOUBLE:
                mrp_json_add_double(rpl, key, va_arg(ap, double));
                break;
            case MRP_JSON_OBJECT:
                mrp_json_add(rpl, key, va_arg(ap, void *));
                break;
            case MRP_JSON_ARRAY:
                mrp_json_add(rpl, key, va_arg(ap, void *));
                break;
            default:
                errno  = EINVAL;
                mrp_json_unref(rpl);
                return -1;
            }
        }

        va_end(ap);

        if (mrp_transport_sendjson(t, rpl))
            status = 0;
        else {
            errno  = EIO;
            status = -1;
        }

        mrp_json_unref(rpl);
    }
    else
        status = -1;

    return status;
}


static int reply_error(mrp_transport_t *t, int reqno, int status,
                       const char *error, mrp_json_t *req, const char *fmt, ...)
{
    char        msg[4096];
    const char *request;
    va_list     ap;

    va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt ? fmt : "", ap);
    va_end(ap);

    if (req != NULL)
        request = "request";
    else
        request = NULL;

    return reply_status(t, reqno, status,
                        "error"  , MRP_JSON_STRING, error,
                        "message", MRP_JSON_STRING, msg,
                        request  , MRP_JSON_OBJECT, req,
                        NULL);
}


static mrp_json_t *add_json_timestamp(mrp_json_t *msg, const char *name)
{
    mrp_json_t     *ts;
    struct timeval  tv;

    if (msg == NULL) {
        msg = mrp_json_create(MRP_JSON_OBJECT);

        if (msg == NULL)
            return NULL;

        if (gettimeofday(&tv, NULL) < 0)
            return NULL;

        mrp_json_add_integer(msg, "sec" , tv.tv_sec);
        mrp_json_add_integer(msg, "usec", tv.tv_usec);

        return msg;
    }
    else {
        if (name == NULL)
            name = "timestamp";

        if (gettimeofday(&tv, NULL) < 0)
            return NULL;

        if ((ts = mrp_json_add_member(msg, name, MRP_JSON_OBJECT)) == NULL)
            return NULL;

        mrp_json_add_integer(ts, "sec" , tv.tv_sec);
        mrp_json_add_integer(ts, "usec", tv.tv_usec);

        return ts;
    }
}


static int send_event(mrp_transport_t *t, int id, const char *event, ...)
{
    mrp_json_t *evt;
    va_list     ap;
    const char *key;
    int         type, status;

    if ((evt = mrp_json_create(MRP_JSON_OBJECT)) != NULL) {
        mrp_json_add_integer(evt, "reqno", 0);
        mrp_json_add_string (evt, "type" , "event");
        mrp_json_add_integer(evt, "id"   , id);
        add_json_timestamp  (evt, NULL);
        mrp_json_add_string (evt, "event", event);

        va_start(ap, event);

        while ((key = va_arg(ap, const char *)) != NULL) {
            switch ((type = va_arg(ap, int))) {
            case MRP_JSON_BOOLEAN:
                mrp_json_add_boolean(evt, key, va_arg(ap, int));
                break;
            case MRP_JSON_STRING:
                mrp_json_add_string(evt, key, va_arg(ap, const char *));
                break;
            case MRP_JSON_INTEGER:
                mrp_json_add_integer(evt, key, va_arg(ap, int));
                break;
            case MRP_JSON_DOUBLE:
                mrp_json_add_double(evt, key, va_arg(ap, double));
                break;
            case MRP_JSON_OBJECT:
                mrp_json_add(evt, key, va_arg(ap, void *));
                break;
            case MRP_JSON_ARRAY:
                mrp_json_add(evt, key, va_arg(ap, void *));
                break;
            default:
                errno  = EINVAL;
                mrp_json_unref(evt);
                return -1;
            }
        }

        va_end(ap);

        if (mrp_transport_sendjson(t, evt))
            status = 0;
        else {
            errno  = EIO;
            status = -1;
        }

        mrp_json_unref(evt);
    }
    else
        status = -1;

    return status;
}


static int reply_timestamp(mrp_transport_t *t, int reqno, mrp_json_t *req)
{
    mrp_json_t *ts = add_json_timestamp(NULL, NULL);

    MRP_UNUSED(req);

    if (ts != NULL)
        return reply_status(t, reqno, 0,
                            "timestamp", MRP_JSON_OBJECT, ts,
                            NULL);
    else
        return -1;
}


static int w3c_focus_notify(srs_client_t *client, srs_voice_focus_t focus)
{
    w3c_client_t *c = (w3c_client_t *)client->user_data;

    mrp_log_info("W3C-client#%d has now %s focus", c->id,
                 focus == SRS_VOICE_FOCUS_NONE ? "no" :
                 (focus == SRS_VOICE_FOCUS_SHARED ? "shared" : "exclusive"));

    switch (focus) {
    case SRS_VOICE_FOCUS_NONE:
        switch (c->req) {
        case W3C_RECOGNIZER_START:
            send_event(c->conn->t, c->id, "error",
                       "errorCode", MRP_JSON_STRING, "aborted",
                       "message"  , MRP_JSON_STRING, "voice focus lost",
                       NULL);
            break;
        default:
            break;
        }
        c->req = W3C_RECOGNIZER_NONE;
        break;

    case SRS_VOICE_FOCUS_SHARED:
    case SRS_VOICE_FOCUS_EXCLUSIVE:
        switch (c->req) {
        case W3C_RECOGNIZER_START:
            send_event(c->conn->t, c->id, "start",
                       NULL);
            break;
        default:
            /* WTF? */
            break;
        }
        break;

    default:
        /* WTF? */
        break;
    }

    return 0;
}


static int w3c_command_notify(srs_client_t *client, int idx,
                              int ntoken, char **tokens, uint32_t *start,
                              uint32_t *end, srs_audiobuf_t *audio)
{
    w3c_client_t *c = client->user_data;
    char          txt[16384], *p, *t;
    int           l, n, i;
    mrp_json_t   *results, *r;

    MRP_UNUSED(idx);
    MRP_UNUSED(start);
    MRP_UNUSED(end);
    MRP_UNUSED(audio);

    p = txt;
    l = sizeof(txt);
    t = "";

    for (i = 0; i < ntoken; i++) {
        n  = snprintf(p, l, "%s%s", t, tokens[i]);
        p += n;
        l -= n;

        if (l <= 0) {
            txt[sizeof(txt) - 1] = '\0';
            break;
        }

        t = " ";
    }

    if (!(results = mrp_json_create(MRP_JSON_ARRAY)))
        return 0;

    if (!mrp_json_array_append(results, r = mrp_json_create(MRP_JSON_OBJECT))) {
        mrp_json_unref(results);
        return 0;
    }

    mrp_json_add_double(r, "confidence", 1.0); /* yeah, right... */
    mrp_json_add_string(r, "transcript", txt);

    send_event(c->conn->t, c->id, "match",
               "final"  , MRP_JSON_BOOLEAN, true,
               "length" , MRP_JSON_INTEGER, 1,
               "results", MRP_JSON_OBJECT, results,
               NULL);

    return 0;
}


static int w3c_voice_notify(srs_client_t *client, srs_voice_event_t *event)
{
    w3c_client_t *c = client->user_data;

    MRP_UNUSED(client);
    MRP_UNUSED(c);
    MRP_UNUSED(event);

    mrp_log_info("Got W3C voice event...");

    return 0;
}


static w3c_client_t *find_client_by_id(w3c_connection_t *conn, int id)
{
    w3c_client_t    *c;
    mrp_list_hook_t *p, *n;

    mrp_list_foreach(&conn->clients, p, n) {
        c = mrp_list_entry(p, typeof(*c), hook);

        if (c->id == id)
            return c;
    }

    errno = ENOENT;

    return NULL;
}


static w3c_client_t *find_client_by_req(w3c_connection_t *conn, mrp_json_t *req)
{
    int id;

    if (mrp_json_get_integer(req, "id", &id))
        return find_client_by_id(conn, id);
    else {
        errno = EINVAL;
        return NULL;
    }
}


static int set_variables(w3c_client_t *c, int reqno, mrp_json_t *set,
                         mrp_json_t *req)
{
    mrp_transport_t *t = c->conn->t;
    mrp_json_iter_t  it;
    const char      *var;
    mrp_json_t      *val;

    if (set == NULL)
        return 0;

    mrp_json_foreach_member(set, var, val, it) {
        mrp_debug("processing attribute '%s'...", var);

        if (!strcmp(var, "name")) {
            if (c->name != NULL)
                return reply_error(t, reqno, EBUSY, "already set", req,
                                   "name already set");

            c->name = mrp_strdup(mrp_json_string_value(val));
            continue;
        }

        if (!strcmp(var, "appclass")) {
            if (c->appclass != NULL)
                return reply_error(t, reqno, EBUSY, "already set", req,
                                   "appclass already set");

            c->appclass = mrp_strdup(mrp_json_string_value(val));
            continue;
        }

        if (!strcmp(var, "lang")) {
            if (c->lang != NULL)
                return reply_error(t, reqno, EBUSY, "already set", req,
                                   "lang already set");

            c->lang = mrp_strdup(mrp_json_string_value(val));
            continue;
        }

        if (!strcmp(var, "grammars")) {
            int         len, i;
            mrp_json_t *grm;

            if (mrp_json_get_type(val) != MRP_JSON_ARRAY) {
            invalid_grammars:
                return reply_error(t, reqno, EINVAL, "invalid grammars",
                                   req, "invalid grammars given");
            }

            if (c->grammars != NULL)
                return reply_error(t, reqno, EBUSY, "already set", req,
                                   "grammars already set");

            len = mrp_json_array_length(val);
            c->grammars = mrp_allocz_array(char *, len);
            c->ngrammar = len;

            for (i = 0; i < len; i++) {
                if (!mrp_json_array_get_item(val, i, MRP_JSON_OBJECT, &grm))
                    goto invalid_grammars;

                if (!mrp_json_get_string(grm, "src", c->grammars + i))
                    goto invalid_grammars;
                else
                    c->grammars[i] = mrp_strdup(c->grammars[i]);
            }

            continue;
        }

        if (!strcmp(var, "events")) {
            w3c_event_t  events = W3C_EVENT_NONE;
            int          len, i;
            const char  *e;

            if (!mrp_json_get_type(val) != MRP_JSON_ARRAY) {
            invalid_events:
                return reply_error(t, reqno, EINVAL, "invalid events", req,
                                   "invalid events given");
            }

            len = mrp_json_array_length(val);
            for (i = 0; i < len; i++) {
                if (!mrp_json_array_get_string(val, i, &e))
                    goto invalid_events;

#define IS_EVENT(_e, _n) (!strcmp(_e, _n) || !strcmp(_e, "on"_n))
                if (IS_EVENT(e, "start"))
                    events |= W3C_EVENT_START;
                else if (IS_EVENT(e, "end"))
                    events |= W3C_EVENT_END;
                else if (IS_EVENT(e, "result"))
                    events |= W3C_EVENT_RESULT;
                else if (IS_EVENT(e, "nomatch"))
                    events |= W3C_EVENT_NOMATCH;
                else if (IS_EVENT(e, "error"))
                    events |= W3C_EVENT_ERROR;
                else if (IS_EVENT(e, "audiostart"))
                    events |= W3C_EVENT_AUDIOSTART;
                else if (IS_EVENT(e, "audioend"))
                    events |= W3C_EVENT_AUDIOEND;
                else if (IS_EVENT(e, "soundstart"))
                    events |= W3C_EVENT_SOUNDSTART;
                else if (IS_EVENT(e, "soundend"))
                    events |= W3C_EVENT_SOUNDEND;
                else if (IS_EVENT(e, "speechstart"))
                    events |= W3C_EVENT_SPEECHSTART;
                else if (IS_EVENT(e, "speechend"))
                    events |= W3C_EVENT_SPEECHEND;
                else
                    goto invalid_events;
#undef IS_EVENT
            }

            continue;
        }

        if (!strcmp(var, "continuous")) {
            c->continuous = mrp_json_boolean_value(val);
            continue;
        }

        if (!strcmp(var, "interimResults")) {
            c->interim = mrp_json_boolean_value(val);
            continue;
        }

        if (!strcmp(var, "maxAlternatives")) {
            c->max_alt = mrp_json_integer_value(val);
            continue;
        }

        if (!strcmp(var, "serviceURI")) {
            if (c->service != NULL)
                return reply_error(t, reqno, EBUSY, "already set", req,
                                   "serviceURI already set");

            c->service = mrp_strdup(mrp_json_string_value(val));
            continue;
        }

        mrp_log_warning("Ignoring unsupported attribute '%s'...", var);
    }

    return 0;
}


static int create_recognizer(w3c_connection_t *conn, int reqno, mrp_json_t *req)
{
    w3c_client_t *c;

    MRP_UNUSED(req);

    if ((c = mrp_allocz(sizeof(*c))) != NULL) {
        mrp_list_init(&c->hook);
        c->conn = conn;
        c->id   = conn->s->next_id++;

        if (set_variables(c, reqno, mrp_json_get(req, "set"), req) < 0)
            return -1;

        mrp_list_append(&conn->clients, &c->hook);

        return reply_status(conn->t, reqno, 0,
                            "id", MRP_JSON_INTEGER, c->id,
                            NULL);
    }
    else
        return -1;
}


static int delete_recognizer(w3c_connection_t *conn, int reqno, mrp_json_t *req)
{
    w3c_client_t *c = find_client_by_req(conn, req);

    if (c == NULL)
        return reply_error(conn->t, reqno, ENOENT, W3C_NOTFOUND, req,
                           "object to delete not found");

    mrp_list_delete(&c->hook);

    client_destroy(c->c);

    mrp_free(c->name);
    mrp_free(c->appclass);
    mrp_free(c->lang);
    mrp_free(c->service);
    mrp_free(c);

    return reply_status(conn->t, reqno, 0, NULL);
}


static int set_recognizer(w3c_connection_t *conn, int reqno, mrp_json_t *req)
{
    w3c_client_t *c = find_client_by_req(conn, req);
    mrp_json_t   *set;

    if (c == NULL)
        return reply_error(conn->t, reqno, ENOENT, W3C_NOTFOUND, req,
                           "no object to set attribute for");

    if (!mrp_json_get_object(req, "set", &set))
        return reply_error(conn->t, reqno, EINVAL, W3C_MALFORMED, req,
                           "no variables given to set");

    if (set_variables(c, reqno, set, req) < 0)
        return -1;
    else
        return reply_status(conn->t, reqno, 0, NULL);
}


static FILE *open_grammar(w3c_server_t *s, const char *uri)
{
    char  path[PATH_MAX];

    if (strncmp(uri, W3C_URI, sizeof(W3C_URI) - 1)) {
        mrp_log_error("W3C: cannot get grammar for URI '%s'.", uri);
        return NULL;
    }
    else
        uri += sizeof(W3C_URI) - 1;

    snprintf(path, sizeof(path), "%s/%s", s->grammar_dir, uri);

    return fopen(path, "r");
}


static char **read_grammars(w3c_server_t *s, int nuri, char **uris, int *ncmdp)
{
    char **commands, line[4096], *cmd, *e;
    int    ncommand, i;
    FILE  *fp;

    commands = NULL;
    ncommand = 0;
    line[0]  = '\0';

    for (i = 0; i < nuri; i++) {
        fp = open_grammar(s, uris[i]);

        if (fp == NULL)
            goto fail;

        while (fgets(line, sizeof(line), fp) != NULL) {
            e   = line + strlen(line) - 1;
            while (e >= line && (*e == '\n' || *e == ' ' || *e == '\t'))
                *e-- = '\0';
            cmd = line;
            while (*cmd == ' ' || *cmd == '\t')
                cmd++;

            if (!mrp_reallocz(commands, ncommand, ncommand + 1))
                goto fail;

            if ((commands[ncommand] = mrp_strdup(cmd)) == NULL)
                goto fail;

            mrp_log_info("W3C: %s/#%d command: '%s'", uris[i], i, cmd);

            ncommand++;
        }

        fclose(fp);
    }

    *ncmdp = ncommand;
    return commands;

 fail:
    for (i = 0; i < ncommand; i++)
        mrp_free(commands[i]);
    mrp_free(commands);

    *ncmdp = 0;
    return NULL;
}


static void free_grammars(int ncommand, char **commands)
{
    int i;

    for (i = 0; i < ncommand; i++)
        mrp_free(commands[i]);

    mrp_free(commands);
}


static int start_recognizer(w3c_connection_t *conn, int reqno, mrp_json_t *req)
{
    static srs_client_ops_t ops = {
        .notify_focus   = w3c_focus_notify,
        .notify_command = w3c_command_notify,
        .notify_render  = w3c_voice_notify,
    };

    w3c_client_t *c = find_client_by_req(conn, req);

    if (c == NULL)
        return reply_error(conn->t, reqno, ENOENT, W3C_NOTFOUND, req,
                           "recognizer not found");

    if (c->req == W3C_RECOGNIZER_START)
        return reply_error(conn->t, reqno, EBUSY, "already started", req,
                           "recognizer already started");

    if (c->c == NULL) {
        const char  *name     = c->name ? c->name : "W3C-client";
        const char  *appclass = c->appclass ? c->appclass : "player";
        char       **commands;
        int          ncommand;
        char         cid[256];

        if (c->grammars == NULL)
            return reply_error(conn->t, reqno, EINVAL, W3C_BADGRAMMAR, req,
                               "no grammars set");

        commands = read_grammars(conn->s, c->ngrammar, c->grammars, &ncommand);

        if (commands == NULL)
            return reply_error(conn->t, reqno, EINVAL, W3C_BADGRAMMAR, req,
                               "no valid grammars found");

        snprintf(cid, sizeof(cid), "W3C-client#%d", c->id);

        c->c = client_create(conn->s->self->srs, SRS_CLIENT_TYPE_EXTERNAL,
                             name, appclass, commands, ncommand, cid, &ops, c);

        free_grammars(ncommand, commands);

        if (c->c == NULL)
            return reply_error(conn->t, reqno, EINVAL, W3C_FAILED, req,
                               "failed to create recognition backend client");
    }

    if (client_request_focus(c->c, SRS_VOICE_FOCUS_SHARED)) {
        c->req = W3C_RECOGNIZER_START;
        return reply_status(conn->t, reqno, 0, NULL);
    }
    else
        return reply_error(conn->t, reqno, EINVAL, W3C_FAILED, req,
                           "recognition focus request failed");

    return 0;
}


static int stop_recognizer(w3c_connection_t *conn, int reqno, mrp_json_t *req)
{
    w3c_client_t *c = find_client_by_req(conn, req);
    const char   *error;

    if (c == NULL) {
        error = (errno == ENOENT ? W3C_NOTFOUND : W3C_MALFORMED);
        return reply_error(conn->t, reqno, errno, error, NULL, NULL);
    }

    if (c->c == NULL)
        return reply_error(conn->t, reqno, EINVAL, "already stopped",
                           NULL, NULL);

    if (client_request_focus(c->c, SRS_VOICE_FOCUS_NONE)) {
        c->req = W3C_RECOGNIZER_STOP;
        return reply_status(conn->t, reqno, 0, NULL);
    }
    else
        return reply_error(conn->t, reqno, EINVAL, W3C_FAILED, NULL, NULL);
}


static int abort_recognizer(w3c_connection_t *conn, int reqno, mrp_json_t *req)
{
    return stop_recognizer(conn, reqno, req);
}


static void connection_evt(mrp_transport_t *lt, void *user_data)
{
    w3c_server_t     *s = (w3c_server_t *)user_data;
    w3c_connection_t *conn;

    if ((conn = mrp_allocz(sizeof(*conn))) != NULL) {
        mrp_list_init(&conn->hook);
        mrp_list_init(&conn->clients);

        conn->s = s;
        conn->t = mrp_transport_accept(lt, conn, MRP_TRANSPORT_REUSEADDR);

        if (conn->t != NULL) {
            mrp_list_append(&s->connections, &conn->hook);
            mrp_log_info("Accepted W3C Speech connection.");

            return;
        }

        mrp_log_error("Failed to accept W3C Speech connection.");
        mrp_free(conn);
    }
    else {
        mrp_log_error("Failed to allocate W3C Speech connection.");
        mrp_transport_destroy(mrp_transport_accept(lt, NULL, 0));
    }
}


static void closed_evt(mrp_transport_t *t, int error, void *user_data)
{
    w3c_connection_t *conn = (w3c_connection_t *)user_data;

    MRP_UNUSED(t);

    if (error != 0)
        mrp_log_error("W3C Speech connection closed with error %d (%s).",
                      error, strerror(error));
    else
        mrp_log_info("W3C Speech connection closed.");

    mrp_list_delete(&conn->hook);

    mrp_transport_destroy(conn->t);
    mrp_free(conn);
}


static inline void dump_request(mrp_json_t *req)
{
    const char *str;

    str = mrp_json_object_to_string(req);

    mrp_log_info("received W3C Speech request:");
    mrp_log_info("  %s", str);
}


static void recv_evt(mrp_transport_t *t, mrp_json_t *req, void *user_data)
{
    w3c_connection_t *conn = (w3c_connection_t *)user_data;
    int               reqno;
    const char       *type, *object;

    MRP_UNUSED(conn);

    dump_request(req);

    if (!mrp_json_get_integer(req, "reqno", &reqno) ||
        !mrp_json_get_string (req, "type" , &type)) {
        reply_error(t, reqno, EINVAL, W3C_MALFORMED, req, NULL);
        return;
    }

    if (!strcmp(type, "create")) {
        if (mrp_json_get_string(req, "object", &object)) {
            if (!strcmp(object, "recognizer"))
                create_recognizer(conn, reqno, req);
            else
                reply_error(t, reqno, EINVAL, W3C_MALFORMED, req,
                            "unknown object type requested");
        }
        else
            reply_error(t, reqno, EINVAL, W3C_MALFORMED, req,
                        "no object type specified");
    }
    else if (!strcmp(type, "delete"))
        delete_recognizer(conn, reqno, req);
    else if (!strcmp(type, "set"))
        set_recognizer(conn, reqno, req);
    else if (!strcmp(type, "start"))
        start_recognizer(conn, reqno, req);
    else if (!strcmp(type, "stop"))
        stop_recognizer(conn, reqno, req);
    else if (!strcmp(type, "abort"))
        abort_recognizer(conn, reqno, req);
    else if (!strcmp(type, "timestamp"))
        reply_timestamp(conn->t, reqno, req);
    else
        reply_error(t, reqno, EINVAL, W3C_MALFORMED, req,
                    "request of unknown type");
}


static int transport_create(w3c_server_t *s)
{
    static mrp_transport_evt_t evt = {
        { .recvjson     = recv_evt },
        { .recvjsonfrom = NULL     },
        .connection     = connection_evt,
        .closed         = closed_evt,
    };

    srs_context_t  *srs = s->self->srs;
    mrp_sockaddr_t  addr;
    socklen_t       alen;
    const char     *type;
    int             flags, state;

    flags = MRP_TRANSPORT_NONBLOCK | MRP_TRANSPORT_MODE_JSON | \
        MRP_TRANSPORT_REUSEADDR;

    alen = mrp_transport_resolve(NULL, s->address, &addr, sizeof(addr), &type);

    if (alen <= 0) {
        mrp_log_error("Failed to resolve W3C Speech transport '%s'.",
                      s->address);
        goto fail;
    }

    if (s->sock < 0)
        s->lt = mrp_transport_create(srs->ml, type, &evt, s, flags);
    else {
        state = MRP_TRANSPORT_LISTENED;
        s->lt = mrp_transport_create_from(srs->ml, type, &s->sock, &evt,
                                          s, flags, state);
    }

    if (s->lt == NULL)
        goto fail;

    if (s->sock < 0) {
        if (!mrp_transport_bind(s->lt, &addr, alen) ||
            !mrp_transport_listen(s->lt, 0)) {
            mrp_log_error("Failed to bind/listen on W3C Speech transport '%s'.",
                          s->address);
            goto fail;
        }
        else
            mrp_log_info("Listening on W3C Speech API transport '%s'...",
                         s->address);
    }
    else
        mrp_log_info("Using socket %d for W3C Speech API transport...", s->sock);

    return 0;

 fail:
    if (s->lt != NULL) {
        mrp_transport_destroy(s->lt);
        s->lt = NULL;
    }

    return -1;
}


static void transport_destroy(w3c_server_t *s)
{
    mrp_transport_destroy(s->lt);
    s->lt = NULL;
}


static int w3c_create(srs_plugin_t *plugin)
{
    w3c_server_t *s;

    mrp_debug("creating W3C Speech API plugin");

    s = mrp_allocz(sizeof(*s));

    if (s == NULL)
        return FALSE;

    mrp_list_init(&s->connections);
    s->self = plugin;

    plugin->plugin_data = s;

    return TRUE;
}


static int w3c_config(srs_plugin_t *plugin, srs_cfg_t *cfg)
{
    w3c_server_t *s = (w3c_server_t *)plugin->plugin_data;

    mrp_debug("configuring W3C Speech API plugin");

    s->address = srs_config_get_string(cfg, CONFIG_ADDRESS, DEFAULT_ADDRESS);
    s->sock    = srs_config_get_int32(cfg, CONFIG_SOCKET, DEFAULT_SOCKET);

    if (s->sock < 0)
        mrp_log_info("Using W3C Speech API transport '%s'.", s->address);
    else
        mrp_log_info("Using W3C Speech API socket %d.", s->sock);

    s->grammar_dir = srs_config_get_string(cfg, CONFIG_GRAMMARDIR,
                                           DEFAULT_GRAMMARDIR);

    mrp_log_info("Using W3C grammar files from '%s'.", s->grammar_dir);

    return TRUE;
}


static int w3c_start(srs_plugin_t *plugin)
{
    w3c_server_t *s = (w3c_server_t *)plugin->plugin_data;

    if (transport_create(s) < 0)
        return FALSE;
    else
        return TRUE;
}


static void w3c_stop(srs_plugin_t *plugin)
{
    MRP_UNUSED(plugin);
}


static void w3c_destroy(srs_plugin_t *plugin)
{
    w3c_server_t *s = (w3c_server_t *)plugin->plugin_data;

    transport_destroy(s);
    mrp_free(s);
}


SRS_DECLARE_PLUGIN(W3C_PLUGIN, W3C_DESCR, W3C_AUTHORS, W3C_VERSION,
                   w3c_create, w3c_config, w3c_start, w3c_stop, w3c_destroy);
