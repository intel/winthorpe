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

#include <stdlib.h>
#include <errno.h>

#include <murphy/common/debug.h>
#include <murphy/common/log.h>
#include <murphy/common/mm.h>
#include <murphy/common/list.h>
#include <murphy/common/transport.h>
#include <murphy/common/native-types.h>

#include "src/daemon/plugin.h"
#include "src/daemon/client.h"
#include "native-messages.h"
#include "native-config.h"

#define PLUGIN_NAME    "native-client"
#define PLUGIN_DESCR   "Native client plugin for SRS."
#define PLUGIN_AUTHORS "Krisztian Litkey <kli@iki.fi>"
#define PLUGIN_VERSION "0.0.1"


/*
 * server runtime context
 */

typedef struct {
    srs_plugin_t    *self;               /* our plugin instance */
    const char      *address;            /* our transport address */
    mrp_transport_t *lt;                 /* transport we listen on */
    mrp_list_hook_t  clients;            /* connected clients */
    int              next_id;            /* next client id */
} server_t;


/*
 * a connected client
 */

typedef struct {
    srs_client_t    *c;                  /* associated SRS client */
    server_t        *s;                  /* server runtime context */
    mrp_transport_t *t;                  /* transport towards this client */
    mrp_list_hook_t  hook;               /* to list of native clients */
    int              id;                 /* client id */
} client_t;


static int focus_notify(srs_client_t *c, srs_voice_focus_t focus);
static int command_notify(srs_client_t *c, int idx, int ntoken, char **tokens,
                          uint32_t *start, uint32_t *end,
                          srs_audiobuf_t *audio);
static int voice_notify(srs_client_t *c, srs_voice_event_t *event);

static int reply_status(client_t *c, uint32_t reqno, int status,
                        const char *msg);
#define reply_register   reply_status
#define reply_unregister reply_status
#define reply_focus      reply_status
static int reply_render(client_t *c, uint32_t reqno, uint32_t id);
static int reply_voiceqry(client_t *c, uint32_t reqno,
                          srs_voice_actor_t *actors, int nactor);

static client_t *create_client(server_t *s, mrp_transport_t *lt)
{
    mrp_transport_t *t;
    client_t        *c;

    c = mrp_allocz(sizeof(*c));

    if (c != NULL) {
        mrp_list_init(&c->hook);

        c->s  = s;
        c->id = s->next_id++;
        c->t  = mrp_transport_accept(lt, c, MRP_TRANSPORT_REUSEADDR);

        if (c->t != NULL) {
            mrp_list_append(&s->clients, &c->hook);

            return c;
        }

        mrp_free(c);
    }
    else {
        t = mrp_transport_accept(lt, NULL, MRP_TRANSPORT_REUSEADDR);
        mrp_transport_destroy(t);
    }

    return NULL;
}


static void destroy_client(client_t *c)
{
    mrp_list_delete(&c->hook);

    mrp_transport_destroy(c->t);
    client_destroy(c->c);

    mrp_free(c);
}


static void register_client(client_t *c, srs_req_register_t *req)
{
    static srs_client_ops_t ops = {
        .notify_focus   = focus_notify,
        .notify_command = command_notify,
        .notify_render  = voice_notify,
    };

    srs_context_t  *srs    = c->s->self->srs;
    char           *name   = req->name;
    char           *appcls = req->appclass;
    char          **cmds   = req->commands;
    int             ncmd   = req->ncommand;
    char            id[64];

    snprintf(id, sizeof(id), "native-client-%d", c->id);

    mrp_debug("received register request from native client #%d", c->id);

    c->c = client_create(srs, SRS_CLIENT_TYPE_EXTERNAL, name, appcls,
                         cmds, ncmd, id, &ops, c);

    if (c->c != NULL)
        reply_register(c, req->reqno, SRS_STATUS_OK, "OK");
    else {
        reply_register(c, req->reqno, SRS_STATUS_FAILED, "failed");
        destroy_client(c);
    }
}


static void unregister_client(client_t *c, srs_req_unregister_t *req)
{
    mrp_debug("received unregister request from native client #%d", c->id);

    reply_unregister(c, req->reqno, SRS_STATUS_OK, "OK");
    destroy_client(c);
}


static void request_focus(client_t *c, srs_req_focus_t *req)
{
    mrp_debug("received focus request from native client #%d", c->id);

    if (client_request_focus(c->c, req->focus))
        reply_focus(c, req->reqno, SRS_STATUS_OK, "OK");
    else
        reply_focus(c, req->reqno, SRS_STATUS_FAILED, "failed");
}


