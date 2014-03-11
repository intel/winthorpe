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
#include <murphy/common/log.h>
#include <murphy/common/mm.h>
#include <murphy/common/mainloop.h>
#include <murphy/common/glib-glue.h>
#include <murphy/common/transport.h>

#include "native-messages.h"
#include "native-client.h"
#include "native-config.h"


/*
 * SRS client library context
 */

struct srs_s {
    mrp_mainloop_t        *ml;           /* main loop */
    mrp_transport_t       *t;            /* transport to server */
    void                  *user_data;    /* opaque client user data */
    char                  *name;         /* client name */
    char                  *appclass;     /* client application class */
    char                 **commands;     /* client speech command set */
    size_t                 ncommand;     /* number of speech commands */
    srs_connect_notify_t   conn_notify;  /* connection notification callback */
    srs_focus_notify_t     focus_notify; /* focus notification callback */
    srs_command_notify_t   cmd_notify;   /* command notification callback */
    int                    registered:1; /* whether we're registered */
    mrp_list_hook_t        reqq;         /* pending request queue */
    uint32_t               reqno;        /* next request number */
    mrp_list_hook_t        voiceq;       /* pending rendering queue */
    uint32_t               cvid;         /* next client voice id */
};


typedef struct {
    mrp_list_hook_t      hook;
    uint32_t             svid;
    uint32_t             cvid;
    srs_render_notify_t  cb;
    void                *cb_data;
    int                  cancelled : 1;
} voice_req_t;


typedef union {
    voice_req_t voice_req;
    struct {
        srs_voiceqry_notify_t  cb;
        void                  *cb_data;
    } voice_qry;
    struct {
        uint32_t id;
    } voice_ccl;
} request_data_t;

typedef struct {
    mrp_list_hook_t hook;                /* hook to request queue */
    uint32_t        reqno;               /* request number */
    uint32_t        type;                /* request type */
    request_data_t  data;                /* type-specific request data */
} request_t;




static void status_reply(srs_t *srs, srs_rpl_status_t *rpl);
static void rendervoice_reply(srs_t *srs, srs_rpl_voice_t *rpl);
static void queryvoices_reply(srs_t *srs, srs_rpl_voiceqry_t *rpl);
static void focus_event(srs_t *srs, srs_evt_focus_t *evt);
static void command_event(srs_t *srs, srs_evt_command_t *evt);
static void voice_event(srs_t *srs, srs_evt_voice_t *evt);
static mrp_mainloop_t *srs_mml;          /* Murphy mainloop to use, if any */
static GMainLoop      *srs_gml;          /* GMainLoop to use, if any */


static void recvfrom_message(mrp_transport_t *t, void *data, uint32_t type_id,
                             mrp_sockaddr_t *addr, socklen_t addrlen,
                             void *user_data)
{
    MRP_UNUSED(t);
    MRP_UNUSED(data);
    MRP_UNUSED(type_id);
    MRP_UNUSED(addr);
    MRP_UNUSED(addrlen);
    MRP_UNUSED(user_data);

    mrp_log_info("received a message of type 0x%x from the server", type_id);
}


static void recv_message(mrp_transport_t *t, void *data, uint32_t type_id,
                         void *user_data)
{
    srs_t     *srs = (srs_t *)user_data;
    srs_msg_t *msg = (srs_msg_t *)data;
    request_t *req;

    MRP_UNUSED(t);
    MRP_UNUSED(user_data);

    mrp_log_info("received a message of type 0x%x from the server", type_id);

    switch (msg->type) {
    case SRS_REPLY_STATUS:
        status_reply(srs, &msg->status_rpl);
        break;
    case SRS_REPLY_RENDERVOICE:
        rendervoice_reply(srs, &msg->voice_rpl);
        break;
    case SRS_REPLY_QUERYVOICES:
        queryvoices_reply(srs, &msg->voice_lst);
        break;
    case SRS_EVENT_FOCUS:
        focus_event(srs, &msg->focus_evt);
        break;
    case SRS_EVENT_COMMAND:
        command_event(srs, &msg->command_evt);
        break;
    case SRS_EVENT_VOICE:
        voice_event(srs, &msg->voice_evt);
        break;

    default:
        mrp_log_error("Received unknown message of type 0x%x.", type_id);
    }

}


