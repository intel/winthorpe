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

#include "src/daemon/client.h"
#include "src/daemon/resourceif.h"

#define RECONNECT_TIMER 5000
#define RESOURCE        "speech_recognition"

static int start_reconnect(srs_context_t *srs);
static void stop_reconnect(srs_context_t *srs);
static int try_connect(srs_context_t *srs);

static void reconnect(mrp_timer_t *t, void *user_data)
{
    srs_context_t *srs = (srs_context_t *)user_data;

    MRP_UNUSED(t);

    if (try_connect(srs))
        stop_reconnect(srs);
}


static int start_reconnect(srs_context_t *srs)
{
    srs->rtmr = mrp_add_timer(srs->ml, RECONNECT_TIMER, reconnect, srs);

    if (srs->rtmr != NULL)
        return TRUE;
    else
        return FALSE;
}


static void stop_reconnect(srs_context_t *srs)
{
    mrp_del_timer(srs->rtmr);
    srs->rtmr = NULL;
}


static void resctx_event(mrp_res_context_t *ctx, mrp_res_error_t err,
                         void *user_data)
{
    srs_context_t *srs = (srs_context_t *)user_data;

    MRP_UNUSED(err);

    switch (ctx->state) {
    case MRP_RES_CONNECTED:
        mrp_log_info("Resource context connection is up.");
        stop_reconnect(srs);
        break;

    case MRP_RES_DISCONNECTED:
        mrp_log_info("Resource context connection is down.");
        client_reset_resources(srs);
        start_reconnect(srs);
    }
}


static int try_connect(srs_context_t *srs)
{
    srs->rctx = mrp_res_create(srs->ml, resctx_event, srs);

    if (srs->rctx != NULL)
        return TRUE;
    else
        return FALSE;
}


int resource_connect(srs_context_t *srs)
{
    if (try_connect(srs) || start_reconnect(srs))
        return TRUE;
    else
        return FALSE;
}


void resource_disconnect(srs_context_t *srs)
{
    stop_reconnect(srs);

    if (srs->rctx != NULL) {
        mrp_res_destroy(srs->rctx);
        srs->rctx = NULL;
    }
}


static void resource_event(mrp_res_context_t *rctx,
                           const mrp_res_resource_set_t *rset, void *user_data)
{
    srs_client_t       *c = (srs_client_t *)user_data;
    mrp_res_resource_t *r = mrp_res_get_resource_by_name(rctx, rset, RESOURCE);
    srs_resset_event_t  e;

    mrp_debug("received resource event for set %p", rset);

    if (r != NULL) {
        switch (r->state) {
        case MRP_RES_RESOURCE_ACQUIRED:
            e = SRS_RESSET_GRANTED;
            break;
        case MRP_RES_RESOURCE_LOST:
            e = SRS_RESSET_RELEASED;
            break;
        default:
            return;
        }

        client_resource_event(c, e);
    }
}


int resource_create(srs_client_t *c)
{
    mrp_res_context_t      *rctx = c->srs->rctx;
    mrp_res_resource_set_t *rset;
    bool                    shared;

    rset = mrp_res_create_resource_set(rctx, c->appclass, resource_event, c);

    if (rset != NULL) {
        c->rset = rset;
        shared  = (c->focus != SRS_VOICE_FOCUS_EXCLUSIVE);

        if (!mrp_res_create_resource(rctx, rset, RESOURCE, TRUE, shared)) {
            resource_destroy(c);

            return FALSE;
        }

        if (c->focus != SRS_VOICE_FOCUS_NONE) {
            c->shared = !!shared;
            if (!resource_acquire(c)) {
                resource_destroy(c);

                return FALSE;
            }
            else
                return TRUE;
        }
    }

    return FALSE;
}


void resource_destroy(srs_client_t *c)
{
    if (c->srs->rctx != NULL && c->rset != NULL)
        mrp_res_delete_resource_set(c->srs->rctx, c->rset);

    c->rset   = NULL;
    c->shared =  0;
}


int resource_acquire(srs_client_t *c)
{
    if (c->srs->rctx == NULL)
        return FALSE;

    if (c->rset == NULL && !resource_create(c))
        return FALSE;

    if (c->focus != SRS_VOICE_FOCUS_SHARED && c->shared) {
        resource_destroy(c);
        return resource_create(c);
    }

    if (mrp_res_acquire_resource_set(c->srs->rctx, c->rset) >= 0)
        return TRUE;
    else
        return FALSE;
}


int resource_release(srs_client_t *c)
{
    if (c->srs->rctx == NULL || c->rset == NULL)
        return FALSE;

    if (mrp_res_release_resource_set(c->srs->rctx, c->rset) >= 0)
        return TRUE;
    else
        return FALSE;
}
