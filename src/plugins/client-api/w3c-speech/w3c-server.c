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

#define W3C_TYPE_SYNTHESIZER     0x0
#define W3C_TYPE_RECOGNIZER      0x1
#define W3C_TYPE_UTTERANCE       0x2
#define W3C_OBJECT_ID(type, cnt) ((cnt << 2) | (type))
#define W3C_OBJECT_TYPE(id)      ((id) & 0x3)


typedef struct w3c_client_s w3c_client_t;
typedef struct w3c_synthesizer_s w3c_synthesizer_t;
typedef struct w3c_attrdef_s w3c_attrdef_t;


/*
 * W3C server (plugin runtime context)
 */

typedef struct {
    srs_plugin_t    *self;               /* our plugin instance */
    const char      *address;            /* transport address to listen on */
    int              sock;               /* or existing socket for transport */
    const char      *grammar_dir;        /* grammar directory */
    mrp_transport_t *lt;                 /* transport we listen on */
    mrp_list_hook_t  clients;            /* connected clients */
    int              next_id;            /* next client id */
} w3c_server_t;


/*
 * W3C client events
 */

typedef enum {
    W3C_EVENT_NONE        = 0x0000,
    W3C_EVENT_START       = 0x0001,
    W3C_EVENT_END         = 0x0002,
    W3C_EVENT_RESULT      = 0x0004,
    W3C_EVENT_NOMATCH     = 0x0008,
    W3C_EVENT_ERROR       = 0x0010,
    W3C_EVENT_AUDIOSTART  = 0x0020,
    W3C_EVENT_AUDIOEND    = 0x0040,
    W3C_EVENT_SOUNDSTART  = 0x0080,
    W3C_EVENT_SOUNDEND    = 0x0100,
    W3C_EVENT_SPEECHSTART = 0x0200,
    W3C_EVENT_SPEECHEND   = 0x0400,
    W3C_EVENT_PAUSE       = 0x0800,
    W3C_EVENT_RESUME      = 0x1000,
    W3C_EVENT_MARK        = 0x2000,
    W3C_EVENT_BOUNDARY    = 0x4000,
} w3c_event_t;


/*
 * W3C client requests
 */

typedef enum {
    W3C_REQUEST_NONE  = 0,
    W3C_REQUEST_START,
    W3C_REQUEST_STOP,
    W3C_REQUEST_ABORT,
    W3C_REQUEST_PAUSE,
    W3C_REQUEST_CANCEL,
    W3C_REQUEST_RESUME,
} w3c_request_t;


/*
 * W3C backend state
 */

typedef enum {
    W3C_BACKEND_STOPPED = 0,
    W3C_BACKEND_STARTED,
    W3C_BACKEND_RENDERING
} w3c_backend_t;


/*
 * a W3C client
 */

struct w3c_client_s {
    mrp_list_hook_t    hook;             /* to list of clients */
    int                id;               /* client id */
    w3c_server_t      *s;                /* back pointer to W3C server/plugin */
    mrp_transport_t   *t;                /* transport to this client */
    w3c_synthesizer_t *syn;              /* singleton per-client synthesizer */
    mrp_list_hook_t    recognizers;      /* recognizer instances */
    int                next_id;          /* next recognizer/utterance id */
};


/*
 * a W3C recognizer instance
 */

typedef struct {
    char      *name;                     /* backend client name */
    char      *appclass;                 /* backend client application class */
    uint32_t   events;                   /* mask of events of interest */
    char     **grammars;                 /* recognizer grammars */
    int        ngrammar;                 /* number of grammars */
    char      *lang;                     /* recognizer language */
    bool       continuous;               /* whether in continuous mode */
    bool       interim;                  /* whether to deliver interim results */
    int        max_alt;                  /* max. alternatives to deliver */
    char      *service;                  /* recognizer service URI */
    bool       shared;                   /* whether use shared focus */
    char     **commands;                 /* commands from grammars */
    int        ncommand;                 /* number of commands */
} w3c_rec_attr_t;


typedef struct {
    mrp_list_hook_t  hook;               /* to list of recognizers */
    w3c_client_t    *c;                  /* client we belong to  */
    int              id;                 /* recognizer object id */
    w3c_rec_attr_t   attr;               /* recognizer attributes */
    uint32_t         mask;               /* attribute mask */
    srs_client_t    *srsc;               /* associated backend client */
    int              request;            /* W3C client request */
    int              backend;            /* W3C backend state */
} w3c_recognizer_t;


/*
 * a W3C synthesizer instance (per-client singleton)
 */

typedef struct {
    char *name;                          /* backend client name */
    char *appclass;                      /* backend client application class */
} w3c_syn_attr_t;


struct w3c_synthesizer_s {
    w3c_client_t    *c;                  /* client we belong to */
    w3c_syn_attr_t   attr;               /* synthesizer attributes */
    srs_client_t    *srsc;               /* associated backend client */
    mrp_list_hook_t  utterances;         /* existing utterances */
    mrp_list_hook_t  pending;            /* pending utterances */
    bool             paused;             /* whether paused */
};


/*
 * a W3C utterance instance
 */

typedef struct {
    char     *text;                      /* text to synthesize */
    char     *lang;                      /* language to use */
    char     *voice;                     /* voice to use for synthesis */
    double    volume;                    /* volume to use for synthesis */
    double    rate;                      /* rate to use for synthesis */
    double    pitch;                     /* pitch to use for synthesis */
    uint32_t  events;                    /* events to deliver to client */
    int       timeout;                   /* rendering timeout */
} w3c_utt_attr_t;


typedef struct {
    mrp_list_hook_t    hook;             /* to utterance list */
    mrp_list_hook_t    pending;          /* to pending list */
    w3c_synthesizer_t *syn;              /* associated synthesizer */
    int                id;               /* (W3C) utterance object id */
    w3c_utt_attr_t     attr;             /* utterance attributes */
    uint32_t           vid;              /* backend id */
} w3c_utterance_t;


/*
 * an attribute definition
 */

typedef int (*w3c_attr_parser_t)(mrp_json_t *attr, void *obj, size_t base,
                                 w3c_attrdef_t *def, int *errc,
                                 const char **errs);
typedef int (*w3c_attr_check_t)(void *obj, w3c_attrdef_t *def, void *value,
                                int *errc, const char **errs);

struct w3c_attrdef_s {
    const char        *name;             /* attribute name   */
    int                type;             /* attribute type   */
    size_t             offs;             /* attribute offset */
    int                mask;             /* change mask bit  */
    w3c_attr_parser_t  parser;           /* attribute parser */
    w3c_attr_check_t   check;            /* attribute checker */
};

typedef enum {
    W3C_ATTR_NAME       = (0x1 <<  0),
    W3C_ATTR_APPCLASS   = (0x1 <<  1),
    W3C_ATTR_EVENTS     = (0x1 <<  2),
    W3C_ATTR_GRAMMARS   = (0x1 <<  3),
    W3C_ATTR_LANG       = (0x1 <<  4),
    W3C_ATTR_CONTINUOUS = (0x1 <<  5),
    W3C_ATTR_INTERIM    = (0x1 <<  6),
    W3C_ATTR_MAXALT     = (0x1 <<  7),
    W3C_ATTR_SERVICE    = (0x1 <<  8),
    W3C_ATTR_TEXT       = (0x1 <<  9),
    W3C_ATTR_VOICE      = (0x1 << 10),
    W3C_ATTR_VOLUME     = (0x1 << 11),
    W3C_ATTR_RATE       = (0x1 << 12),
    W3C_ATTR_PITCH      = (0x1 << 13),
    W3C_ATTR_SHARED     = (0x1 << 14),
    W3C_ATTR_TIMEOUT    = (0x1 << 15),
} w3c_attrmask_t;


/*
 * a client request handler
 */

typedef int (*request_handler_t)(w3c_client_t *c, int reqno, mrp_json_t *req);


static int create_synthesizer(w3c_client_t *c);
static void destroy_synthesizer(w3c_synthesizer_t *syn);
static void destroy_recognizer(w3c_recognizer_t *rec);
static w3c_utterance_t *lookup_utterance(w3c_client_t *c, int id, uint32_t vid);
static void destroy_utterance(w3c_utterance_t *utt);


