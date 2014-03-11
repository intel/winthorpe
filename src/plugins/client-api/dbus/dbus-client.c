/*
 * Copyright (c) 2012, Intel Corporation
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

#include <stdlib.h>
#include <errno.h>

#include <murphy/common/log.h>
#include <murphy/common/debug.h>
#include <murphy/common/dbus-libdbus.h>

#include "srs/daemon/plugin.h"
#include "srs/daemon/client.h"

#include "dbus-config.h"

#define PLUGIN_NAME    "dbus-client"
#define PLUGIN_DESCR   "A D-Bus client plugin for SRS."
#define PLUGIN_AUTHORS "Krisztian Litkey <kli@iki.fi>"
#define PLUGIN_VERSION "0.0.1"

#define BUS_CONFIG  "dbus.address"
#define BUS_DEFAULT "session"

#define MAX_COMMANDS 256

static int register_req(mrp_dbus_t *dbus, mrp_dbus_msg_t *msg, void *user_data);
static int unregister_req(mrp_dbus_t *dbus, mrp_dbus_msg_t *msg,
                          void *user_data);
static int focus_req(mrp_dbus_t *dbus, mrp_dbus_msg_t *msg, void *user_data);
static int render_voice_req(mrp_dbus_t *dbus, mrp_dbus_msg_t *msg,
                            void *user_data);
static int cancel_voice_req(mrp_dbus_t *dbus, mrp_dbus_msg_t *msg,
                            void *user_data);
static int query_voices_req(mrp_dbus_t *dbus, mrp_dbus_msg_t *msg,
                            void *user_data);

static int focus_notify(srs_client_t *c, srs_voice_focus_t focus);
static int command_notify(srs_client_t *c, int idx, int ntoken, char **tokens,
                          uint32_t *start, uint32_t *end,
                          srs_audiobuf_t *audio);
static int voice_notify(srs_client_t *c, srs_voice_event_t *event);

#define reply_error      simple_reply
#define reply_register   simple_reply
#define reply_unregister simple_reply
#define reply_focus      simple_reply
#define reply_cancel     simple_reply

typedef struct {
    srs_plugin_t *self;                  /* our plugin instance */
    const char   *address;               /* bus address */
    mrp_dbus_t   *dbus;                  /* bus we're on */
} dbusif_t;


static void dbusif_cleanup(dbusif_t *bus);


static int dbusif_setup(dbusif_t *bus)
{
    srs_context_t  *srs = bus->self->srs;
    const char     *path, *iface, *method;
    int           (*cb)(mrp_dbus_t *, mrp_dbus_msg_t *, void *);

    mrp_debug("setting up client D-BUS interface (%s)", bus->address);

    bus->dbus = mrp_dbus_get(srs->ml, bus->address, NULL);

    if (bus->dbus != NULL) {
        path   = SRS_CLIENT_PATH;
        iface  = SRS_CLIENT_INTERFACE;

        method = SRS_CLIENT_REGISTER;
        cb     = register_req;
        if (!mrp_dbus_export_method(bus->dbus, path, iface, method, cb, bus)) {
            mrp_log_error("Failed to register D-BUS '%s' method.", method);
            goto fail;
        }

        method = SRS_CLIENT_UNREGISTER;
        cb     = unregister_req;
        if (!mrp_dbus_export_method(bus->dbus, path, iface, method, cb, bus)) {
            mrp_log_error("Failed to register D-BUS '%s' method.", method);
            goto fail;
        }

        method = SRS_CLIENT_REQUEST_FOCUS;
        cb     = focus_req;
        if (!mrp_dbus_export_method(bus->dbus, path, iface, method, cb, bus)) {
            mrp_log_error("Failed to register D-BUS '%s' method.", method);
            goto fail;
        }

        method = SRS_CLIENT_RENDER_VOICE;
        cb     = render_voice_req;
        if (!mrp_dbus_export_method(bus->dbus, path, iface, method, cb, bus)) {
            mrp_log_error("Failed to register D-BUS '%s' method.", method);
            goto fail;
        }

        method = SRS_CLIENT_CANCEL_VOICE;
        cb     = cancel_voice_req;
        if (!mrp_dbus_export_method(bus->dbus, path, iface, method, cb, bus)) {
            mrp_log_error("Failed to register D-BUS '%s' method.", method);
            goto fail;
        }

        method = SRS_CLIENT_QUERY_VOICES;
        cb     = query_voices_req;
        if (!mrp_dbus_export_method(bus->dbus, path, iface, method, cb, bus)) {
            mrp_log_error("Failed to register D-BUS '%s' method.", method);
            goto fail;
        }

        if (!mrp_dbus_acquire_name(bus->dbus, SRS_CLIENT_SERVICE, NULL)) {
            mrp_log_error("Failed to acquire D-BUS name '%s'.",
                          SRS_CLIENT_SERVICE);
            goto fail;
        }

    }
    else {
        mrp_log_error("Failed to connect to D-BUS (%s).", bus->address);
        goto fail;
    }

    return TRUE;

 fail:
    dbusif_cleanup(bus);
    return FALSE;
}


