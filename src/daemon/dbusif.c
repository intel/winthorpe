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
#include <murphy/common/dbus.h>

#include "src/daemon/context.h"
#include "src/daemon/client.h"
#include "src/daemon/dbusif.h"

#define MAX_COMMANDS 256

static int register_cb(mrp_dbus_t *dbus, DBusMessage *msg, void *user_data);
static int unregister_cb(mrp_dbus_t *dbus, DBusMessage *msg, void *user_data);
static int focus_cb(mrp_dbus_t *dbus, DBusMessage *msg, void *user_data);

static int focus_notify(srs_client_t *c, srs_voice_focus_t focus);
static int command_notify(srs_client_t *c, int idx, int ntoken, char **tokens,
                          void *samplebuf, size_t samplelen, uint32_t *start,
                          uint32_t *end);

#define reply_register   simple_reply
#define reply_unregister simple_reply
#define reply_focus      simple_reply


void dbusif_setup(srs_context_t *srs)
{
    const char *path, *iface, *method;
    int       (*cb)(mrp_dbus_t *, DBusMessage *, void *);

    mrp_debug("setting up client D-BUS interface (%s)", srs->dbus_address);

    srs->dbus = mrp_dbus_get(srs->ml, srs->dbus_address, NULL);

    if (srs->dbus != NULL) {
        path   = SRS_SERVICE_PATH;
        iface  = SRS_SERVICE_INTERFACE;

        method = SRS_METHOD_REGISTER;
        cb     = register_cb;
        if (!mrp_dbus_export_method(srs->dbus, path, iface, method, cb, srs)) {
            mrp_log_error("Failed to register D-BUS '%s' method.", method);
            goto fail;
        }

        method = SRS_METHOD_UNREGISTER;
        cb     = unregister_cb;
        if (!mrp_dbus_export_method(srs->dbus, path, iface, method, cb, srs)) {
            mrp_log_error("Failed to register D-BUS '%s' method.", method);
            goto fail;
        }

        method = SRS_METHOD_FOCUS;
        cb     = focus_cb;
        if (!mrp_dbus_export_method(srs->dbus, path, iface, method, cb, srs)) {
            mrp_log_error("Failed to register D-BUS '%s' method.", method);
            goto fail;
        }

        if (!mrp_dbus_acquire_name(srs->dbus, SRS_SERVICE_NAME, NULL)) {
            mrp_log_error("Failed to acquire D-BUS name '%s'.",
                          SRS_SERVICE_NAME);
            goto fail;
        }

    }
    else {
        mrp_log_error("Failed to connect to D-BUS (%s).", srs->dbus_address);
        exit(1);
    }

    return;

 fail:
    dbusif_cleanup(srs);
}


void dbusif_cleanup(srs_context_t *srs)
{
    const char *path, *iface, *method;
    int       (*cb)(mrp_dbus_t *, DBusMessage *, void *);

    mrp_debug("cleaning up client D-BUS interface");

    if (srs->dbus != NULL) {
        mrp_dbus_release_name(srs->dbus, SRS_SERVICE_NAME, NULL);

        path   = SRS_SERVICE_PATH;
        iface  = SRS_SERVICE_INTERFACE;

        method = SRS_METHOD_REGISTER;
        cb     = register_cb;
        mrp_dbus_remove_method(srs->dbus, path, iface, method, cb, srs);

        method = SRS_METHOD_UNREGISTER;
        cb     = unregister_cb;
        mrp_dbus_remove_method(srs->dbus, path, iface, method, cb, srs);

        method = SRS_METHOD_FOCUS;
        cb     = focus_cb;
        mrp_dbus_remove_method(srs->dbus, path, iface, method, cb, srs);

        mrp_dbus_unref(srs->dbus);
        srs->dbus = NULL;
    }
}


static void name_change_cb(mrp_dbus_t *dbus, const char *name, int running,
                           const char *owner, void *user_data)
{
    srs_context_t *srs = (srs_context_t *)user_data;
    srs_client_t  *c;

    MRP_UNUSED(owner);

    mrp_debug("D-BUS client %s %s", name, running ? "up" : "down");

    if (!running) {
        c = client_lookup_by_id(srs, name);

        if (c != NULL) {
            mrp_log_info("client %s disconnected from D-BUS", name);
            client_destroy(c);
            mrp_dbus_forget_name(dbus, name, name_change_cb, user_data);
        }
    }
}


static void simple_reply(mrp_dbus_t *dbus, DBusMessage *req, int errcode,
                         const char *errmsg)
{
    int32_t error;

    if (!errcode)
        mrp_dbus_reply(dbus, req, DBUS_TYPE_INVALID);
    else {
        error = errcode;
        mrp_dbus_reply_error(dbus, req, DBUS_ERROR_FAILED, errmsg,
                             DBUS_TYPE_INT32, &error, DBUS_TYPE_INVALID);
    }
}


static int parse_commands(DBusMessageIter *it, char **commands, int ncommand)
{
    int n;

    n = 0;
    while (dbus_message_iter_get_arg_type(it) == DBUS_TYPE_STRING) {
        if (n >= ncommand)
            return -1;

        dbus_message_iter_get_basic(it, commands + n);
        n++;

        dbus_message_iter_next(it);
    }

    return n;
}