static void closed_event(mrp_transport_t *t, int error, void *user_data)
{
    srs_t *srs    = (srs_t *)user_data;
    int    status = 0;
    char  *msg    = !error ? "connection closed" : "connection error";

    mrp_debug("transport closed by server");

    srs->conn_notify(srs, status, msg, srs->user_data);
}


static int queue_request(srs_t *srs, srs_msg_t *req, request_data_t *data)
{
    request_t *r;

    if ((r = mrp_allocz(sizeof(*r))) == NULL)
        return -1;

    mrp_list_init(&r->hook);
    r->type  = req->any_req.type;
    r->reqno = req->any_req.reqno = srs->reqno++;

    if (data != NULL)
        r->data = *data;

    if (send_message(srs->t, req) == 0) {
        mrp_list_append(&srs->reqq, &r->hook);
        return 0;
    }
    else {
        mrp_free(r);
        return -1;
    }
}


static request_t *find_request(srs_t *srs, uint32_t reqno)
{
    mrp_list_hook_t *p, *n;
    request_t       *r;

    mrp_list_foreach(&srs->reqq, p, n) {
        r = mrp_list_entry(p, typeof(*r), hook);

        if (r->reqno == reqno)
            return r;
    }

    return NULL;
}


static void purge_request(request_t *r)
{
    mrp_list_delete(&r->hook);
    mrp_free(r);
}


static void purge_reqq(srs_t *srs)
{
    mrp_list_hook_t *p, *n;
    request_t       *r;

    mrp_list_foreach(&srs->reqq, p, n) {
        r = mrp_list_entry(p, typeof(*r), hook);
        purge_request(r);
    }
}


static voice_req_t *add_voice_req(srs_t *srs, voice_req_t *req)
{
    voice_req_t *r;

    if ((r = mrp_allocz(sizeof(*r))) == NULL)
        return NULL;

    mrp_list_init(&r->hook);
    r->svid    = req->svid;
    r->cvid    = req->cvid;
    r->cb      = req->cb;
    r->cb_data = req->cb_data;

    mrp_list_append(&srs->voiceq, &r->hook);

    return r;
}


static voice_req_t *voice_req_for_cvid(srs_t *srs, uint32_t cvid)
{
    mrp_list_hook_t *p, *n;
    voice_req_t     *r;

    mrp_list_foreach(&srs->voiceq, p, n) {
        r = mrp_list_entry(p, typeof(*r), hook);

        if (r->cvid == cvid)
            return r;
    }

    return NULL;
}


static voice_req_t *voice_req_for_svid(srs_t *srs, uint32_t svid)
{
    mrp_list_hook_t *p, *n;
    voice_req_t     *r;

    mrp_list_foreach(&srs->voiceq, p, n) {
        r = mrp_list_entry(p, typeof(*r), hook);

        if (r->svid == svid)
            return r;
    }

    return NULL;
}


static request_t *request_for_cvid(srs_t *srs, uint32_t cvid)
{
    mrp_list_hook_t *p, *n;
    request_t       *r;

    mrp_list_foreach(&srs->reqq, p, n) {
        r = mrp_list_entry(p, typeof(*r), hook);

        if (r->type == SRS_REQUEST_RENDERVOICE &&
            r->data.voice_req.cvid == cvid)
            return r;
    }

    return NULL;
}


static void purge_voice_req(voice_req_t *r)
{
    mrp_list_delete(&r->hook);
    mrp_free(r);
}


static void purge_voiceq(srs_t *srs)
{
    mrp_list_hook_t *p, *n;
    voice_req_t     *r;

    mrp_list_foreach(&srs->voiceq, p, n) {
        r = mrp_list_entry(p, typeof(*r), hook);
        purge_voice_req(r);
    }
}