static void dbusif_cleanup(dbusif_t *bus)
{
    srs_context_t  *srs = bus->self->srs;
    const char     *path, *iface, *method;
    int           (*cb)(mrp_dbus_t *, mrp_dbus_msg_t *, void *);

    mrp_debug("cleaning up client D-BUS interface");

    if (bus->dbus != NULL) {
        mrp_dbus_release_name(bus->dbus, SRS_CLIENT_SERVICE, NULL);

        path   = SRS_CLIENT_PATH;
        iface  = SRS_CLIENT_INTERFACE;

        method = SRS_CLIENT_REGISTER;
        cb     = register_req;
        mrp_dbus_remove_method(bus->dbus, path, iface, method, cb, bus);

        method = SRS_CLIENT_UNREGISTER;
        cb     = unregister_req;
        mrp_dbus_remove_method(bus->dbus, path, iface, method, cb, bus);

        method = SRS_CLIENT_REQUEST_FOCUS;
        cb     = focus_req;
        mrp_dbus_remove_method(bus->dbus, path, iface, method, cb, bus);

        method = SRS_CLIENT_RENDER_VOICE;
        cb     = render_voice_req;
        mrp_dbus_remove_method(bus->dbus, path, iface, method, cb, bus);

        method = SRS_CLIENT_CANCEL_VOICE;
        cb     = cancel_voice_req;
        mrp_dbus_remove_method(bus->dbus, path, iface, method, cb, bus);

        method = SRS_CLIENT_QUERY_VOICES;
        cb     = query_voices_req;
        mrp_dbus_remove_method(bus->dbus, path, iface, method, cb, bus);

        mrp_dbus_unref(bus->dbus);
        bus->dbus = NULL;
    }
}


static void name_change_cb(mrp_dbus_t *dbus, const char *name, int running,
                           const char *owner, void *user_data)
{
    dbusif_t      *bus = (dbusif_t *)user_data;
    srs_context_t *srs = bus->self->srs;
    srs_client_t  *c;

    MRP_UNUSED(owner);

    mrp_debug("D-BUS client %s %s", name, running ? "up" : "down");

    if (!running) {
        c = client_lookup_by_id(srs, name);

        if (c != NULL) {
            mrp_log_info("client %s disconnected from D-BUS", name);
            client_destroy(c);
            mrp_dbus_forget_name(dbus, name, name_change_cb, bus);
        }
    }
}


static void simple_reply(mrp_dbus_t *dbus, mrp_dbus_msg_t *req, int errcode,
                         const char *errmsg)
{
    int32_t error;

    if (!errcode)
        mrp_dbus_reply(dbus, req, MRP_DBUS_TYPE_INVALID);
    else {
        error = errcode;
        mrp_dbus_reply_error(dbus, req, MRP_DBUS_ERROR_FAILED, errmsg,
                             MRP_DBUS_TYPE_INT32, &error,
                             MRP_DBUS_TYPE_INVALID);
    }
}