static int parse_register(DBusMessage *req, const char **id, const char **name,
                          const char **appclass, char **commands, int *ncommand,
                          const char **errmsg)
{
    DBusMessageIter it, ia;

    *id = dbus_message_get_sender(req);

    if (*id == NULL || !dbus_message_iter_init(req, &it)) {
        *errmsg = "failed to parse register message";
        return EINVAL;
    }

    if (dbus_message_iter_get_arg_type(&it) != DBUS_TYPE_STRING)
        goto malformed;
    else
        dbus_message_iter_get_basic(&it, name);

    dbus_message_iter_next(&it);

    if (dbus_message_iter_get_arg_type(&it) != DBUS_TYPE_STRING)
        goto malformed;
    else
        dbus_message_iter_get_basic(&it, appclass);

    dbus_message_iter_next(&it);

    if (dbus_message_iter_get_arg_type(&it) != DBUS_TYPE_ARRAY)
        goto malformed;
    else
        dbus_message_iter_recurse(&it, &ia);

    *ncommand = parse_commands(&ia, commands, *ncommand);

    if (*ncommand > 0)
        return 0;
    else {
        *errmsg = "failed to parse commands";
        return EINVAL;
    }

 malformed:
    *errmsg = "malformed register message";
    return EINVAL;
}


static int register_cb(mrp_dbus_t *dbus, DBusMessage *req, void *user_data)
{
    static srs_client_ops_t ops = {
        .notify_focus   = focus_notify,
        .notify_command = command_notify
    };

    srs_context_t   *srs = (srs_context_t *)user_data;
    const char      *id, *name, *appcls, *errmsg;
    char            *cmds[MAX_COMMANDS];
    int              ncmd, err;
    srs_client_t    *c;

    ncmd = MRP_ARRAY_SIZE(cmds);
    err  = parse_register(req, &id, &name, &appcls, &cmds[0], &ncmd, &errmsg);

    if (err) {
        reply_register(dbus, req, err, errmsg);

        return TRUE;
    }

    mrp_debug("got register request from %s", id);

    c = client_create(srs, SRS_CLIENT_TYPE_DBUS, name, appcls, cmds, ncmd,
                      id, &ops, NULL);

    if (c != NULL) {
        if (mrp_dbus_follow_name(dbus, id, name_change_cb, srs)) {
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


static int parse_unregister(DBusMessage *req, const char **id,
                            const char **errmsg)
{
    *id = dbus_message_get_sender(req);

    if (*id != NULL)
        return 0;
    else {
        *errmsg = "failed to determine client id";
        return EINVAL;
    }
}


static int unregister_cb(mrp_dbus_t *dbus, DBusMessage *req, void *user_data)
{
    srs_context_t *srs = (srs_context_t *)user_data;
    const char    *id, *errmsg;
    srs_client_t  *c;
    int            err;

    err = parse_unregister(req, &id, &errmsg);

    if (!err) {
        mrp_debug("got unregister request from %s", id);

        c = client_lookup_by_id(srs, id);

        if (c != NULL) {
            mrp_dbus_forget_name(dbus, c->id, name_change_cb, srs);
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


static int parse_focus(DBusMessage *req, const char **id, int *focus,
                       const char **errmsg)
{
    const char *type;

    *id = dbus_message_get_sender(req);

    if (*id != NULL) {
        if (dbus_message_get_args(req, NULL, DBUS_TYPE_STRING, &type,
                                  DBUS_TYPE_INVALID)) {
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


static int focus_cb(mrp_dbus_t *dbus, DBusMessage *req, void *user_data)
{
    srs_context_t *srs = (srs_context_t *)user_data;
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
    srs_context_t *srs   = c->srs;
    const char    *dest  = c->id;
    const char    *path  = SRS_SERVICE_PATH;
    const char    *iface = SRS_SERVICE_INTERFACE;
    const char    *sig   = SRS_SIGNAL_FOCUS;
    const char    *state;

    switch (focus) {
    case SRS_VOICE_FOCUS_NONE:      state = "none";      break;
    case SRS_VOICE_FOCUS_SHARED:    state = "shared";    break;
    case SRS_VOICE_FOCUS_EXCLUSIVE: state = "exclusive"; break;
    default:                        return FALSE;
    }

    return mrp_dbus_signal(srs->dbus, dest, path, iface, sig,
                           DBUS_TYPE_STRING, &state, DBUS_TYPE_INVALID);
}


static int command_notify(srs_client_t *c, int idx, int ntoken, char **tokens,
                          void *samplebuf, size_t samplelen, uint32_t *start,
                          uint32_t *end)
{
    srs_context_t *srs   = c->srs;
    const char    *dest  = c->id;
    const char    *path  = SRS_SERVICE_PATH;
    const char    *iface = SRS_SERVICE_INTERFACE;
    const char    *sig   = SRS_SIGNAL_COMMAND;

    char           buf[1024], *cmd, *p, *t;
    int            i, n, l;

    MRP_UNUSED(idx);
    MRP_UNUSED(samplebuf);
    MRP_UNUSED(samplelen);
    MRP_UNUSED(start);
    MRP_UNUSED(end);

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

    return mrp_dbus_signal(srs->dbus, dest, path, iface, sig,
                           DBUS_TYPE_STRING, &cmd, DBUS_TYPE_INVALID);
}