void srs_set_mainloop(mrp_mainloop_t *ml)
{
    if (srs_mml == NULL) {
        srs_mml = ml;
        return;
    }

    if (ml == NULL) {
        srs_mml = NULL;
        return;
    }

    if (srs_mml != ml)
        mrp_log_warning("SRS native client mainloop already set (to %p).",
                        srs_mml);
}


void srs_set_gmainloop(GMainLoop *gml)
{
    if (srs_gml == NULL) {
        srs_gml = g_main_loop_ref(gml);
        return;
    }

    if (gml == NULL) {
        g_main_loop_unref(srs_gml);
        srs_gml = NULL;
        return;
    }

    if (srs_gml != gml)
        mrp_log_warning("SRS native client GMainLoop already set (to %p).",
                        srs_gml);
}


srs_t *srs_create(const char *name, const char *appclass, char **commands,
                  size_t ncommand, srs_connect_notify_t conn_notify,
                  srs_focus_notify_t focus_notify,
                  srs_command_notify_t cmd_notify, void *user_data)
{
    srs_t *srs;
    int    i;

    if (conn_notify == NULL)
        return NULL;

    if ((srs = mrp_allocz(sizeof(*srs))) == NULL)
        return NULL;

    mrp_list_init(&srs->reqq);
    mrp_list_init(&srs->voiceq);
    srs->reqno = 1;
    srs->cvid  = 1;

    if (srs_mml != NULL)
        srs->ml = srs_mml;
    else if (srs_gml != NULL)
        srs->ml = mrp_mainloop_glib_get(srs_gml);
    else
        srs->ml = mrp_mainloop_create();

    if (srs->ml == NULL)
        goto fail;

    srs->user_data    = user_data;
    srs->name         = mrp_strdup(name);
    srs->appclass     = mrp_strdup(appclass);
    srs->conn_notify  = conn_notify;
    srs->focus_notify = focus_notify;
    srs->cmd_notify   = cmd_notify;

    if (srs->name == NULL || srs->appclass == NULL)
        goto fail;

    if (ncommand > 0) {
        if (cmd_notify == NULL)
            goto fail;

        srs->ncommand = ncommand;
        srs->commands = mrp_allocz_array(char *, ncommand);

        if (srs->commands == NULL)
            goto fail;

        for (i = 0; i < srs->ncommand; i++) {
            srs->commands[i] = mrp_strdup(commands[i]);

            if (srs->commands[i] == NULL)
                goto fail;
        }
    }

    return srs;

 fail:
    srs_destroy(srs);

    return NULL;
}


void srs_destroy(srs_t *srs)
{
    int i;

    if (srs == NULL)
        return;

    mrp_transport_destroy(srs->t);
    purge_reqq(srs);

    if (srs->ml != srs_mml)
        mrp_mainloop_destroy(srs->ml);

    mrp_free(srs->name);
    mrp_free(srs->appclass);

    if (srs->commands != NULL) {
        for (i = 0; i < srs->ncommand; i++)
            mrp_free(srs->commands[i]);

        mrp_free(srs->commands);
    }

    mrp_free(srs);
}


int srs_connect(srs_t *srs, const char *server, int reconnect)
{
    static mrp_transport_evt_t evt = {
        { .recvnative     = recv_message     },
        { .recvnativefrom = recvfrom_message },
        .closed           = closed_event  ,
        .connection       = NULL
    };

    mrp_sockaddr_t      addr;
    socklen_t           alen;
    const char         *atype, *opt, *val;
    int                 flags;
    void               *typemap;
    srs_req_register_t  reg;

    if (server == NULL)
        server = DEFAULT_ADDRESS;

    alen = mrp_transport_resolve(NULL, server, &addr, sizeof(addr), &atype);

    if (alen <= 0)
        return -1;

    if ((typemap = register_message_types()) == NULL)
        return -1;

    flags  = MRP_TRANSPORT_REUSEADDR | MRP_TRANSPORT_MODE_NATIVE;
    srs->t = mrp_transport_create(srs->ml, atype, &evt, srs, flags);

    if (srs->t == NULL)
        return -1;

    if (!mrp_transport_setopt(srs->t, "type-map", typemap))
        return -1;

    if (!mrp_transport_connect(srs->t, &addr, alen))
        return -1;

    reg.type     = SRS_REQUEST_REGISTER;
    reg.name     = srs->name;
    reg.appclass = srs->appclass;
    reg.commands = srs->commands;
    reg.ncommand = srs->ncommand;

    return queue_request(srs, (srs_msg_t *)&reg, NULL);
}