static void reply_render(mrp_dbus_t *dbus, mrp_dbus_msg_t *req, uint32_t id)
{
    mrp_dbus_reply(dbus, req, MRP_DBUS_TYPE_UINT32, &id,
                   MRP_DBUS_TYPE_INVALID);
}


static char *clear_non_us_ascii(char *s)
{
    char *p;

    for (p = s; *p; p++) {
        if (*p & 0x80)
            *p = '?';
    }

    return s;
}


static void reply_voice_query(mrp_dbus_t *dbus, mrp_dbus_msg_t *req, int nactor,
                              srs_voice_actor_t *actors)
{
    srs_voice_actor_t *a;
    char              *voices[nactor], **v;
    char              *lang[nactor], **ml;
    char              *dialect[nactor], **sl;
    char              *gender[nactor], **g;
    char              *description[nactor], **d;
    uint32_t           n;
    int                i;

    a  = actors;
    v  = voices;
    ml = lang;
    sl = dialect;
    g  = gender;
    d  = description;
    for (i = 0; i < nactor; i++, a++, v++, ml++, sl++, g++, d++) {
        *v  = a->name;
        *ml = a->lang;
        *sl = a->dialect ? a->dialect : "";
        *g  = a->gender == SRS_VOICE_GENDER_MALE ? "male" : "female";

        /*
         * XXX TODO: this is a hack is currently needed for festival
         * which can feed us voice descriptions that are not UTF-8
         * (and consequently not 7-bit ASCII either).
         */
        *d  = clear_non_us_ascii(a->description);

        printf("* description: %s\n", *d);
    }

    n = nactor;
    v  = voices;
    ml = lang;
    sl = dialect;
    g  = gender;
    d  = description;
    mrp_dbus_reply(dbus, req,
                   MRP_DBUS_TYPE_UINT32, &n,
                   MRP_DBUS_TYPE_ARRAY, DBUS_TYPE_STRING, &v , n,
                   MRP_DBUS_TYPE_ARRAY, DBUS_TYPE_STRING, &ml, n,
                   MRP_DBUS_TYPE_ARRAY, DBUS_TYPE_STRING, &sl, n,
                   MRP_DBUS_TYPE_ARRAY, DBUS_TYPE_STRING, &g , n,
                   MRP_DBUS_TYPE_ARRAY, DBUS_TYPE_STRING, &d , n,
                   MRP_DBUS_TYPE_INVALID);
}


static int parse_commands(mrp_dbus_msg_t *msg, char **commands, int ncommand)
{
    int n;

    n = 0;
    while (n < ncommand - 1) {
        if (mrp_dbus_msg_read_basic(msg, MRP_DBUS_TYPE_STRING, commands + n))
            n++;
        else
            return -1;
    }

    return n;
}


static int parse_register(mrp_dbus_msg_t *req, const char **id,
                          const char **name, const char **appclass,
                          char ***commands, int *ncommand, const char **errmsg)
{
    void   *cmds;
    size_t  ncmd;

    *id = mrp_dbus_msg_sender(req);

    if (*id == NULL) {
        *errmsg = "failed to parse register message";
        return EINVAL;
    }

    if (!mrp_dbus_msg_read_basic(req, MRP_DBUS_TYPE_STRING, name))
        goto malformed;

    if (!mrp_dbus_msg_read_basic(req, MRP_DBUS_TYPE_STRING, appclass))
        goto malformed;

    if (mrp_dbus_msg_read_array(req, MRP_DBUS_TYPE_STRING, &cmds, &ncmd)) {
        if (ncmd > 0) {
            *commands = cmds;
            *ncommand = (int)ncmd;

            return 0;
        }
    }

 malformed:
    *errmsg = "malformed register message";
    return EINVAL;
}


