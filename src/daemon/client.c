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

#include "src/daemon/resourceif.h"
#include "src/daemon/client.h"


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
    mrp_list_hook_t *p, *n;
    srs_client_t    *c;

    mrp_list_foreach(&srs->clients, p, n) {
        c = mrp_list_entry(p, typeof(*c), hook);
        resource_create(c);
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
                            srs_client_ops_t *ops)
{
    srs_client_t *c;

    c = mrp_allocz(sizeof(*c));

    if (c == NULL)
        return NULL;

    mrp_list_init(&c->hook);
    c->srs  = srs;
    c->type = type;
    c->ops  = ops;

    c->name     = mrp_strdup(name);
    c->appclass = mrp_strdup(appclass);
    c->id       = mrp_strdup(id);

    if (c->name == NULL || c->appclass == NULL || c->id == NULL) {
        client_destroy(c);
        return NULL;
    }

    c->commands = parse_commands(commands, ncommand);
    c->ncommand = ncommand;

    if (c->commands == NULL) {
        client_destroy(c);
        return NULL;
    }

    if (srs->rctx != NULL)
        resource_create(c);

    mrp_list_append(&srs->clients, &c->hook);

    mrp_log_info("created client %s (%s:%s)", c->id, c->appclass, c->name);

    return c;
}


void client_destroy(srs_client_t *c)
{
    if (c != NULL) {
        mrp_log_info("destroying client %s (%s:%s)", c->id,
                     c->appclass, c->name);

        resource_destroy(c);

        mrp_list_delete(&c->hook);

        mrp_free(c->name);
        mrp_free(c->appclass);
        mrp_free(c->id);

        free_commands(c->commands, c->ncommand);
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

    if (c->focus != focus) {
        c->focus = focus;

        if (c->focus != SRS_VOICE_FOCUS_NONE)
            c->enabled = TRUE;

        if (c->focus != SRS_VOICE_FOCUS_NONE)
            return resource_acquire(c);
        else
            return resource_release(c);
    }

    return TRUE;
}


static int notify_focus(srs_client_t *c)
{
    if (!c->enabled)
        return TRUE;

    if (!c->allowed)
        mrp_log_info("Client %s has lost voice focus.", c->id);
    else
        mrp_log_info("Client %s has gained %svoice focus.", c->id,
                     c->focus == SRS_VOICE_FOCUS_SHARED ?
                     "shared " : "exclusive ");

    c->ops->notify_focus(c, c->focus);

    return TRUE;
}


void client_resource_event(srs_client_t *c, srs_resset_event_t e)
{
    switch (e) {
    case SRS_RESSET_GRANTED:
        c->allowed = TRUE;
        break;

    case SRS_RESSET_RELEASED:
        c->allowed = FALSE;
        break;

    default:
        return;
    }

    notify_focus(c);
}