void srs_disconnect(srs_t *srs)
{
    if (srs == NULL)
        return;

    mrp_transport_destroy(srs->t);
    srs->t = NULL;
    purge_reqq(srs);
    srs->registered = 0;
}


static int check_connection(srs_t *srs)
{
    if (srs == NULL) {
        errno = EINVAL;
        return -1;
    }

    if (!srs->registered) {
        errno = ENOTCONN;
        return -1;
    }
}


int srs_request_focus(srs_t *srs, srs_voice_focus_t focus)
{
    srs_req_focus_t req;

    if (check_connection(srs) < 0)
        return -1;

    req.type  = SRS_REQUEST_FOCUS;
    req.focus = focus;

    return queue_request(srs, (srs_msg_t *)&req, NULL);
}


uint32_t srs_render_voice(srs_t *srs, const char *msg, const char *voice,
                          double rate, double pitch, int timeout, int events,
                          srs_render_notify_t cb, void *cb_data)
{
    srs_req_voice_t req;
    request_data_t  data;

    if (check_connection(srs) < 0)
        return -1;

    req.type    = SRS_REQUEST_RENDERVOICE;
    req.msg     = (char *)msg;
    req.voice   = (char *)voice;
    req.rate    = rate;
    req.pitch   = pitch;
    req.timeout = timeout;
    req.events  = events;

    memset(&data, 0, sizeof(data));
    data.voice_req.cvid    = srs->cvid++;
    data.voice_req.svid    = SRS_VOICE_INVALID;
    data.voice_req.cb      = cb;
    data.voice_req.cb_data = cb_data;

    if (queue_request(srs, (srs_msg_t *)&req, &data) < 0)
        return -1;
    else
        return data.voice_req.cvid;
}


int srs_cancel_voice(srs_t *srs, uint32_t id)
{
    srs_ccl_voice_t  req;
    request_data_t   data;
    voice_req_t     *vr;
    request_t       *vreq;

    if (check_connection(srs) < 0)
        return -1;

    if ((vr = voice_req_for_cvid(srs, id)) != NULL)
        id = vr->svid;
    else {
        if ((vreq = request_for_cvid(srs, id)) != NULL) {
            vreq->data.voice_req.cancelled = TRUE;
            return 0;
        }
        else {
            errno = ENOENT;
            return -1;
        }
    }

    req.type = SRS_REQUEST_CANCELVOICE;
    req.id   = id;

    data.voice_ccl.id = id;

    return queue_request(srs, (srs_msg_t *)&req, &data);
}


int srs_query_voices(srs_t *srs, const char *language,
                     srs_voiceqry_notify_t cb, void *cb_data)
{
    srs_req_voiceqry_t req;
    request_data_t     data;

    if (check_connection(srs) < 0)
        return -1;

    req.type = SRS_REQUEST_QUERYVOICES;
    req.lang = (char *)(language ? language : "");

    data.voice_qry.cb      = cb;
    data.voice_qry.cb_data = cb_data;

    return queue_request(srs, (srs_msg_t *)&req, &data);
}