static int register_req(mrp_dbus_t *dbus, mrp_dbus_msg_t *req, void *user_data)
{
    static srs_client_ops_t ops = {
        .notify_focus   = focus_notify,
        .notify_command = command_notify,
        .notify_render  = voice_notify,
    };

    dbusif_t        *bus = (dbusif_t *)user_data;
    srs_context_t   *srs = bus->self->srs;
    const char      *id, *name, *appcls, *errmsg;
    char           **cmds;
    int              ncmd, err;
    srs_client_t    *c;

    ncmd = MRP_ARRAY_SIZE(cmds);
    err  = parse_register(req, &id, &name, &appcls, &cmds, &ncmd, &errmsg);

    if (err) {
        reply_register(dbus, req, err, errmsg);

        return TRUE;
    }

    mrp_debug("got register request from %s", id);

    c = client_create(srs, SRS_CLIENT_TYPE_EXTERNAL, name, appcls, cmds, ncmd,
                      id, &ops, bus);

    if (c != NULL) {
        if (mrp_dbus_follow_name(dbus, id, name_change_cb, bus)) {
            err    = 0;
            errmsg = NULL;
        }
        else {
            client_destroy(c);
            err    = EINVAL;
            errmsg = "failed to track DBUS name";
        }
    }
    else {
        err    = EINVAL;
        errmsg = "failed to register client";
    }

    reply_register(dbus, req, err, errmsg);
    return TRUE;
}


static int parse_unregister(mrp_dbus_msg_t *req, const char **id,
                            const char **errmsg)
{
    *id = mrp_dbus_msg_sender(req);

    if (*id != NULL)
        return 0;
    else {
        *errmsg = "failed to determine client id";
        return EINVAL;
    }
}


static int unregister_req(mrp_dbus_t *dbus, mrp_dbus_msg_t *req,
                          void *user_data)
{
    dbusif_t      *bus = (dbusif_t *)user_data;
    srs_context_t *srs = bus->self->srs;
    const char    *id, *errmsg;
    srs_client_t  *c;
    int            err;

    err = parse_unregister(req, &id, &errmsg);

    if (!err) {
        mrp_debug("got unregister request from %s", id);

        c = client_lookup_by_id(srs, id);

        if (c != NULL) {
            mrp_dbus_forget_name(dbus, c->id, name_change_cb, bus);
            client_destroy(c);
            reply_unregister(dbus, req, 0, NULL);
        }
        else
            reply_unregister(dbus, req, 1, "you don't exist, go away");
    }
    else
        reply_unregister(dbus, req, err, errmsg);

    return TRUE;
}


static int parse_focus(mrp_dbus_msg_t *req, const char **id, int *focus,
                       const char **errmsg)
{
    const char *type;

    *id = mrp_dbus_msg_sender(req);

    if (*id != NULL) {
        if (mrp_dbus_msg_read_basic(req, MRP_DBUS_TYPE_STRING, &type)) {
            if (!strcmp(type, "none"))
                *focus = SRS_VOICE_FOCUS_NONE;
            else if (!strcmp(type, "shared"   ))
                *focus = SRS_VOICE_FOCUS_SHARED;
            else if (!strcmp(type, "exclusive"))
                *focus = SRS_VOICE_FOCUS_EXCLUSIVE;
            else {
                *errmsg = "invalid voice focus requested";
                return EINVAL;
            }

            return 0;
        }
        else
            *errmsg = "malformed voice focus request";
    }
    else
        *errmsg = "failed to determine client id";

    return EINVAL;
}


static int focus_req(mrp_dbus_t *dbus, mrp_dbus_msg_t *req, void *user_data)
{
    dbusif_t      *bus = (dbusif_t *)user_data;
    srs_context_t *srs = bus->self->srs;
    const char    *id, *errmsg;
    int            focus, err;
    srs_client_t  *c;

    err = parse_focus(req, &id, &focus, &errmsg);

    if (err == 0) {
        mrp_debug("got 0x%x focus request from %s", focus, id);

        c = client_lookup_by_id(srs, id);

        if (c != NULL) {
            if (client_request_focus(c, focus))
                reply_focus(dbus, req, 0, NULL);
            else
                reply_focus(dbus, req, 1, "focus request failed");
        }
        else
            reply_focus(dbus, req, 1, "you don't exist, go away");
    }
    else
        reply_focus(dbus, req, err, errmsg);

    return TRUE;
}