static void request_voice(client_t *c, srs_req_voice_t *req)
{
    const char *msg     = req->msg;
    const char *voice   = req->voice;
    int         timeout = req->timeout;
    int         events  = req->events;
    uint32_t    reqid;

    mrp_debug("received voice render request from native client #%d", c->id);

    reqid = client_render_voice(c->c, msg, voice, timeout, events);

    if (reqid != SRS_VOICE_INVALID)
        reply_render(c, req->reqno, reqid);
    else
        reply_status(c, req->reqno, SRS_STATUS_FAILED, "failed");
}


static void cancel_voice(client_t *c, srs_ccl_voice_t *req)
{
    mrp_debug("received voice cancel request from native client #%d", c->id);

    client_cancel_voice(c->c, req->id);
    reply_status(c, req->reqno, SRS_STATUS_OK, "OK");
}


static void query_voices(client_t *c, srs_req_voiceqry_t *req)
{
    srs_voice_actor_t  *actors;
    int                 nactor;

    mrp_debug("received voice query request from native client #%d", c->id);

    nactor = client_query_voices(c->c, req->lang, &actors);
    reply_voiceqry(c, req->reqno, actors, nactor);
    client_free_queried_voices(actors);
}


static int reply_status(client_t *c, uint32_t reqno, int status,
                        const char *msg)
{
    srs_rpl_status_t rpl;

    mrp_debug("replying <%d, %s> to request #%d from native client #%d",
              status, msg, reqno, c->id);

    rpl.type   = SRS_REPLY_STATUS;
    rpl.reqno  = reqno;
    rpl.status = status;
    rpl.msg    = (char *)msg;

    return send_message(c->t, (srs_msg_t *)&rpl);
}


static int reply_render(client_t *c, uint32_t reqno, uint32_t id)
{
    srs_rpl_voice_t rpl;

    mrp_debug("replying <#%u> to request #%d from native client #%d", id,
              reqno, c->id);

    rpl.type  = SRS_REPLY_RENDERVOICE;
    rpl.reqno = reqno;
    rpl.id    = id;

    return send_message(c->t, (srs_msg_t *)&rpl);
}


static int focus_notify(srs_client_t *client, srs_voice_focus_t focus)
{
    client_t        *c = (client_t *)client->user_data;
    srs_evt_focus_t  evt;

    mrp_debug("relaying focus event to native client #%d", c->id);

    evt.type  = SRS_EVENT_FOCUS;
    evt.focus = focus;

    return send_message(c->t, (srs_msg_t *)&evt);
}


static int command_notify(srs_client_t *client, int idx,
                          int ntoken, char **tokens, uint32_t *start,
                          uint32_t *end, srs_audiobuf_t *audio)
{
    client_t          *c = (client_t *)client->user_data;
    srs_evt_command_t  evt;

    MRP_UNUSED(start);
    MRP_UNUSED(end);
    MRP_UNUSED(audio);

    mrp_debug("relaying command event to native client #%d", c->id);

    evt.type   = SRS_EVENT_COMMAND;
    evt.idx    = idx;
    evt.tokens = tokens;
    evt.ntoken = ntoken;

    return send_message(c->t, (srs_msg_t *)&evt);
}


static int voice_notify(srs_client_t *client, srs_voice_event_t *event)
{
    client_t        *c = (client_t *)client->user_data;
    srs_evt_voice_t  evt;

    mrp_debug("relaying voice event to native client #%d", c->id);

    evt.type  = SRS_EVENT_VOICE;
    evt.event = event->type;
    evt.id    = event->id;

    if (event->type == SRS_VOICE_EVENT_PROGRESS) {
        evt.pcnt = event->data.progress.pcnt;
        evt.msec = event->data.progress.msec;
    }
    else {
        evt.pcnt = 0;
        evt.msec = 0;
    }

    return send_message(c->t, (srs_msg_t *)&evt);
}


static int reply_voiceqry(client_t *c, uint32_t reqno,
                          srs_voice_actor_t *actors, int nactor)
{
    srs_rpl_voiceqry_t rpl;

    mrp_debug("replying to request #%u from native client #%d", reqno, c->id);

    if (actors < 0)
        actors = 0;

    rpl.type   = SRS_REPLY_QUERYVOICES;
    rpl.reqno  = reqno;
    rpl.actors = actors;
    rpl.nactor = nactor;

    return send_message(c->t, (srs_msg_t *)&rpl);
}


static inline void dump_message(void *data, uint32_t type_id)
{
    char buf[1024];

    if (mrp_print_native(buf, sizeof(buf), data, type_id) > 0)
        mrp_debug("got message of type 0x%x: %s", type_id, buf);
}


static void connection_evt(mrp_transport_t *lt, void *user_data)
{
    server_t *s = (server_t *)user_data;
    client_t *c;

    c = create_client(s, lt);

    if (c != NULL)
        mrp_log_info("Accepted new native client connection.");
    else
        mrp_log_error("Failed to accept new native client connection.");
}