static w3c_client_t *w3c_client_create(w3c_server_t *s)
{
    w3c_client_t *c;
    int           flags;

    if ((c = mrp_allocz(sizeof(*c))) == NULL) {
        mrp_transport_destroy(mrp_transport_accept(s->lt, NULL, 0));
        return NULL;
    }

    mrp_list_init(&c->hook);
    mrp_list_init(&c->recognizers);

    if (create_synthesizer(c) < 0) {
        mrp_transport_destroy(mrp_transport_accept(s->lt, NULL, 0));
        mrp_free(c);
        return NULL;
    }

    flags = MRP_TRANSPORT_REUSEADDR;

    if ((c->t = mrp_transport_accept(s->lt, c, flags)) == NULL) {
        mrp_free(c);
        return NULL;
    }

    c->id      = s->next_id++;
    c->s       = s;
    c->next_id = 1;

    mrp_list_append(&s->clients, &c->hook);

    mrp_log_info("Created W3C client #%d.", c->id);

    return c;
}


static void w3c_client_destroy(w3c_client_t *c)
{
    mrp_list_hook_t  *p, *n;
    w3c_recognizer_t *rec;

    mrp_log_info("Destroying W3C client #%d...", c->id);

    mrp_list_foreach(&c->recognizers, p, n) {
        rec = mrp_list_entry(p, typeof(*rec), hook);
        destroy_recognizer(rec);
    }

    destroy_synthesizer(c->syn);

    mrp_list_delete(&c->hook);
    mrp_transport_destroy(c->t);
    mrp_free(c);
}


static void connection_evt(mrp_transport_t *lt, void *user_data)
{
    w3c_server_t *s = (w3c_server_t *)user_data;
    w3c_client_t *c;

    MRP_UNUSED(lt);

    if ((c = w3c_client_create(s)) == NULL)
        mrp_log_error("Failed to create new W3C client.");
    else
        mrp_log_info("Accepted connection from W3C client #%d.", c->id);
}


static void closed_evt(mrp_transport_t *t, int error, void *user_data)
{
    w3c_client_t *c = (w3c_client_t *)user_data;

    MRP_UNUSED(t);

    if (error != 0)
        mrp_log_error("W3C speech connection closed with error %d (%s).",
                      error, strerror(error));
    else
        mrp_log_info("W3C speech connection closed.");

    w3c_client_destroy(c);
}


static inline void dump_request(mrp_json_t *req)
{
    const char *str;

#if 1
    str = mrp_json_object_to_string(req);
#else
    str = json_object_to_json_string_ext(req, JSON_C_TO_STRING_PRETTY);
#endif

    mrp_log_info("received W3C speech request:");
    mrp_log_info("  %s", str);
}


static mrp_json_t *add_json_timestamp(mrp_json_t *msg)
{
    mrp_json_t     *ts;
    struct timeval  tv;

    if (gettimeofday(&tv, NULL) < 0)
        return NULL;

    if (msg != NULL)
        ts = mrp_json_add_member(msg, "timestamp", MRP_JSON_OBJECT);
    else
        msg = ts = mrp_json_create(MRP_JSON_OBJECT);

    if (ts == NULL)
        return NULL;

    mrp_json_add_integer(ts, "sec" , tv.tv_sec);
    mrp_json_add_integer(ts, "usec", tv.tv_usec);

    return msg;
}


static int _reply_status(mrp_transport_t *t, int reqno, int status, ...)
{
    mrp_json_t *rpl;
    va_list     ap;
    const char *key;
    int         type;

    if ((rpl = mrp_json_create(MRP_JSON_OBJECT)) == NULL)
        return -1;

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
            mrp_json_unref(rpl);
            errno  = EINVAL;

            va_end(ap);

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

    return status;
}

#define reply_status(...) _reply_status(__VA_ARGS__, NULL)


static int reply_error(mrp_transport_t *t, int reqno, int status,
                       const char *error, mrp_json_t *req, const char *fmt, ...)
{
    const char *request;
    char        msg[4096];
    va_list     ap;

    va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt ? fmt : "", ap);
    va_end(ap);

    if (req != NULL)
        request = "request";
    else
        request = NULL;

    if (reqno < 0)
        mrp_json_get_integer(req, "reqno", &reqno);

    return reply_status(t, reqno, status,
                        "error"  , MRP_JSON_STRING, error,
                        "message", MRP_JSON_STRING, msg  ,
                        request  , MRP_JSON_OBJECT, req   );
}


