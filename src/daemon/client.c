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

#include <murphy/common/mm.h>
#include <murphy/common/log.h>

#include "srs/daemon/resctl.h"
#include "srs/daemon/recognizer.h"
#include "srs/daemon/client.h"

typedef struct {
    mrp_list_hook_t  hook;               /* to list of voice requests */
    srs_client_t    *c;                  /* client for this request */
    uint32_t         id;                 /* voice request id */
    int              notify_events;      /* event mask */
} voice_req_t;


static void resource_event(srs_resctl_event_t *e, void *user_data);

void client_reset_resources(srs_context_t *srs)
{
    mrp_list_hook_t *p, *n;
    srs_client_t    *c;

    mrp_list_foreach(&srs->clients, p, n) {
        c = mrp_list_entry(p, typeof(*c), hook);
        c->rset = NULL;
    }
}


void client_create_resources(srs_context_t *srs)
{
    mrp_list_hook_t   *p, *n;
    srs_client_t      *c;
    srs_voice_focus_t  f;

    mrp_list_foreach(&srs->clients, p, n) {
        c = mrp_list_entry(p, typeof(*c), hook);
        c->rset = srs_resctl_create(srs, c->appclass, resource_event, c);
        if (c->rset != NULL) {
            f = c->requested;
            c->requested = SRS_VOICE_FOCUS_NONE;
            client_request_focus(c, f);
        }
    }
}


static void free_commands(srs_command_t *cmds, int ncmd)
{
    srs_command_t *c;
    int            i, j;

    if (cmds != NULL) {
        for (i = 0, c = cmds; i < ncmd; i++, c++) {
            if (c->tokens != NULL) {
                for (j = 0; j < c->ntoken; j++)
                    mrp_free(c->tokens[j]);

                mrp_free(c->tokens);
            }
        }

        mrp_free(cmds);
    }
}


static int parse_command(srs_command_t *cmd, char *command)
{
    char **tokens, *p, *b, *e;
    int    ntoken, len, osize, nsize, i;

    tokens = NULL;
    ntoken = 0;

    p = command;

    while (*p) {
        b = p;
        while (*b == ' ' || *b == '\t')
            b++;

        e = b;
        while (*e != ' ' && *e != '\t' && *e != '\0')
            e++;

        len   = e - b;
        osize = sizeof(*tokens) *  ntoken;
        nsize = sizeof(*tokens) * (ntoken + 1);

        if (mrp_reallocz(tokens, osize, nsize) == NULL)
            goto fail;

        tokens[ntoken] = mrp_datadup(b, len + 1);

        if (tokens[ntoken] == NULL)
            goto fail;

        tokens[ntoken][len] = '\0';
        ntoken++;

        p = e;
    }

    cmd->tokens = tokens;
    cmd->ntoken = ntoken;

    return TRUE;

 fail:
    for (i = 0; i < ntoken; i++)
        mrp_free(tokens[i]);
    mrp_free(tokens);

    return FALSE;
}


static srs_command_t *parse_commands(char **commands, int ncommand)
{
    srs_command_t *cmds;
    int            i;

    cmds = mrp_allocz_array(typeof(*cmds), ncommand);

    if (cmds != NULL) {
        for (i = 0; i < ncommand; i++)
            if (!parse_command(cmds + i, commands[i]))
                goto fail;
    }

    return cmds;

 fail:
    free_commands(cmds, ncommand);
    return NULL;
}


srs_client_t *client_create(srs_context_t *srs, srs_client_type_t type,
                            const char *name, const char *appclass,
                            char **commands, int ncommand, const char *id,
                            srs_client_ops_t *ops, void *user_data)
{
    srs_client_t *c;

    c = mrp_allocz(sizeof(*c));

    if (c == NULL)
        return NULL;

    mrp_list_init(&c->hook);
    mrp_list_init(&c->voices);
    c->srs       = srs;
    c->type      = type;
    c->ops       = *ops;
    c->user_data = user_data;
    c->name      = mrp_strdup(name);
    c->appclass  = mrp_strdup(appclass);
    c->id        = mrp_strdup(id);

    if (c->name == NULL || c->appclass == NULL || c->id == NULL) {
        client_destroy(c);
        return NULL;
    }

    c->commands = parse_commands(commands, ncommand);
    c->ncommand = ncommand;

    if (c->commands == NULL || srs_srec_add_client(srs, c) != 0) {
        client_destroy(c);
        return NULL;
    }

    if (srs->rctx != NULL)
        c->rset = srs_resctl_create(srs, c->appclass, resource_event, c);

    mrp_list_append(&srs->clients, &c->hook);

    mrp_log_info("created client %s (%s:%s)", c->id, c->appclass, c->name);

    return c;
}


static void purge_voice_requests(srs_client_t *c)
{
    mrp_list_hook_t *p, *n;
    voice_req_t     *req;

    mrp_list_foreach(&c->voices, p, n) {
        req = mrp_list_entry(p, typeof(*req), hook);
        client_cancel_voice(c, req->id);
    }
}


void client_destroy(srs_client_t *c)
{
    if (c != NULL) {
        mrp_log_info("destroying client %s (%s:%s)", c->id,
                     c->appclass, c->name);

        srs_srec_del_client(c->srs, c);
        srs_resctl_destroy(c->rset);
        c->rset = NULL;

        mrp_list_delete(&c->hook);

        mrp_free(c->name);
        mrp_free(c->appclass);
        mrp_free(c->id);

        free_commands(c->commands, c->ncommand);
        purge_voice_requests(c);
    }
}