static void status_reply(srs_t *srs, srs_rpl_status_t *rpl)
{
    request_t *req    = find_request(srs, rpl->reqno);
    int        status = rpl->status;

    if (req == NULL) {
        mrp_log_warning("Received reply for unkown request #%u.", rpl->reqno);
        return;
    }

    switch (req->type) {
    case SRS_REQUEST_REGISTER:
        mrp_debug("Registration to server %s.",
                  status == 0 ? "successful" : "failed");

        srs->registered = (status == 0);
        srs->conn_notify(srs, status == 0, rpl->msg, srs->user_data);

        if (!srs->registered) {
            purge_reqq(srs);
            purge_voiceq(srs);
        }
        break;

    case SRS_REQUEST_UNREGISTER:
        mrp_debug("Unregistering from server %s.",
                  rpl->status == SRS_STATUS_OK ? "successful" : "failed");

        srs->registered = FALSE;
        purge_reqq(srs);
        purge_voiceq(srs);
        break;

    case SRS_REQUEST_FOCUS:
        mrp_debug("Focus request %s on server.",
                  status == 0 ? "succeeded" : "failed");
        break;

    default:
        mrp_log_warning("Dequeued request with invalid type 0x%x.", req->type);
    }

    purge_request(req);
}


static void rendervoice_reply(srs_t *srs, srs_rpl_voice_t *rpl)
{
    request_t         *req = find_request(srs, rpl->reqno);
    voice_req_t       *vr;
    srs_voice_event_t  e;

    if (req == NULL) {
        mrp_log_warning("Got reply for unknown voice request #%u.", rpl->reqno);
        return;
    }

    vr = &req->data.voice_req;

    if (rpl->id == SRS_VOICE_INVALID) {
        if (vr->cb != NULL) {
            e.type = SRS_VOICE_EVENT_ABORTED;
            e.id   = vr->cvid;
            e.data.progress.pcnt = 0;
            e.data.progress.msec = 0;

            vr->cb(srs, &e, srs->user_data, vr->cb_data);
        }
    }
    else {
        if (req->data.voice_req.cancelled)
            srs_cancel_voice(srs, rpl->id);
        else {
            vr->svid = rpl->id;

            if (add_voice_req(srs, vr) == NULL)
                mrp_log_error("Failed to add active voice request #%u.",
                              vr->cvid);
        }
    }

    purge_request(req);
}


static void voice_event(srs_t *srs, srs_evt_voice_t *evt)
{
    voice_req_t       *vr = voice_req_for_svid(srs, evt->id);
    srs_voice_event_t  e;

    mrp_debug("Got voice event 0x%x for #%u.", evt->event, evt->id);

    if (vr == NULL) {
        mrp_log_warning("Got event for unknown voice request #%u.", evt->id);
        return;
    }

    e.type = evt->event;
    e.id   = vr->cvid;
    e.data.progress.pcnt = evt->pcnt;
    e.data.progress.msec = evt->msec;

    vr->cb(srs, &e, srs->user_data, vr->cb_data);

    if (evt->event == SRS_VOICE_EVENT_COMPLETED ||
        evt->event == SRS_VOICE_EVENT_TIMEOUT   ||
        evt->event == SRS_VOICE_EVENT_ABORTED)
        purge_voice_req(vr);
}


static void queryvoices_reply(srs_t *srs, srs_rpl_voiceqry_t *rpl)
{
    request_t             *req = find_request(srs, rpl->reqno);
    srs_voiceqry_notify_t  cb;
    void                  *cb_data;

    mrp_debug("Got voice query response.");

    if (req == NULL) {
        mrp_log_warning("Received voice query response for unknown request.");
        return;
    }

    cb      = req->data.voice_qry.cb;
    cb_data = req->data.voice_qry.cb_data;

    cb(srs, rpl->actors, rpl->nactor, srs->user_data, cb_data);

    purge_request(req);
}


static void focus_event(srs_t *srs, srs_evt_focus_t *evt)
{
    mrp_debug("Got focus 0x%x.", evt->focus);

    if (srs->focus_notify != NULL)
        srs->focus_notify(srs, evt->focus, srs->user_data);
}


static void command_event(srs_t *srs, srs_evt_command_t *evt)
{
    mrp_debug("Got command event #%u.", evt->idx);

    if (srs->cmd_notify != NULL)
        srs->cmd_notify(srs, evt->idx, evt->tokens, evt->ntoken,
                        srs->user_data);
}