static void closed_evt(mrp_transport_t *t, int error, void *user_data)
{
    client_t *c = (client_t *)user_data;

    MRP_UNUSED(t);

    if (error != 0)
        mrp_log_error("Native client connection closed with error %d (%s).",
                      error, strerror(error));
    else
        mrp_log_info("Native client connection closed.");

    destroy_client(c);
}


static void recv_evt(mrp_transport_t *t, void *data, uint32_t type_id,
                     void *user_data)
{
    client_t  *c   = (client_t *)user_data;
    srs_msg_t *req = (srs_msg_t *)data;

    MRP_UNUSED(t);

    dump_message(data, type_id);

    switch (req->type) {
    case SRS_REQUEST_REGISTER:
        register_client(c, &req->reg_req);
        break;

    case SRS_REQUEST_UNREGISTER:
        unregister_client(c, &req->bye_req);
        break;

    case SRS_REQUEST_FOCUS:
        request_focus(c, &req->focus_req);
        break;

    case SRS_REQUEST_RENDERVOICE:
        request_voice(c, &req->voice_req);
        break;

    case SRS_REQUEST_CANCELVOICE:
        cancel_voice(c, &req->voice_ccl);
        break;

    case SRS_REQUEST_QUERYVOICES:
        query_voices(c, &req->voice_qry);
        break;

    default:
        break;
    }

}


static int transport_setup(server_t *s)
{
    static mrp_transport_evt_t evt = {
        { .recvnative     = recv_evt },
        { .recvnativefrom = NULL     },
        .connection       = connection_evt,
        .closed           = closed_evt,
    };

    srs_context_t  *srs  = s->self->srs;
    mrp_sockaddr_t  addr;
    socklen_t       alen;
    const char     *type, *opt, *val;
    int             flags;
    void           *typemap;

    alen = mrp_transport_resolve(NULL, s->address, &addr, sizeof(addr), &type);

    if (alen < 0) {
        mrp_log_error("Failed to resolve transport address '%s'.", s->address);
        goto fail;
    }

    flags = MRP_TRANSPORT_REUSEADDR | MRP_TRANSPORT_MODE_NATIVE;
    s->lt = mrp_transport_create(srs->ml, type, &evt, s, flags);

    if (s->lt == NULL) {
        mrp_log_error("Failed to create transport for native clients.");
        return FALSE;
    }

    if ((typemap = register_message_types()) == NULL) {
        mrp_log_error("Failed to register native messages.");
        goto fail;
    }

    if (!mrp_transport_setopt(s->lt, "type-map", typemap)) {
        mrp_log_error("Failed to set transport type map.");
        goto fail;
    }

    if (mrp_transport_bind(s->lt, &addr, alen) &&
        mrp_transport_listen(s->lt, 0)) {
        mrp_log_info("Listening on transport '%s'...", s->address);

        return TRUE;
    }
    else
        mrp_log_error("Failed to bind/listen transport.");

 fail:
    if (s->lt) {
        mrp_transport_destroy(s->lt);
        s->lt = NULL;
    }

    return FALSE;
}


static void transport_cleanup(server_t *s)
{
    mrp_transport_destroy(s->lt);
    s->lt = NULL;
}


static int create_native(srs_plugin_t *plugin)
{
    server_t *s;

    mrp_debug("creating native client interface plugin");

    if ((s = mrp_allocz(sizeof(*s))) == NULL)
        return FALSE;

    mrp_list_init(&s->clients);
    s->self = plugin;

    plugin->plugin_data = s;

    return TRUE;

 fail:
    mrp_free(s);

    return FALSE;
}


static int config_native(srs_plugin_t *plugin, srs_cfg_t *cfg)
{
    server_t *s = (server_t *)plugin->plugin_data;

    mrp_debug("configure native client interface plugin");

    s->address = srs_get_string_config(cfg, CONFIG_ADDRESS, DEFAULT_ADDRESS);
    mrp_log_info("Using native client transport address: '%s'.", s->address);

    return TRUE;
}


static int start_native(srs_plugin_t *plugin)
{
    server_t *s = (server_t *)plugin->plugin_data;

    MRP_UNUSED(plugin);

    return transport_setup(s);
}


static void stop_native(srs_plugin_t *plugin)
{
    MRP_UNUSED(plugin);
}


static void destroy_native(srs_plugin_t *plugin)
{
    server_t *s = (server_t *)plugin->plugin_data;

    transport_cleanup(s);
    mrp_free(s);
}


SRS_DECLARE_PLUGIN(PLUGIN_NAME, PLUGIN_DESCR, PLUGIN_AUTHORS, PLUGIN_VERSION,
                   create_native, config_native, start_native, stop_native,
                   destroy_native)