static int focus_notify(srs_client_t *c, srs_voice_focus_t focus)
{
    dbusif_t      *bus   = (dbusif_t *)c->user_data;
    srs_context_t *srs   = c->srs;
    const char    *dest  = c->id;
    const char    *path  = SRS_CLIENT_PATH;
    const char    *iface = SRS_CLIENT_INTERFACE;
    const char    *sig   = SRS_CLIENT_NOTIFY_FOCUS;
    const char    *state;

    switch (focus) {
    case SRS_VOICE_FOCUS_NONE:      state = "none";      break;
    case SRS_VOICE_FOCUS_SHARED:    state = "shared";    break;
    case SRS_VOICE_FOCUS_EXCLUSIVE: state = "exclusive"; break;
    default:                        return FALSE;
    }

    return mrp_dbus_signal(bus->dbus, dest, path, iface, sig,
                           MRP_DBUS_TYPE_STRING, state, MRP_DBUS_TYPE_INVALID);
}


static int command_notify(srs_client_t *c, int idx, int ntoken, char **tokens,
                          uint32_t *start, uint32_t *end, srs_audiobuf_t *audio)
{
    dbusif_t      *bus   = (dbusif_t *)c->user_data;
    srs_context_t *srs   = c->srs;
    const char    *dest  = c->id;
    const char    *path  = SRS_CLIENT_PATH;
    const char    *iface = SRS_CLIENT_INTERFACE;
    const char    *sig   = SRS_CLIENT_NOTIFY_COMMAND;

    char           buf[1024], *cmd, *p, *t;
    int            i, n, l;

    MRP_UNUSED(idx);
    MRP_UNUSED(start);
    MRP_UNUSED(end);
    MRP_UNUSED(audio);

    p = cmd = buf;
    l = sizeof(buf) - 1;
    t = "";

    for (i = 0; i < ntoken; i++) {
        n = snprintf(p, l, "%s%s", t, tokens[i]);

        if (n >= l)
            return FALSE;

        p += n;
        l -= n;
        t  = " ";
    }

    return mrp_dbus_signal(bus->dbus, dest, path, iface, sig,
                           MRP_DBUS_TYPE_STRING, cmd, MRP_DBUS_TYPE_INVALID);
}


static int voice_notify(srs_client_t *c, srs_voice_event_t *event)
{
    dbusif_t      *bus   = (dbusif_t *)c->user_data;
    srs_context_t *srs   = c->srs;
    const char    *dest  = c->id;
    const char    *path  = SRS_CLIENT_PATH;
    const char    *iface = SRS_CLIENT_INTERFACE;
    const char    *sig   = SRS_CLIENT_NOTIFY_VOICE;
    const char    *type;
    double         pcnt;
    uint32_t       msec;


    switch (event->type) {
    case SRS_VOICE_EVENT_STARTED:   type = "started"  ; goto send;
    case SRS_VOICE_EVENT_COMPLETED: type = "completed"; goto send;
    case SRS_VOICE_EVENT_TIMEOUT:   type = "timeout"  ; goto send;
    case SRS_VOICE_EVENT_ABORTED:   type = "aborted"  ; goto send;
    send:
        return mrp_dbus_signal(bus->dbus, dest, path, iface, sig,
                               MRP_DBUS_TYPE_UINT32, &event->id,
                               MRP_DBUS_TYPE_STRING, type,
                               MRP_DBUS_TYPE_INVALID);

    case SRS_VOICE_EVENT_PROGRESS:
        type = "progress";
        pcnt = event->data.progress.pcnt;
        msec = event->data.progress.msec;

        return mrp_dbus_signal(bus->dbus, dest, path, iface, sig,
                               MRP_DBUS_TYPE_UINT32, &event->id,
                               MRP_DBUS_TYPE_STRING, type,
                               MRP_DBUS_TYPE_DOUBLE, &pcnt,
                               MRP_DBUS_TYPE_UINT32, &msec,
                               MRP_DBUS_TYPE_INVALID);

    default:
        return TRUE;
    }
}