srs_client_t *client_lookup_by_id(srs_context_t *srs, const char *id)
{
    srs_client_t    *c;
    mrp_list_hook_t *p, *n;

    /* for now we just do a linear search... */

    mrp_list_foreach(&srs->clients, p, n) {
        c = mrp_list_entry(p, typeof(*c), hook);

        if (!strcmp(c->id, id))
            return c;
    }

    return NULL;
}


static const char *focus_string(srs_voice_focus_t focus)
{
    switch (focus) {
    case SRS_VOICE_FOCUS_NONE:      return "none";
    case SRS_VOICE_FOCUS_SHARED:    return "shared";
    case SRS_VOICE_FOCUS_EXCLUSIVE: return "exclusive";
    default:                        return "<invalid>";
    }
}


int client_request_focus(srs_client_t *c, srs_voice_focus_t focus)
{
    mrp_debug("client %s requested %s focus", c->id, focus_string(focus));

    if (c->requested != focus) {
        c->requested = focus;

        if (c->requested != SRS_VOICE_FOCUS_NONE) {
            c->enabled = TRUE;
            c->shared  = focus == SRS_VOICE_FOCUS_SHARED;
            return srs_resctl_acquire(c->rset, c->shared);
        }
        else
            return srs_resctl_release(c->rset);
    }
    else
        mrp_debug("client %s has already the requested %s focus", c->id,
                  focus_string(focus));

    return TRUE;
}


static void notify_focus(srs_client_t *c, int granted)
{
    srs_voice_focus_t focus;

    if (!c->enabled)
        return;

    if (granted & SRS_RESCTL_MASK_SREC) {
        if (c->shared)
            focus = SRS_VOICE_FOCUS_SHARED;
        else
            focus = SRS_VOICE_FOCUS_EXCLUSIVE;
    }
    else
        focus = SRS_VOICE_FOCUS_NONE;

    mrp_log_info("Client %s has %s %svoice focus.", c->id,
                 focus ? "gained" : "lost",
                 focus ? (c->shared ? "shared " : "exclusive ") : "");

    c->ops.notify_focus(c, focus);

    c->granted = granted;
}


static void resource_event(srs_resctl_event_t *e, void *user_data)
{
    srs_client_t *c = (srs_client_t *)user_data;

    if (e->type != SRS_RESCTL_EVENT_RESOURCE)
        return;

    notify_focus(c, e->resource.granted);

    c->granted = e->resource.granted;
}


void client_notify_command(srs_client_t *c, int index,
                           int ntoken, const char **tokens,
                           uint32_t *start, uint32_t *end,
                           srs_audiobuf_t *audio)
{
    if (!c->enabled)
        return;

    if (!(c->granted & SRS_RESCTL_MASK_SREC))
        return;

    if (0 <= index && index < c->ncommand) {
        c->ops.notify_command(c, index, ntoken, (char **)tokens,
                              start, end, audio);
    }
}


static void client_voice_event(srs_voice_event_t *event, void *notify_data)
{
    voice_req_t *req  = (void *)notify_data;
    int          mask = (1 << event->type);
    int          done;

    if (mask & SRS_VOICE_MASK_DONE) {
        mrp_list_delete(&req->hook);
        done = TRUE;
    }
    else
        done = FALSE;

    if (req->notify_events & mask)
        req->c->ops.notify_render(req->c, event);

    if (done)
        mrp_free(req);
}


uint32_t client_render_voice(srs_client_t *c, const char *msg,
                             const char *voice, double rate, double pitch,
                             int timeout, int notify_events)
{
    srs_context_t *srs    = c->srs;
    const char    *tags[] = { "media.role=speech", NULL };
    int            forced = SRS_VOICE_MASK_DONE;
    voice_req_t   *req;

    if ((req = mrp_allocz(sizeof(*req))) == NULL)
        return SRS_VOICE_INVALID;

    mrp_list_init(&req->hook);
    req->c             = c;
    req->notify_events = notify_events;

    if (rate == 0)
        rate = 1;
    if (pitch == 0)
        pitch = 1;

    req->id = srs_render_voice(srs, msg, (char **)tags, voice, rate, pitch,
                               timeout, notify_events | forced,
                               client_voice_event, req);

    if (req->id != SRS_VOICE_INVALID) {
        mrp_list_append(&c->voices, &req->hook);

        return req->id;
    }
    else {
        mrp_free(req);

        return SRS_VOICE_INVALID;
    }
}


void client_cancel_voice(srs_client_t *c, uint32_t id)
{
    srs_context_t   *srs = c->srs;
    mrp_list_hook_t *p, *n;
    voice_req_t     *req;

    mrp_list_foreach(&c->voices, p, n) {
        req = mrp_list_entry(p, typeof(*req), hook);

        if (req->id == id) {
            srs_cancel_voice(srs, id, FALSE);

            mrp_list_delete(&req->hook);
            mrp_free(req);

            return;
        }
    }
}


int client_query_voices(srs_client_t *c, const char *language,
                        srs_voice_actor_t **actorsp)
{
    srs_context_t *srs  = c->srs;
    const char    *lang = language && *language ? language : NULL;

    return srs_query_voices(c->srs, lang, actorsp);
}


void client_free_queried_voices(srs_voice_actor_t *actors)
{
    srs_free_queried_voices(actors);
}