static int _send_event(mrp_transport_t *t, int id, const char *event, ...)
{
    mrp_json_t *evt;
    va_list     ap;
    const char *key;
    int         type, status;

    if ((evt = mrp_json_create(MRP_JSON_OBJECT)) != NULL) {
        mrp_json_add_integer(evt, "reqno", 0);
        mrp_json_add_string (evt, "type" , "event");
        mrp_json_add_integer(evt, "id"   , id);
        add_json_timestamp  (evt);
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

#define send_event(...) _send_event(__VA_ARGS__, NULL)


static int malformed_request(mrp_transport_t *t, mrp_json_t *req,
                             const char *fmt, ...)
{
    int     reqno;
    char    msg[4096];
    va_list ap;

    if (!mrp_json_get_integer(req, "reqno", &reqno))
        reqno = 0;

    va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);

    if (reqno > 0)
        return reply_error(t, reqno, EINVAL, W3C_MALFORMED, req, "%s", msg);
    else
        return send_event(t, 0, "error",
                          "errorCode", MRP_JSON_STRING, W3C_MALFORMED,
                          "message"  , MRP_JSON_STRING, msg);
}


static inline int update_speaking(w3c_synthesizer_t *syn, bool state)
{
    w3c_client_t *c = syn->c;

    return send_event(c->t, 0, "speaking", "state", MRP_JSON_BOOLEAN, state);
}


static inline int update_pending(w3c_synthesizer_t *syn, bool prev)
{
    w3c_client_t *c    = syn->c;
    bool          curr = !mrp_list_empty(&c->syn->pending);

    if (curr != prev)
        return send_event(c->t, 0, "pending", "state", MRP_JSON_BOOLEAN, curr);
    else
        return 0;
}


static inline int update_paused(w3c_synthesizer_t *syn, bool state)
{
    w3c_client_t *c = syn->c;

    return send_event(c->t, 0, "paused", "state", MRP_JSON_BOOLEAN, state);
}


static int w3c_focus_notify(srs_client_t *c, srs_voice_focus_t focus)
{
    w3c_recognizer_t *rec = (w3c_recognizer_t *)c->user_data;

    mrp_log_info("W3C-recognizer#%d has now %s focus", rec->id,
                 (focus == SRS_VOICE_FOCUS_NONE ? "no" :
                  (focus == SRS_VOICE_FOCUS_SHARED ? "shared" : "exclusive")));

    if (focus == SRS_VOICE_FOCUS_NONE) {
        switch (rec->request) {
        case W3C_REQUEST_START:
            send_event(rec->c->t, rec->id, "error",
                       "error"  , MRP_JSON_STRING, "aborted",
                       "message", MRP_JSON_STRING, "voice focus lost");
            break;
        case W3C_REQUEST_STOP:
        case W3C_REQUEST_ABORT:
            send_event(rec->c->t, rec->id, "stopped");
            break;
        default:
            break;
        }
        rec->backend = W3C_BACKEND_STOPPED;
    }
    else {
        switch (rec->request) {
        case W3C_REQUEST_START:
            send_event(rec->c->t, rec->id, "started");
            break;
        default:
            break;
        }
        rec->backend = W3C_BACKEND_STARTED;
    }

    return 0;
}


static char *concat_tokens(char *buf, size_t size, int ntoken, char **tokens)
{
    char *p;
    int   l, n, i;

    p = buf;
    l = size;

    for (i = 0; i < ntoken; i++) {
        n  = snprintf(p, l, "%s%s", i ? " " : "", tokens[i]);
        p += n;
        l -= n;

        if (l <= 0) {
            buf[size - 1] = '\0';
            break;
        }
    }

    return buf;
}


static int w3c_command_notify(srs_client_t *c, int idx, int ntoken,
                              char **tokens, uint32_t *start, uint32_t *end,
                              srs_audiobuf_t *audio)
{
    w3c_recognizer_t *rec = (w3c_recognizer_t *)c->user_data;
    mrp_json_t       *results, *r;
    char              text[16*1024];

    MRP_UNUSED(idx);
    MRP_UNUSED(start);
    MRP_UNUSED(end);
    MRP_UNUSED(audio);

    r = results = NULL;

    if ((results = mrp_json_create(MRP_JSON_ARRAY))  == NULL ||
        (r       = mrp_json_create(MRP_JSON_OBJECT)) == NULL) {
        mrp_json_unref(results);
        mrp_json_unref(r);

        return -1;
    }

    concat_tokens(text, sizeof(text), ntoken, tokens);

    mrp_json_add_double(r, "confidence", 0.89);
    mrp_json_add_string(r, "transcript", text);

    if (mrp_json_array_append(results, r)) {
        send_event(rec->c->t, rec->id, "result",
                   "final"  , MRP_JSON_BOOLEAN, true,
                   "length" , MRP_JSON_INTEGER, 1,
                   "results", MRP_JSON_OBJECT , results);

        return 0;
    }
    else {
        mrp_json_unref(results);
        mrp_json_unref(r);

        return -1;
    }
}


static int no_voice_notify(srs_client_t *c, srs_voice_event_t *event)
{
    MRP_UNUSED(c);
    MRP_UNUSED(event);

    return 0;
}


static int create_recognizer_client(w3c_recognizer_t *rec, int *errc,
                                    const char **errs)
{
    static srs_client_ops_t ops = {
        .notify_focus   = w3c_focus_notify,
        .notify_command = w3c_command_notify,
        .notify_render  = no_voice_notify
    };

    srs_context_t  *srs      = rec->c->s->self->srs;
    char          **commands = rec->attr.commands;
    int             ncommand = rec->attr.ncommand;
    const char     *name     = rec->attr.name;
    const char     *appclass = rec->attr.appclass;
    char            cid[256];

    if (rec->srsc != NULL)
        return 0;

    if (!commands) {
        *errc = EINVAL;
        *errs = W3C_BADGRAMMAR;
        return -1;
    }

    snprintf(cid, sizeof(cid), "W3C-client #%d.%d", rec->c->id, rec->id);

    if (name == NULL)
        name = cid;

    if (appclass == NULL)
        appclass = "player";

    rec->srsc = client_create(srs, SRS_CLIENT_TYPE_EXTERNAL, name, appclass,
                              commands, ncommand, cid, &ops, rec);

    if (rec->srsc == NULL) {
        *errc = EINVAL;
        *errs = W3C_FAILED;

        return -1;
    }

    rec->request = W3C_REQUEST_NONE;
    rec->backend = W3C_BACKEND_STOPPED;

    return 0;
}


static void destroy_recognizer_client(w3c_recognizer_t *rec)
{
    client_destroy(rec->srsc);

    mrp_log_info("Destroying recognizer #%d.%d...", rec->c->id, rec->id);

    rec->srsc    = NULL;
    rec->backend = W3C_BACKEND_STOPPED;
}


static int start_recognizer_client(w3c_recognizer_t *rec, int *errc,
                                   const char **errs)
{
    int focus;

    if (rec->attr.shared)
        focus = SRS_VOICE_FOCUS_SHARED;
    else
        focus = SRS_VOICE_FOCUS_EXCLUSIVE;

    if (client_request_focus(rec->srsc, focus))
        return 0;
    else {
        *errc = EINVAL;
        *errs = W3C_FAILED;

        return -1;
    }
}


static int stop_recognizer_client(w3c_recognizer_t *rec, int *errc,
                                  const char **errs)
{
    if (client_request_focus(rec->srsc, SRS_VOICE_FOCUS_NONE))
        return 0;
    else {
        *errc = EINVAL;
        *errs = W3C_FAILED;

        return -1;
    }
}


static int w3c_voice_notify(srs_client_t *c, srs_voice_event_t *e)
{
    w3c_synthesizer_t *syn  = (w3c_synthesizer_t *)c->user_data;
    w3c_utterance_t   *utt  = lookup_utterance(syn->c, -1, e->id);
    int                mask = 1 << e->type;

    switch (e->type) {
    case SRS_VOICE_EVENT_STARTED:
        if (utt->attr.events & W3C_EVENT_START)
            send_event(syn->c->t, utt->id, "start");
        break;
    case SRS_VOICE_EVENT_COMPLETED:
        if (utt->attr.events & W3C_EVENT_END)
            send_event(syn->c->t, utt->id, "end");
        break;
    case SRS_VOICE_EVENT_TIMEOUT:
        if (utt->attr.events & W3C_EVENT_ERROR)
            send_event(syn->c->t, utt->id, "error",
                       "error", MRP_JSON_STRING, "timeout while queued");
        break;
    case SRS_VOICE_EVENT_ABORTED:
        if (utt->attr.events & W3C_EVENT_ERROR)
            send_event(syn->c->t, utt->id, "error",
                       "error", MRP_JSON_STRING, "aborted");
        break;
    case SRS_VOICE_EVENT_PROGRESS:
        break;
    default:
        break;
    }

    if (mask & SRS_VOICE_MASK_STARTED) {
        update_speaking(utt->syn, true);
    }
    else if (mask & SRS_VOICE_MASK_DONE) {
        utt->vid = SRS_VOICE_INVALID;
        mrp_list_delete(&utt->pending);

        update_speaking(utt->syn, false);
        update_pending(utt->syn, true);
    }

    return 0;
}


static int create_synthesizer_client(w3c_synthesizer_t *syn, int *errc,
                                     const char **errs)
{
    static srs_client_ops_t ops = {
        .notify_focus   = NULL,
        .notify_command = NULL,
        .notify_render  = w3c_voice_notify
    };

    srs_context_t  *srs      = syn->c->s->self->srs;
    const char     *name     = syn->attr.name;
    const char     *appclass = syn->attr.appclass;
    char            cid[256];

    if (syn->srsc != NULL)
        return 0;

    snprintf(cid, sizeof(cid), "W3C-renderer #%d", syn->c->id);

    if (name == NULL)
        name = cid;

    if (appclass == NULL)
        appclass = "player";

    syn->srsc = client_create(srs, SRS_CLIENT_TYPE_EXTERNAL, name, appclass,
                              NULL, 0, cid, &ops, syn);

    if (syn->srsc == NULL) {
        *errc = EINVAL;
        *errs = W3C_FAILED;

        return -1;
    }

    return 0;
}


static void destroy_synthesizer_client(w3c_synthesizer_t *syn)
{
    client_destroy(syn->srsc);
    syn->srsc = NULL;
}


static int create_synthesizer(w3c_client_t *c)
{
    w3c_synthesizer_t *syn;

    if (c->syn != NULL)
        return 0;

    if ((syn = mrp_allocz(sizeof(*syn))) == 0)
        return -1;

    mrp_list_init(&syn->utterances);
    mrp_list_init(&syn->pending);

    syn->c = c;
    c->syn = syn;

    return 0;
}


static void destroy_synthesizer(w3c_synthesizer_t *syn)
{
    mrp_list_hook_t *p, *n;
    w3c_utterance_t *utt;

    if (syn == NULL)
        return;

    destroy_synthesizer_client(syn);

    syn->c->syn = NULL;

    mrp_list_foreach(&syn->utterances, p, n) {
        utt = mrp_list_entry(p, typeof(*utt), hook);
        destroy_utterance(utt);

    }

    mrp_free(syn);
}


static w3c_utterance_t *create_utterance(w3c_synthesizer_t *syn)
{
    w3c_utterance_t *utt;

    if ((utt = mrp_allocz(sizeof(*utt))) != NULL) {
        mrp_list_init(&utt->hook);
        mrp_list_init(&utt->pending);

        utt->syn = syn;
        utt->id  = W3C_OBJECT_ID(W3C_TYPE_UTTERANCE, syn->c->next_id++);
        utt->vid = SRS_VOICE_INVALID;

        utt->attr.volume  = 1.0;
        utt->attr.rate    = 1.0;
        utt->attr.pitch   = 1.0;
        utt->attr.timeout = -1;

        mrp_list_append(&syn->utterances, &utt->hook);
    }

    return utt;
}


static void destroy_utterance(w3c_utterance_t *utt)
{
    mrp_list_delete(&utt->hook);
    mrp_list_delete(&utt->pending);

    mrp_free(utt->attr.text);
    mrp_free(utt->attr.lang);
    mrp_free(utt->attr.voice);

    mrp_free(utt);
}


static w3c_utterance_t *lookup_utterance(w3c_client_t *c, int id, uint32_t vid)
{
    mrp_list_hook_t *p, *n;
    w3c_utterance_t *utt;

    mrp_list_foreach(&c->syn->utterances, p, n) {
        utt = mrp_list_entry(p, typeof(*utt), hook);

        if (id != -1 && utt->id == id)
            return utt;

        if (id == -1 && utt->vid == vid)
            return utt;
    }

    return NULL;
}


static int activate_utterance(w3c_utterance_t *utt)
{
    const char *msg, *voice;
    double      rate, pitch;
    int         timeout, events;

    if (utt->vid != SRS_VOICE_INVALID)
        return 0;

    msg     = utt->attr.text;
    voice   = utt->attr.voice ? utt->attr.voice : utt->attr.lang;
    rate    = utt->attr.rate;
    pitch   = utt->attr.pitch;
    timeout = utt->attr.timeout;

    events  = SRS_VOICE_MASK_ALL;

    if (!utt->syn->paused) {
        utt->vid = client_render_voice(utt->syn->srsc, msg, voice, rate, pitch,
                                       timeout, events);

        if (utt->vid == SRS_VOICE_INVALID) {
            errno = EINVAL;
            return -1;
        }
    }

    mrp_list_delete(&utt->pending);
    mrp_list_append(&utt->syn->pending, &utt->pending);

    return 0;
}


static int cancel_utterance(w3c_utterance_t *utt)
{
    client_cancel_voice(utt->syn->srsc, utt->vid);
    utt->vid = SRS_VOICE_INVALID;
    mrp_list_delete(&utt->pending);

    return 0;
}


static int pause_utterance(w3c_utterance_t *utt)
{
    /* XXX TODO: not right pause since will restart from the start. */
    client_cancel_voice(utt->syn->srsc, utt->vid);
    utt->vid = SRS_VOICE_INVALID;

    return 0;
}


static int resume_utterance(w3c_utterance_t *utt)
{
    /* XXX TODO: since we can't resume ATM, for now we restart from scratch */
    return activate_utterance(utt);
}


static int parse_string_attr(mrp_json_t *attr, void *obj, size_t base,
                             w3c_attrdef_t *def, int *errc, const char **errs)
{
    char       **valuep = (char **)(((char *)obj) + base + def->offs);
    const char  *value;
    int          status;

    value  = mrp_json_string_value(attr);
    status = def->check(obj, def, (void *)&value, errc, errs);

    if (status < 0)
        return -(errno = -status);

    mrp_free(*valuep);
    *valuep = mrp_strdup(value);
    mrp_debug("string attribute '%s' set to '%s'", def->name, value);

    return 0;
}


static int parse_boolean_attr(mrp_json_t *attr, void *obj, size_t base,
                              w3c_attrdef_t *def, int *errc, const char **errs)
{
    bool *valuep = (bool *)(((char *)obj) + base + def->offs);
    bool  value;
    int   status;

    value  = !!mrp_json_boolean_value(attr);
    status = def->check(obj, def, (void *)&value, errc, errs);

    if (status < 0)
        return -(errno = -status);

    *valuep = value;
    mrp_debug("boolean attribute '%s' set to %s", def->name,
              value ? "true" : "false");

    return 0;
}


static int parse_integer_attr(mrp_json_t *attr, void *obj, size_t base,
                              w3c_attrdef_t *def, int *errc, const char **errs)
{
    int *valuep = (int *)(((char *)obj) + base + def->offs);
    int  value, status;

    value  = (int)mrp_json_integer_value(attr);
    status = def->check(obj, def, (void *)&value, errc, errs);

    if (status < 0)
        return -(errno = -status);

    *valuep = value;
    mrp_debug("integer attribute '%s' set to %d", def->name, value);

    return 0;
}


static int parse_double_attr(mrp_json_t *attr, void *obj, size_t base,
                             w3c_attrdef_t *def, int *errc, const char **errs)
{
    double *valuep = (double *)(((char *)obj) + base + def->offs);
    double  value;
    int     status;

    value  = (double)mrp_json_double_value(attr);
    status = def->check(obj, def, (void *)&value, errc, errs);

    if (status < 0)
        return -(errno = -status);

    *valuep = value;
    mrp_debug("double attribute '%s' set to %f", def->name, value);

    return 0;
}


static int parse_events(mrp_json_t *attr, void *obj, size_t base,
                        w3c_attrdef_t *def, int *errc, const char **errs)
{
    static struct {
        const char *name;
        int         mask;
    } events[] = {
#define E(_n, _m) { _n, W3C_EVENT_##_m }
        E("start"       , START        ),
        E("end"         , END          ),
        E("result"      , RESULT       ),
        E("nomatch"     , NOMATCH      ),
        E("error"       , ERROR        ),
        E("audiostart"  , AUDIOSTART   ),
        E("audioend"    , AUDIOEND     ),
        E("soundstart"  , SOUNDSTART   ),
        E("soundend"    , SOUNDEND     ),
        E("speechstart" , SPEECHSTART  ),
        E("speechend"   , SPEECHEND    ),
        E("pause"       , PAUSE        ),
        E("resume"      , RESUME       ),
        E("mark"        , MARK         ),
        E("boundary"    , BOUNDARY     ),
#undef E
        { NULL, 0 }
    }, *e;

    const char *name;
    int        *valuep = (int *)(((char *)obj) + base + def->offs);
    int         mask, len, i, status;

    len  = mrp_json_array_length(attr);
    mask = W3C_EVENT_NONE;

    for (i = 0; i < len; i++) {
        if (!mrp_json_array_get_string(attr, i, &name)) {
            errno = EINVAL;
            *errc = EINVAL;
            *errs = W3C_BADEVENTS;
            return -1;
        }

        for (e = events; e->name; e++) {
            if (!strcmp(e->name, name)) {
                mask |= e->mask;
                break;
            }
        }

        if (e->name == NULL) {
            mrp_log_error("Unknown W3C event '%s' requested", name);
            errno = EINVAL;
            *errc = EINVAL;
            *errs = W3C_BADEVENT;
            return -1;
        }
    }

    status = def->check(obj, def, (void *)&mask, errc, errs);

    if (status < 0)
        return -(errno = -status);

    *valuep = mask;
    mrp_debug("events attribute '%s' set to 0x%x", def->name, *valuep);

    return 0;
}


static int parse_grammars(mrp_json_t *attr, void *obj, size_t base,
                          w3c_attrdef_t *def, int *errc, const char **errs)
{
    char       ***grammarsp = (char ***)(((char *)obj) + base + def->offs);
    int          *ngrammarp;
    char        **grammars;
    int           ngrammar, len, i, status;
    mrp_json_t   *grm;

    if (grammarsp != (char ***)(((char *)obj) +
                                MRP_OFFSET(w3c_recognizer_t, attr) +
                                MRP_OFFSET(w3c_rec_attr_t, grammars))) {
        *errc = EINVAL;
        *errs = W3C_SERVERERR;
        return -1;
    }

    ngrammarp = (int *)(((char *)obj) +
                        MRP_OFFSET(w3c_recognizer_t, attr) +
                        MRP_OFFSET(w3c_rec_attr_t, ngrammar));

    grammars = NULL;
    ngrammar = 0;
    len      = mrp_json_array_length(attr);

    for (i = 0; i < len; i++) {
        if (!mrp_reallocz(grammars, ngrammar, ngrammar + 1)) {
            *errc = ENOMEM;
            *errs = W3C_NOMEM;
            return -1;
        }

        if (!mrp_json_array_get_item(attr, i, MRP_JSON_OBJECT, &grm))
            goto invalid_grammars;

        if (!mrp_json_get_string(grm, "src", grammars + i))
            goto invalid_grammars;

        grammars[i] = mrp_strdup(grammars[i]);
        ngrammar++;
    }

    if (!mrp_reallocz(grammars, ngrammar, ngrammar + 1)) {
        *errc = ENOMEM;
        *errs = W3C_NOMEM;
        return -1;
    }

    status = def->check(obj, def, (void *)grammars, errc, errs);

    if (status < 0) {
        errno = -status;
        goto invalid_grammars;
    }

    for (i = 0; i < *ngrammarp; i++)
        mrp_free((*grammarsp)[i]);
    mrp_free(*grammarsp);

    *grammarsp = grammars;
    *ngrammarp = ngrammar;

    mrp_debug("grammar attribute '%s' set to:", def->name);
    for (i = 0; i < ngrammar; i++)
        mrp_debug("    #%d: '%s'", i, grammars[i]);

    return 0;

 invalid_grammars:
    for (i = 0; i < ngrammar; i++)
        mrp_free(grammars[i]);
    mrp_free(grammars);

    *errc = EINVAL;
    *errs = W3C_BADGRAMMAR;

    return -1;
}


static int w3c_set_attributes(mrp_json_t *set, void *obj, size_t base,
                              w3c_attrdef_t *defs, int *errc, const char **errs)
{
    w3c_attrdef_t *d;
    mrp_json_t    *v;
    int            i, mask, vtype;

    if (set == NULL)
        return 0;

    /*
     * Notes:
     *
     *   Currently unsupported attributes in set are silently ignored.
     *   Detecting those would require to replace the loop below with
     *   two nested loops, the outer of which loopinh through all key-
     *   value pairs in the set and the inner looking for the descriptor
     *   for key.
     */

    mask = 0;
    for (i = 0, d = defs; d->name != NULL; d++, i++) {
        v = mrp_json_get(set, d->name);

        if (v == NULL)
            continue;

        if ((vtype = (int)mrp_json_get_type(v)) != d->type) {
            if (!(vtype = MRP_JSON_INTEGER && d->type == MRP_JSON_DOUBLE)) {
                *errc = EINVAL;
                *errs = W3C_MALFORMED;
                return -(i + 1);
            }
        }

        if (d->parser(v, obj, base, d, errc, errs) < 0)
            return -(i + 1);

        mask |= d->mask;
    }

    return mask;
}


static w3c_recognizer_t *create_recognizer(w3c_client_t *c)
{
    w3c_recognizer_t *rec;

    if ((rec = mrp_allocz(sizeof(*rec))) != NULL) {
        mrp_list_init(&rec->hook);
        mrp_list_append(&c->recognizers, &rec->hook);

        rec->c  = c;
        rec->id = W3C_OBJECT_ID(W3C_TYPE_RECOGNIZER, c->next_id++);
    }

    mrp_log_info("Created W3C recognizer #%d.%d.", rec->c->id, rec->id);

    return rec;
}


static void destroy_recognizer(w3c_recognizer_t *rec)
{
    int i;

    if (rec == NULL)
        return;

    mrp_log_info("Destroying W3C recognizer #%d.%d.", rec->c->id, rec->id);

    mrp_list_delete(&rec->hook);

    destroy_recognizer_client(rec);

    mrp_free(rec->attr.name);
    mrp_free(rec->attr.appclass);
    mrp_free(rec->attr.lang);
    mrp_free(rec->attr.service);

    for (i = 0; i < rec->attr.ngrammar; i++)
        mrp_free(rec->attr.grammars[i]);
    mrp_free(rec->attr.grammars);


    mrp_free(rec);
}


static w3c_recognizer_t *lookup_recognizer(w3c_client_t *c, int id)
{
    mrp_list_hook_t  *p, *n;
    w3c_recognizer_t *rec;

    mrp_list_foreach(&c->recognizers, p, n) {
        rec = mrp_list_entry(p, typeof(*rec), hook);

        if (rec->id == id)
            return rec;
    }

    return NULL;
}



static int check_recognizer_attr(void *obj, w3c_attrdef_t *def, void *val,
                                 int *errc, const char **errs)
{
    w3c_recognizer_t *rec = (w3c_recognizer_t *)obj;

    MRP_UNUSED(val);

    switch (def->mask) {
    case W3C_ATTR_NAME:
    case W3C_ATTR_APPCLASS:
    case W3C_ATTR_GRAMMARS:
    case W3C_ATTR_LANG:
        if (rec->srsc == NULL)
            return 0;

        *errc = EBUSY;
        *errs = W3C_BUSY;
        return -EBUSY;

    case W3C_ATTR_EVENTS:     return 0;
    case W3C_ATTR_CONTINUOUS: return 0;
    case W3C_ATTR_INTERIM:    return 0;
    case W3C_ATTR_MAXALT:     return 0;
    case W3C_ATTR_SERVICE:    return 0;
    case W3C_ATTR_SHARED:     return 0;
    default:
        *errc = EINVAL;
        *errs = W3C_MALFORMED;
        return -EINVAL;
    }
}


static int check_utterance_attr(void *obj, w3c_attrdef_t *def, void *val,
                                int *errc, const char **errs)
{
    w3c_utterance_t *utt = (w3c_utterance_t *)obj;

    MRP_UNUSED(val);

    switch (def->mask) {
    case W3C_ATTR_TEXT:
    case W3C_ATTR_LANG:
    case W3C_ATTR_VOICE:
        if (utt->vid == SRS_VOICE_INVALID)
            return 0;

        *errc = EBUSY;
        *errs = W3C_BUSY;
        return -EBUSY;

    case W3C_ATTR_VOLUME:
    case W3C_ATTR_RATE:
    case W3C_ATTR_PITCH:
    case W3C_ATTR_TIMEOUT:
        if (utt->vid == SRS_VOICE_INVALID)
            return 0;

        *errc = EBUSY;
        *errs = W3C_BUSY;
        return -EBUSY;

    case W3C_ATTR_EVENTS:
        return 0;

    default:
        *errc = EINVAL;
        *errs = W3C_MALFORMED;
        return -EINVAL;
    }
}


static inline w3c_attrdef_t *recognizer_attributes(void)
{
#define DEFAULT NULL
#define BOOLEAN(_n, _an, _m, _p) \
    { _n, MRP_JSON_BOOLEAN, MRP_OFFSET(w3c_rec_attr_t, _an), W3C_ATTR_##_m, \
            _p ? _p : parse_boolean_attr, check_recognizer_attr }
#define STRING(_n, _an, _m, _p)  \
    { _n, MRP_JSON_STRING , MRP_OFFSET(w3c_rec_attr_t, _an), W3C_ATTR_##_m, \
            _p ? _p : parse_string_attr , check_recognizer_attr }
#define INTEGER(_n, _an, _m, _p) \
    { _n, MRP_JSON_INTEGER, MRP_OFFSET(w3c_rec_attr_t, _an), W3C_ATTR_##_m, \
            _p ? _p : parse_integer_attr, check_recognizer_attr }
#define ARRAY(_n, _an, _m, _p)   \
    { _n, MRP_JSON_ARRAY  , MRP_OFFSET(w3c_rec_attr_t, _an), W3C_ATTR_##_m, \
            _p, check_recognizer_attr }

    static w3c_attrdef_t def[] = {
        STRING ("name"           , name      , NAME      , DEFAULT       ),
        STRING ("appclass"       , appclass  , APPCLASS  , DEFAULT       ),
        ARRAY  ("events"         , events    , EVENTS    , parse_events  ),
        ARRAY  ("grammars"       , grammars  , GRAMMARS  , parse_grammars),
        STRING ("lang"           , lang      , LANG      , DEFAULT       ),
        BOOLEAN("continuous"     , continuous, CONTINUOUS, DEFAULT       ),
        BOOLEAN("interimResults" , interim   , INTERIM   , DEFAULT       ),
        INTEGER("maxAlternatives", max_alt   , MAXALT    , DEFAULT       ),
        STRING ("serviceURI"     , service   , SERVICE   , DEFAULT       ),
        BOOLEAN("shared"         , shared    , SHARED    , DEFAULT       ),
        { NULL, 0, 0, 0, NULL, NULL }
    };

#undef DEFAULT
#undef BOOLEAN
#undef STRING
#undef INTEGER
#undef ARRAY

    return def;
}


static inline w3c_attrdef_t *utterance_attributes(void)
{
#define DEFAULT NULL
#define BOOLEAN(_n, _an, _m, _p) \
    { _n, MRP_JSON_BOOLEAN, MRP_OFFSET(w3c_utt_attr_t, _an), W3C_ATTR_##_m, \
            _p ? _p : parse_boolean_attr, check_utterance_attr }
#define STRING(_n, _an, _m, _p)  \
    { _n, MRP_JSON_STRING , MRP_OFFSET(w3c_utt_attr_t, _an), W3C_ATTR_##_m, \
            _p ? _p : parse_string_attr , check_utterance_attr }
#define INTEGER(_n, _an, _m, _p) \
    { _n, MRP_JSON_INTEGER, MRP_OFFSET(w3c_utt_attr_t, _an), W3C_ATTR_##_m, \
            _p ? _p : parse_integer_attr, check_utterance_attr }
#define DOUBLE(_n, _an, _m, _p) \
    { _n, MRP_JSON_DOUBLE, MRP_OFFSET(w3c_utt_attr_t, _an), W3C_ATTR_##_m, \
            _p ? _p : parse_double_attr, check_utterance_attr }
#define ARRAY(_n, _an, _m, _p)   \
    { _n, MRP_JSON_ARRAY  , MRP_OFFSET(w3c_utt_attr_t, _an), W3C_ATTR_##_m, \
            _p, check_utterance_attr }

    static w3c_attrdef_t def[] = {
        STRING ("text"           , text      , TEXT      , DEFAULT       ),
        STRING ("lang"           , lang      , LANG      , DEFAULT       ),
        STRING ("voiceURI"       , voice     , VOICE     , DEFAULT       ),
        DOUBLE ("volume"         , volume    , VOLUME    , DEFAULT       ),
        DOUBLE ("rate"           , rate      , RATE      , DEFAULT       ),
        DOUBLE ("pitch"          , pitch     , PITCH     , DEFAULT       ),
        INTEGER("timeout"        , timeout   , TIMEOUT   , DEFAULT       ),
        ARRAY  ("events"         , events    , EVENTS    , parse_events  ),
        { NULL, 0, 0, 0, NULL, NULL }
    };

#undef DEFAULT
#undef BOOLEAN
#undef STRING
#undef INTEGER
#undef DOUBLE
#undef ARRAY

    return def;
}



static char *strip_whitespace(char *buf)
{
    char *e;

    if (buf == NULL)
        return NULL;

    while (*buf == ' ' || *buf == '\t')
        buf++;

    e = buf + strlen(buf) - 1;
    while (e >= buf && (*e == '\n' || *e == ' ' || *e == '\t'))
        *e-- = '\0';

    return buf;
}


static FILE *open_grammar(w3c_server_t *s, const char *URI)
{
    char  path[PATH_MAX];
    FILE *fp;

    if (strncmp(URI, W3C_URI, sizeof(W3C_URI) - 1)) {
        mrp_log_error("W3C: invalid grammar '%s'.", URI);
        errno = EINVAL;
        return NULL;
    }

    URI += sizeof(W3C_URI) - 1;
    snprintf(path, sizeof(path), "%s/%s", s->grammar_dir, URI);

    fp = fopen(path, "r");

    if (fp != NULL)
        mrp_debug("W3C: grammar '%s' -> '%s'", URI, path);

    return fp;
}


int read_grammars(w3c_recognizer_t *rec, const char **errs)
{
    char **cmds, *cmd, buf[4096];
    int    n, i;
    FILE  *fp;

    cmds = NULL;
    n    = 0;

    for (i = 0; i < rec->attr.ngrammar; i++) {
        fp = open_grammar(rec->c->s, rec->attr.grammars[i]);

        if (fp == NULL) {
            *errs = W3C_BADGRAMMAR;
            goto fail;
        }

        while (fgets(buf, sizeof(buf), fp) != NULL) {
            cmd = strip_whitespace(buf);

            if (!*cmd)
                continue;

            if (mrp_reallocz(cmds, n, n + 1) == NULL ||
                (cmds[n] = mrp_strdup(cmd))  == NULL) {
                *errs = W3C_NOMEM;
                goto fail;
            }

            mrp_debug("command #%d: '%s'", n, cmd);

            n++;
        }

        fclose(fp);
    }

    for (i = 0; i < rec->attr.ncommand; i++)
        mrp_free(rec->attr.commands[i]);
    mrp_free(rec->attr.commands);

    rec->attr.commands = cmds;
    rec->attr.ncommand = n;

    return 0;

 fail:
    for (i = 0; i < n; i++)
        mrp_free(cmds[i]);
    mrp_free(cmds);

    return -1;
}


static int check_id(w3c_client_t *c, mrp_json_t *req, int *idp)
{
    if (mrp_json_get_integer(req, "id", idp))
        return 0;
    else {
        malformed_request(c->t, req, "missing object ID");
        return -1;
    }
}


static w3c_recognizer_t *check_recognizer(w3c_client_t *c, mrp_json_t *req,
                                          int id)
{
    w3c_recognizer_t *rec;

    if (id < 0)
        if (check_id(c, req, &id) < 0)
            return NULL;

    rec = lookup_recognizer(c, id);

    if (rec != NULL)
        return rec;

    reply_error(c->t, -1, ENOENT, W3C_NOTFOUND, req,
                "recognizer object not found");
    return NULL;
}


static w3c_utterance_t *check_utterance(w3c_client_t *c, mrp_json_t *req, int id)
{
    w3c_utterance_t *utt;

    if (id < 0)
        if (check_id(c, req, &id) < 0)
            return NULL;

    utt = lookup_utterance(c, id, -1);

    if (utt != NULL)
        return utt;

    reply_error(c->t, -1, ENOENT, W3C_NOTFOUND, req,
                "utterance object not found");
    return NULL;
}




static int w3c_create_recognizer(w3c_client_t *c, int reqno, mrp_json_t *req)
{
    w3c_attrdef_t    *def = recognizer_attributes();
    w3c_recognizer_t *rec;
    mrp_json_t       *set;
    size_t            base;
    int               mask;
    int               errc;
    const char       *errs;

    if ((rec = create_recognizer(c)) == NULL) {
        return reply_error(c->t, reqno, ENOMEM, W3C_FAILED, req,
                           "failed to create new recognizer instance");
    }

    set = mrp_json_get(req, "set");

    if (set != NULL) {
        base = MRP_OFFSET(w3c_recognizer_t, attr);
        errc = 0;
        errs = NULL;
        mask = w3c_set_attributes(set, rec, base, def, &errc, &errs);
    }
    else
        mask = 0;

    if (mask < 0) {
        reply_error(c->t, reqno, errc, errs, req,
                    "failed to set recognizer attributes");
        destroy_recognizer(rec);
        return -1;
    }

    if (mask & W3C_ATTR_GRAMMARS) {
        if (read_grammars(rec, &errs) < 0) {
            errc = errno;
            reply_error(c->t, reqno, errc, errs, req,
                        "failed to locate/parse given grammars");
            destroy_recognizer(rec);
            return -1;
        }
    }

    return reply_status(c->t, reqno, 0, "id", MRP_JSON_INTEGER, rec->id);
}


static int w3c_delete_object(w3c_client_t *c, int reqno, mrp_json_t *req)
{
    int               id;
    w3c_recognizer_t *rec;
    w3c_utterance_t  *utt;

    if (check_id(c, req, &id) < 0)
        return -1;

    switch (W3C_OBJECT_TYPE(id)) {
    case W3C_TYPE_RECOGNIZER:
        rec = check_recognizer(c, req, id);

        if (rec != NULL)
            destroy_recognizer(rec);

        return reply_status(c->t, reqno, 0);

    case W3C_TYPE_UTTERANCE:
        utt = check_utterance(c, req, id);

        if (utt != NULL)
            destroy_utterance(utt);

        return reply_status(c->t, reqno, 0);

    default:
        break;
    }

    return -1;
}


static int w3c_set_attribute(w3c_client_t *c, int reqno, mrp_json_t *req)
{
    int               id;
    w3c_recognizer_t *rec;
    w3c_utterance_t  *utt;
    mrp_json_t       *set;
    w3c_attrdef_t    *defs;
    size_t            base;
    int               mask;
    int               errc;
    const char       *errs;

    if (check_id(c, req, &id) < 0)
        return -1;

    if ((set = mrp_json_get(req, "set")) == NULL)
        return malformed_request(c->t, req, "missing attributes");

    if (mrp_json_get_type(set) != MRP_JSON_OBJECT)
        return malformed_request(c->t, req, "invalid attributes");

    switch (W3C_OBJECT_TYPE(id)) {
    case W3C_TYPE_RECOGNIZER:
        if ((rec = check_recognizer(c, req, id)) == NULL)
            return 0;

        defs = recognizer_attributes();
        base = MRP_OFFSET(w3c_recognizer_t, attr);
        mask = w3c_set_attributes(set, rec, base, defs, &errc, &errs);

        if (mask < 0)
            return reply_error(c->t, reqno, errc, errs, req,
                               "failed to set attribute #%d", -(mask - 1));

        if (mask & W3C_ATTR_GRAMMARS) {
            if (read_grammars(rec, &errs) < 0) {
                errc = errno;
                return reply_error(c->t, reqno, errc, errs, req,
                                   "failed to locate/parse some given grammar");
            }
        }

        return reply_status(c->t, reqno, 0);

    case W3C_TYPE_UTTERANCE:
        if ((utt = check_utterance(c, req, id)) == NULL)
            return 0;

        defs = utterance_attributes();
        base = MRP_OFFSET(w3c_utterance_t, attr);
        mask = w3c_set_attributes(set, utt, base, defs, &errc, &errs);

        if (mask < 0)
            return reply_error(c->t, reqno, errc, errs, req,
                               "failed to set attribute #%d", -(mask - 1));
        else
            return reply_status(c->t, reqno, 0);

    default:
        break;
    }

    return -1;
}


static int w3c_get_timestamp(w3c_client_t *c, int reqno, mrp_json_t *req)
{
    mrp_log_info("%s(#%d: %s)", __FUNCTION__, reqno,
                 mrp_json_object_to_string(req));

    return reply_status(c->t, reqno, 0,
                        "timestamp", MRP_JSON_OBJECT, add_json_timestamp(NULL));
}


static int w3c_start_recognizer(w3c_client_t *c, int reqno, mrp_json_t *req)
{
    w3c_recognizer_t *rec;
    int               id, errc;
    const char       *errs;

    if (check_id(c, req, &id) < 0)
        return -1;

    if ((rec = check_recognizer(c, req, id)) == NULL)
        return -1;

    if (rec->srsc == NULL) {
        if (create_recognizer_client(rec, &errc, &errs) < 0) {
            reply_error(c->t, reqno, errc, errs, req,
                        "failed to create backend client");
            return -1;
        }
    }

    if (start_recognizer_client(rec, &errc, &errs) < 0) {
        reply_error(c->t, reqno, errc, errs, req,
                    "failed to start backend client");
        return -1;
    }

    rec->request = W3C_REQUEST_START;

    return reply_status(c->t, reqno, 0);
}


static int w3c_stop_recognizer(w3c_client_t *c, int reqno, mrp_json_t *req)
{
    w3c_recognizer_t *rec;
    int               id, errc;
    const char       *errs;

    if (check_id(c, req, &id) < 0)
        return -1;

    if ((rec = check_recognizer(c, req, id)) == NULL)
        return -1;

    if (stop_recognizer_client(rec, &errc, &errs) < 0) {
        reply_error(c->t, reqno, errc, errs, req,
                    "failed to stop backend client");
        return -1;
    }

    rec->request = W3C_REQUEST_STOP;

    return reply_status(c->t, reqno, 0);
}


static int w3c_abort_recognizer(w3c_client_t *c, int reqno, mrp_json_t *req)
{
    w3c_recognizer_t *rec;
    int               id, errc;
    const char       *errs;

    if (check_id(c, req, &id) < 0)
        return -1;

    if ((rec = check_recognizer(c, req, id)) == NULL)
        return -1;

    if (stop_recognizer_client(rec, &errc, &errs) < 0) {
        reply_error(c->t, reqno, errc, errs, req,
                    "failed to stop backend client");
        return -1;
    }

    rec->request = W3C_REQUEST_ABORT;

    return reply_status(c->t, reqno, 0);
}


static int w3c_create_utterance(w3c_client_t *c, int reqno, mrp_json_t *req)
{
    w3c_synthesizer_t *syn = c->syn;
    w3c_attrdef_t     *def = utterance_attributes();
    w3c_utterance_t   *utt;
    mrp_json_t        *set;
    size_t             base;
    int                mask;
    int                errc;
    const char        *errs;

    if ((utt = create_utterance(syn)) == NULL)
        return reply_error(c->t, reqno, ENOMEM, W3C_FAILED, req,
                           "failed to create new utterance instance");

    if ((set = mrp_json_get(req, "set")) != NULL) {
        base = MRP_OFFSET(w3c_utterance_t, attr);
        errc = 0;
        errs = NULL;
        mask = w3c_set_attributes(set, utt, base, def, &errc, &errs);
    }
    else
        mask = 0;

    if (mask < 0) {
        reply_error(c->t, reqno, errc, errs, req,
                    "failed to set utterance attribute (#%d)", -mask);
        destroy_utterance(utt);
        return -1;
    }

    return reply_status(c->t, reqno, 0, "id", MRP_JSON_INTEGER, utt->id);
}


static int w3c_speak_utterance(w3c_client_t *c, int reqno, mrp_json_t *req)
{
    w3c_utterance_t   *utt;
    w3c_synthesizer_t *syn;
    int                id, uid;
    int                errc;
    const char        *errs;
    bool               had_pending;

    if (check_id(c, req, &id) < 0)
        return -1;

    if (id != 0)
        return reply_error(c->t, reqno, EINVAL, W3C_MALFORMED, req,
                           "speak must use implicit ID 0");

    if (!mrp_json_get_integer(req, "utterance", &uid))
        return malformed_request(c->t, req, "missing utterace ID");

    if ((utt = check_utterance(c, req, uid)) == NULL)
        return -1;

    if (utt->vid != SRS_VOICE_INVALID) {
        reply_error(c->t, reqno, EBUSY, W3C_BUSY, req,
                    "utterance is already being played/queued");
        return -1;
    }

    if (utt->attr.text == NULL) {
        reply_error(c->t, reqno, EINVAL, W3C_FAILED, req,
                    "utterance text not set");
        return -1;
    }

    if (utt->attr.lang == NULL && utt->attr.voice == NULL) {
        reply_error(c->t, reqno, EINVAL, W3C_FAILED, req,
                    "neither voice nor language is set");
        return -1;
    }

    syn = utt->syn;

    if (syn->srsc == NULL) {
        if (create_synthesizer_client(syn, &errc, &errs) < 0) {
            reply_error(c->t, reqno, errc, errs, req,
                        "failed to create backend client");
            return -1;
        }
    }

    had_pending = !mrp_list_empty(&syn->pending);

    if (activate_utterance(utt) < 0) {
        reply_error(c->t, reqno, EINVAL, W3C_FAILED, req,
                    "synthesizer backend failed");
        return -1;
    }

    reply_status(c->t, reqno, 0);
    update_pending(syn, had_pending);

    return 0;
}


static int w3c_cancel_utterance(w3c_client_t *c, int reqno, mrp_json_t *req)
{
    w3c_synthesizer_t *syn = c->syn;
    w3c_utterance_t   *utt;
    mrp_list_hook_t   *p, *n;
    int                id, uid;
    bool               cancelled;

    if (check_id(c, req, &id) < 0)
        return -1;

    cancelled = false;

    if (id != 0)
        return reply_error(c->t, reqno, EINVAL, W3C_MALFORMED, req,
                           "cancel must use implicit ID 0");

    if (mrp_json_get_integer(req, "utterance", &uid)) {
        if ((utt = check_utterance(c, req, uid)) == NULL)
            return -1;

        cancel_utterance(utt);
    }
    else {
        mrp_list_foreach(&syn->pending, p, n) {
            utt = mrp_list_entry(p, typeof(*utt), pending);

            cancel_utterance(utt);
            cancelled = true;
        }
    }

    reply_status(c->t, reqno, 0);
    update_pending(syn, cancelled);

    return 0;
}


static int w3c_pause_utterance(w3c_client_t *c, int reqno, mrp_json_t *req)
{
    w3c_synthesizer_t *syn = c->syn;
    w3c_utterance_t   *utt;
    mrp_list_hook_t   *p, *n;
    int                id;

    if (check_id(c, req, &id) < 0)
        return -1;

    if (id != 0)
        return reply_error(c->t, reqno, EINVAL, W3C_MALFORMED, req,
                           "pause must use implicit ID 0");

    mrp_list_foreach(&syn->pending, p, n) {
        utt = mrp_list_entry(p, typeof(*utt), pending);

        pause_utterance(utt);
    }

    reply_status(c->t, reqno, 0);

    syn->paused = true;
    update_paused(syn, true);

    return 0;
}


static int w3c_resume_utterance(w3c_client_t *c, int reqno, mrp_json_t *req)
{
    w3c_synthesizer_t *syn = c->syn;
    w3c_utterance_t   *utt;
    mrp_list_hook_t   *p, *n;
    int                id;
    bool               first;

    if (check_id(c, req, &id) < 0)
        return -1;

    if (id != 0)
        return reply_error(c->t, reqno, EINVAL, W3C_MALFORMED, req,
                           "pause must use implicit ID 0");

    syn->paused = false;

    first = true;
    mrp_list_foreach(&syn->pending, p, n) {
        utt = mrp_list_entry(p, typeof(*utt), pending);

        if (first)
            resume_utterance(utt);
        else
            activate_utterance(utt);

        first = false;
    }

    reply_status(c->t, reqno, 0);
    update_paused(syn, false);

    return 0;
}


static int w3c_get_voices(w3c_client_t *c, int reqno, mrp_json_t *req)
{
    srs_context_t     *srs    = c->s->self->srs;
    mrp_json_t        *voices = NULL;
    const char        *lang   = NULL;
    srs_voice_actor_t *actors, *a;
    int                nactor, i;
    mrp_json_t        *actor;

    mrp_json_get_string(req, "lang", &lang);

    nactor = srs_query_voices(srs, lang, &actors);

    if (nactor < 0) {
        reply_error(c->t, reqno, EINVAL, W3C_FAILED, req,
                    "failed to query backend voices");
        return -1;
    }

    voices = mrp_json_create(MRP_JSON_ARRAY);

    if (voices == NULL)
        goto oom;

    for (i = 0, a = actors; i < nactor; i++, a++) {
        actor = mrp_json_create(MRP_JSON_OBJECT);

        if (actor == NULL)
            goto oom;

        if (!mrp_json_array_append(voices, actor)) {
            mrp_json_unref(actor);
            goto oom;
        }

        mrp_json_add_string (actor, "voiceURI"    , a->name);
        mrp_json_add_string (actor, "lang"        , a->lang);
        mrp_json_add_string (actor, "name"        , a->name);
        mrp_json_add_boolean(actor, "localService", true);
        mrp_json_add_boolean(actor, "default"     , false);
    }

    return reply_status(c->t, reqno, 0,
                        "voices", MRP_JSON_ARRAY, voices);

 oom: /* Rrright... surely this is going to succeed then. */
    reply_error(c->t, reqno, ENOMEM, W3C_NOMEM, req, "out of memory");
    srs_free_queried_voices(actors);
    mrp_json_unref(voices);
    return -1;
}


static request_handler_t get_handler(w3c_client_t *c, mrp_json_t *req,
                                     int *reqnop)
{
    static struct {
        const char        *type;
        const char        *key;
        const char        *value;
        request_handler_t  handler;
    } *h, handlers[] = {
        { "create"   , "object" , "recognizer", w3c_create_recognizer },
        { "create"   , "object" , "utterance" , w3c_create_utterance  },
        { "delete"   , NULL     , NULL        , w3c_delete_object     },
        { "set"      , NULL     , NULL        , w3c_set_attribute     },
        { "timestamp", NULL     , NULL        , w3c_get_timestamp     },
        { "invoke"   , "method" , "start"     , w3c_start_recognizer  },
        { "invoke"   , "method" , "stop"      , w3c_stop_recognizer   },
        { "invoke"   , "method" , "abort"     , w3c_abort_recognizer  },
        { "invoke"   , "method" , "speak"     , w3c_speak_utterance   },
        { "invoke"   , "method" , "cancel"    , w3c_cancel_utterance  },
        { "invoke"   , "method" , "pause"     , w3c_pause_utterance   },
        { "invoke"   , "method" , "resume"    , w3c_resume_utterance  },
        { "invoke"   , "method" , "get-voices", w3c_get_voices        },
        { NULL, NULL, NULL, NULL }
    };

    const char *type, *key, *value;
    int         reqno;

    if (!mrp_json_get_string (req, "type" , &type)) {
        malformed_request(c->t, req, "missing request type");
        goto fail;
    }

    if (!mrp_json_get_integer(req, "reqno", &reqno)) {
        malformed_request(c->t, req, "missing request number");
        goto fail;
    }

    if (reqnop != NULL)
        *reqnop = reqno;

    key   = NULL;
    value = NULL;

    for (h = handlers; h->type != NULL; h++) {
        if (strcmp(type, h->type) != 0)
            continue;

        if (h->key == NULL)
            break;

        if (key == NULL || strcmp(key, h->key) != 0) {
            if (!mrp_json_get_string(req, (key = h->key), &value)) {
                malformed_request(c->t, req, "missing request %s", key);
                goto fail;
            }
        }

        if (!strcmp(value, h->value))
            break;
    }

    if (h->type != NULL)
        return h->handler;
    else
        malformed_request(c->t, req, "unknown request type");

 fail:
    w3c_client_destroy(c);

    return NULL;
}


static void recv_evt(mrp_transport_t *t, mrp_json_t *req, void *user_data)
{
    w3c_client_t      *c = (w3c_client_t *)user_data;
    request_handler_t  h;
    int                reqno;

    MRP_UNUSED(t);

    dump_request(req);

    if ((h = get_handler(c, req, &reqno)) != NULL)
        h(c, reqno, req);
    else
        mrp_log_error("Failed to find request handler for request %s.",
                      mrp_json_object_to_string(req));
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

    if (alen <= 0)
        goto resolve_failed;

    if (s->sock >= 0) {
        state = MRP_TRANSPORT_LISTENED;
        s->lt = mrp_transport_create_from(srs->ml, type, &s->sock, &evt,
                                          s, flags, state);

        if (s->lt == NULL)
            goto create_failed;

        mrp_log_info("Using socket %d for W3C speech transport.", s->sock);
    }
    else {
        s->lt = mrp_transport_create(srs->ml, type, &evt, s, flags);

        if (s->lt == NULL)
            goto create_failed;

        if (!mrp_transport_bind(s->lt, &addr, alen))
            goto bind_failed;

        if (!mrp_transport_listen(s->lt, 0))
            goto bind_failed;

        mrp_log_info("Listening on W3C speech transport '%s'.", s->address);
    }

    return 0;

 resolve_failed:
    mrp_log_error("Can't resolve W3C speech transport '%s'.", s->address);
    goto cleanup_fail;

 create_failed:
    mrp_log_error("Can't create W3C speech transport.");
    goto cleanup_fail;

 bind_failed:
    mrp_log_error("Can't bind/listen W3C speech transport '%s'.", s->address);
    /* fallthru */

 cleanup_fail:
    mrp_transport_destroy(s->lt);
    s->lt = NULL;

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

    mrp_list_init(&s->clients);
    s->self = plugin;

    plugin->plugin_data = s;

    return TRUE;
}


static int w3c_config(srs_plugin_t *plugin, srs_cfg_t *cfg)
{
    w3c_server_t *s = (w3c_server_t *)plugin->plugin_data;

    mrp_debug("configuring W3C speech plugin");

    s->address = srs_config_get_string(cfg, CONFIG_ADDRESS, DEFAULT_ADDRESS);
    s->sock    = srs_config_get_int32(cfg, CONFIG_SOCKET, DEFAULT_SOCKET);

    if (s->sock < 0)
        mrp_log_info("Using W3C speech transport '%s'.", s->address);
    else
        mrp_log_info("Using W3C speech socket %d.", s->sock);

    s->grammar_dir = srs_config_get_string(cfg, CONFIG_GRAMMARDIR,
                                           DEFAULT_GRAMMARDIR);

    mrp_log_info("Looking for W3C grammar files in '%s'.", s->grammar_dir);

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