static int parse_render_voice(mrp_dbus_msg_t *req, const char **id,
                              const char **msg, const char **voice,
                              double *rate, double *pitch, int *timeout,
                              int *notify_events, const char **errmsg)
{
    char    **events, *e;
    int       i;
    size_t    nevent;
    int32_t   to;

    *id = mrp_dbus_msg_sender(req);

    if (*id == NULL)
        return EINVAL;

    *rate = *pitch = 1;

    if (!mrp_dbus_msg_read_basic(req, MRP_DBUS_TYPE_STRING, msg) ||
        !mrp_dbus_msg_read_basic(req, MRP_DBUS_TYPE_STRING, voice) ||
        (mrp_dbus_msg_arg_type(req, NULL) != MRP_DBUS_TYPE_DOUBLE ||
         !(mrp_dbus_msg_read_basic(req, MRP_DBUS_TYPE_DOUBLE, &rate) &&
           mrp_dbus_msg_read_basic(req, MRP_DBUS_TYPE_DOUBLE, &pitch))) ||
        !mrp_dbus_msg_read_basic(req, MRP_DBUS_TYPE_INT32 , &to) ||
        !mrp_dbus_msg_read_array(req, MRP_DBUS_TYPE_STRING,
                                 (void **)&events, &nevent)) {
        *errmsg = "malformed voice render message";

        return EINVAL;
    }

    *timeout       = to;
    *notify_events = 0;

    for (i = 0; i < nevent; i++) {
        e = events[i];

        if (!strcmp(e, SRS_CLIENT_VOICE_STARTED))
            *notify_events |= SRS_VOICE_MASK_STARTED;
        else if (!strcmp(e, SRS_CLIENT_VOICE_PROGRESS))
            *notify_events |= SRS_VOICE_MASK_PROGRESS;
        else if (!strcmp(e, SRS_CLIENT_VOICE_COMPLETED))
            *notify_events |= SRS_VOICE_MASK_COMPLETED;
        else if (!strcmp(e, SRS_CLIENT_VOICE_TIMEOUT))
            *notify_events |= SRS_VOICE_MASK_TIMEOUT;
        else if (!strcmp(e, SRS_CLIENT_VOICE_ABORTED))
            *notify_events |= SRS_VOICE_MASK_ABORTED;
        else {
            *errmsg = "invalid event";

            return EINVAL;
        }
    }

    return 0;
}


static int render_voice_req(mrp_dbus_t *dbus, mrp_dbus_msg_t *req,
                            void *user_data)
{
    dbusif_t      *bus = (dbusif_t *)user_data;
    srs_context_t *srs = bus->self->srs;
    const char    *id, *msg, *voice, *errmsg;
    double         rate, pitch;
    int            timeout, events, err;
    uint32_t       reqid;
    srs_client_t  *c;

    err = parse_render_voice(req, &id, &msg, &voice, &rate, &pitch, &timeout,
                             &events, &errmsg);

    if (err != 0) {
        reply_error(dbus, req, err, errmsg);

        return TRUE;
    }

    c = client_lookup_by_id(srs, id);

    if (c == NULL) {
        reply_error(dbus, req, 1, "you don't exists, go away");

        return TRUE;
    }

    reqid = client_render_voice(c, msg, voice, rate, pitch, timeout, events);

    if (reqid != SRS_VOICE_INVALID)
        reply_render(dbus, req, reqid);
    else
        reply_error(dbus, req, 1, "voice render request failed");

    return TRUE;
}


static int parse_cancel_voice(mrp_dbus_msg_t *req, const char **id,
                              uint32_t *reqid, const char **errmsg)
{

    *id = mrp_dbus_msg_sender(req);

    if (*id == NULL)
        return EINVAL;

    if (!mrp_dbus_msg_read_basic(req, MRP_DBUS_TYPE_UINT32, &reqid)) {
        *errmsg = "malformed voice render message";

        return EINVAL;
    }

    return 0;
}


static int cancel_voice_req(mrp_dbus_t *dbus, mrp_dbus_msg_t *req,
                            void *user_data)
{
    dbusif_t      *bus = (dbusif_t *)user_data;
    srs_context_t *srs = bus->self->srs;
    const char    *id, *errmsg;
    uint32_t       reqid;
    int            err;
    srs_client_t  *c;

    err = parse_cancel_voice(req, &id, &reqid, &errmsg);

    if (err != 0) {
        reply_cancel(dbus, req, err, errmsg);

        return TRUE;
    }

    c = client_lookup_by_id(srs, id);

    if (c == NULL) {
        reply_cancel(dbus, req, 1, "you don't exists, go away");

        return TRUE;
    }

    client_cancel_voice(c, reqid);
    reply_cancel(dbus, req, 0, NULL);

    return TRUE;
}


static int parse_voice_query(mrp_dbus_msg_t *req, const char **id,
                             const char **lang)
{
    *id = mrp_dbus_msg_sender(req);

    if (*id == NULL)
        return EINVAL;

    if (!mrp_dbus_msg_read_basic(req, MRP_DBUS_TYPE_STRING, lang))
        lang = NULL;

    return 0;
}


static int query_voices_req(mrp_dbus_t *dbus, mrp_dbus_msg_t *req,
                            void *user_data)
{
    dbusif_t          *bus = (dbusif_t *)user_data;
    srs_context_t     *srs = bus->self->srs;
    const char        *lang;
    const char        *id;
    int                err;
    srs_client_t      *c;
    srs_voice_actor_t *actors;
    int                nactor;

    err = parse_voice_query(req, &id, &lang);

    if (err != 0) {
        reply_cancel(dbus, req, err, "internal error");

        return TRUE;
    }

    c = client_lookup_by_id(srs, id);

    if (c == NULL) {
        reply_error(dbus, req, 1, "you don't exists, go away");

        return TRUE;
    }

    nactor = client_query_voices(c, lang, &actors);

    if (nactor < 0)
        reply_error(dbus, req, 1, "voice actor query failed");
    else
        reply_voice_query(dbus, req, nactor, actors);

    client_free_queried_voices(actors);

    return TRUE;
}


static int create_dbusif(srs_plugin_t *plugin)
{
    dbusif_t *bus;

    mrp_debug("creating D-Bus client interface plugin");

    bus = mrp_allocz(sizeof(*bus));

    if (bus != NULL) {
        bus->self = plugin;
        plugin->plugin_data = bus;
        return TRUE;
    }
    else
        return FALSE;
}


static int config_dbusif(srs_plugin_t *plugin, srs_cfg_t *settings)
{
    dbusif_t *bus = (dbusif_t *)plugin->plugin_data;

    MRP_UNUSED(settings);

    mrp_debug("configure D-Bus client interface plugin");

    bus->address = srs_get_string_config(settings, BUS_CONFIG, BUS_DEFAULT);

    mrp_log_info("Client interface D-Bus address: '%s'", bus->address);

    return dbusif_setup(bus);
}


static int start_dbusif(srs_plugin_t *plugin)
{
    dbusif_t *bus = (dbusif_t *)plugin->plugin_data;

    MRP_UNUSED(bus);

    mrp_debug("start D-Bus client interface plugin");

    return TRUE;
}


static void stop_dbusif(srs_plugin_t *plugin)
{
    dbusif_t *bus = (dbusif_t *)plugin->plugin_data;

    mrp_debug("stop D-Bus client interface plugin");

    return;
}


static void destroy_dbusif(srs_plugin_t *plugin)
{
    srs_context_t *srs = plugin->srs;
    dbusif_t      *dbus = (dbusif_t *)plugin->plugin_data;

    MRP_UNUSED(srs);

    mrp_debug("destroy D-Bus client interface plugin");

    dbusif_cleanup(dbus);
    mrp_free(dbus);
}


SRS_DECLARE_PLUGIN(PLUGIN_NAME, PLUGIN_DESCR, PLUGIN_AUTHORS, PLUGIN_VERSION,
                   create_dbusif, config_dbusif, start_dbusif, stop_dbusif,
                   destroy_dbusif)
