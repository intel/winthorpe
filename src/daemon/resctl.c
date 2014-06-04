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

#include "srs/daemon/context.h"
#include "srs/daemon/resctl.h"

#define CONNECT_TIMER (5 * 1000)

struct srs_resctx_s {
    srs_context_t         *srs;          /* SRS context */
    mrp_res_context_t     *ctx;          /* resource context */
    mrp_list_hook_t        sets;         /* resource sets */
    srs_resctl_event_cb_t  cb;           /* notification callback */
    void                  *user_data;    /* opaque notification data */
    mrp_timer_t           *t;            /* (re)connect timer */
};


struct srs_resset_s {
    srs_resctx_t           *ctx;         /* resource context */
    mrp_res_resource_set_t *set;         /* resource set */
    mrp_list_hook_t         hook;        /* to list of resource sets */
    srs_resctl_event_cb_t   cb;          /* notification callback */
    void                   *user_data;   /* opaque notification data */
    char                   *appclass;    /* application class */
    int                     shared : 1;  /* whether currently shared */
    mrp_deferred_t         *emul;        /* deferred cb for emulation */
};


static void context_event(mrp_res_context_t *rctx, mrp_res_error_t err,
                          void *user_data);
static void set_event(mrp_res_context_t *rctx,
                      const mrp_res_resource_set_t *rset, void *user_data);
static void stop_connect(srs_resctx_t *ctx);
static void notify_connect(srs_resctx_t *ctx);
static void notify_disconnect(srs_resctx_t *ctx, int requested);

static int emul_acquire(srs_resset_t *set, int shared);
static int emul_release(srs_resset_t *set);

#define CONFIG_SREC  "resource.recognition"
#define DEFAULT_SREC "speech_recognition"
#define CONFIG_SSYN  "resource.synthesis"
#define DEFAULT_SSYN "speech_synthesis"

static const char *name_srec;
static const char *name_ssyn;


static void get_resource_names(srs_cfg_t *cfg)
{
    name_srec = srs_get_string_config(cfg, CONFIG_SREC, DEFAULT_SREC);
    name_ssyn = srs_get_string_config(cfg, CONFIG_SSYN, DEFAULT_SSYN);

    mrp_log_info("Using resource '%s' for speech recognition.", name_srec);
    mrp_log_info("Using resoruce '%s' for speech synthesis.", name_ssyn);
}


static int try_connect(srs_resctx_t *ctx)
{
    srs_context_t *srs = ctx->srs;

    ctx->ctx = mrp_res_create(srs->ml, context_event, ctx);

    if (ctx->ctx != NULL)
        return TRUE;
    else
        return FALSE;
}


static void connect_timer_cb(mrp_timer_t *t, void *user_data)
{
    srs_resctx_t *ctx = (srs_resctx_t *)user_data;

    MRP_UNUSED(t);

    if (try_connect(ctx))
        stop_connect(ctx);
}


static int start_connect(srs_resctx_t *ctx)
{
    srs_context_t *srs = ctx->srs;

    ctx->t = mrp_add_timer(srs->ml, CONNECT_TIMER, connect_timer_cb, ctx);

    return (ctx->t != NULL);
}


static void stop_connect(srs_resctx_t *ctx)
{
    mrp_del_timer(ctx->t);
    ctx->t = NULL;
}


int srs_resctl_connect(srs_context_t *srs, srs_resctl_event_cb_t cb,
                       void *user_data, int reconnect)
{
    srs_resctx_t *ctx;

    if (srs->rctx != NULL)
        return FALSE;

    ctx = mrp_allocz(sizeof(*ctx));

    if (ctx == NULL)
        return FALSE;

    mrp_list_init(&ctx->sets);
    ctx->srs       = srs;
    ctx->cb        = cb;
    ctx->user_data = user_data;
    srs->rctx      = ctx;

    if (try_connect(ctx) || (reconnect && start_connect(ctx)))
        return TRUE;
    else {
        mrp_free(ctx);
        srs->rctx = NULL;
        return FALSE;
    }
}


void srs_resctl_disconnect(srs_context_t *srs)
{
    srs_resctx_t *ctx = srs->rctx;

    if (ctx != NULL) {
        stop_connect(ctx);
        mrp_res_destroy(ctx->ctx);
        ctx->ctx = NULL;
        notify_disconnect(ctx, TRUE);

        mrp_free(ctx);
        srs->rctx = NULL;
    }
}


static void context_event(mrp_res_context_t *rctx, mrp_res_error_t err,
                          void *user_data)
{
    srs_resctx_t  *ctx = (srs_resctx_t *)user_data;
    srs_context_t *srs = ctx->srs;

    MRP_UNUSED(err);

    switch (rctx->state) {
    case MRP_RES_CONNECTED:
        mrp_log_info("Resource context connection is up.");
        stop_connect(ctx);
        notify_connect(ctx);
        break;

    case MRP_RES_DISCONNECTED:
        mrp_log_info("Resource context connection is down.");
        notify_disconnect(ctx, FALSE);
        start_connect(ctx);
    }
}


static void notify_connect(srs_resctx_t *ctx)
{
    srs_resctl_event_t e;

    if (ctx->cb != NULL) {
        e.connection.type = SRS_RESCTL_EVENT_CONNECTION;
        e.connection.up   = TRUE;
        ctx->cb(&e, ctx->user_data);
    }
}


static void notify_disconnect(srs_resctx_t *ctx, int requested)
{
    mrp_list_hook_t    *p, *n;
    srs_resset_t       *set;
    srs_resctl_event_t  e;

    if (!requested) {
        if (ctx->cb != NULL) {
            e.connection.type = SRS_RESCTL_EVENT_CONNECTION;
            e.connection.up   = FALSE;
            ctx->cb(&e, ctx->user_data);
        }
    }

    e.type = SRS_RESCTL_EVENT_DESTROYED;

    mrp_list_foreach(&ctx->sets, p, n) {
        set = mrp_list_entry(p, typeof(*set), hook);

        if (set->cb != NULL)
            set->cb(&e, set->user_data);
    }
}


srs_resset_t *srs_resctl_create(srs_context_t *srs, char *appclass,
                                srs_resctl_event_cb_t cb, void *user_data)
{
    srs_resctx_t *ctx = srs->rctx;
    srs_resset_t *set;
    int           shared;

    set = mrp_allocz(sizeof(*set));

    if (set == NULL)
        return NULL;

    mrp_list_init(&set->hook);
    set->ctx       = ctx;
    set->cb        = cb;
    set->user_data = user_data;
    set->shared    = shared = TRUE;
    set->appclass  = mrp_strdup(appclass);

    if (ctx == NULL || ctx->ctx == NULL || srs_resctl_online(srs, set)) {
        mrp_list_append(&ctx->sets, &set->hook);
        return set;
    }

 fail:
    if (ctx != NULL) {
        if (set->set != NULL)
            mrp_res_delete_resource_set(ctx->ctx, set->set);

        mrp_free(set->appclass);
        mrp_free(set);
    }

    return NULL;
}


void srs_resctl_destroy(srs_resset_t *set)
{
    srs_resctx_t *ctx = set ? set->ctx : NULL;

    if (set != NULL) {
        if (set->emul != NULL) {
            mrp_del_deferred(set->emul);
            set->emul = NULL;
        }

        if (ctx != NULL)
            mrp_res_delete_resource_set(ctx->ctx, set->set);

        mrp_list_delete(&set->hook);
        mrp_free(set->appclass);
        mrp_free(set);
    }
}


int srs_resctl_online(srs_context_t *srs, srs_resset_t *set)
{
    srs_resctx_t *ctx    = srs->rctx;
    int           shared = set->shared;

    if (set == NULL)
        return FALSE;

    if (set->emul != NULL) {
        mrp_del_deferred(set->emul);
        set->emul = NULL;
    }

    set->ctx = ctx;
    set->set = mrp_res_create_resource_set(ctx->ctx, set->appclass,
                                           set_event, set);

    if (set->set == NULL)
        return FALSE;

    if (name_srec == NULL || name_ssyn == NULL)
        get_resource_names(srs->settings);

    if (mrp_res_create_resource(ctx->ctx, set->set, name_srec, TRUE, shared) &&
        mrp_res_create_resource(ctx->ctx, set->set, name_ssyn, TRUE, shared))
        return TRUE;

    mrp_res_delete_resource_set(ctx->ctx, set->set);
    set->set = NULL;

    return FALSE;
}


void srs_resctl_offline(srs_resset_t *set)
{
    if (set != NULL)
        set->set = NULL;
}


int srs_resctl_acquire(srs_resset_t *set, int shared)
{
    srs_resctx_t *ctx = set ? set->ctx : NULL;

    if (ctx == NULL)
        return FALSE;

    if (ctx->ctx == NULL || set->set == NULL)
        return emul_acquire(set, shared);

    if (!!shared != !!set->shared) {
        mrp_res_delete_resource_set(ctx->ctx, set->set);
        set->shared = !!shared;
        set->set    = NULL;

        set->set = mrp_res_create_resource_set(ctx->ctx, set->appclass,
                                               set_event, set);

        if (set->set == NULL)
            goto fail;

        if (!mrp_res_create_resource(ctx->ctx, set->set,
                                     name_srec, TRUE, shared) ||
            !mrp_res_create_resource(ctx->ctx, set->set,
                                     name_ssyn, TRUE, shared))
            goto fail;
    }

    if (mrp_res_acquire_resource_set(ctx->ctx, set->set) == 0)
        return TRUE;
    else
        /* fall through */;

 fail:
    if (set->set != NULL) {
        mrp_res_delete_resource_set(ctx->ctx, set->set);
        set->set = NULL;
    }

    return FALSE;
}


int srs_resctl_release(srs_resset_t *set)
{
    srs_resctx_t *ctx = set ? set->ctx : NULL;

    if (ctx == NULL)
        return FALSE;

    if (ctx->ctx == NULL || set->set == NULL)
        return emul_release(set);

    if (mrp_res_release_resource_set(ctx->ctx, set->set) >= 0)
        return TRUE;
    else
        return FALSE;
}


static void set_event(mrp_res_context_t *rctx,
                      const mrp_res_resource_set_t *rset, void *user_data)
{
    srs_resset_t       *set  = (srs_resset_t *)user_data;
    mrp_res_resource_t *srec, *ssyn;
    srs_resctl_event_t  e;

    srec = mrp_res_get_resource_by_name(rctx, rset, name_srec);
    ssyn = mrp_res_get_resource_by_name(rctx, rset, name_ssyn);

    if (srec == NULL || ssyn == NULL || srec->state != ssyn->state) {
        mrp_log_error("Inconsistent resources in set.");
        return;
    }

    if (set->cb == NULL)
        return;

    e.resource.type    = SRS_RESCTL_EVENT_RESOURCE;
    e.resource.granted = 0;

    if (srec->state)
        e.resource.granted |= SRS_RESCTL_MASK_SREC;
    if (ssyn->state)
        e.resource.granted |= SRS_RESCTL_MASK_SYNT;

    set->cb(&e, set->user_data);
}


static void emul_acquire_cb(mrp_deferred_t *d, void *user_data)
{
    srs_resset_t       *set = (srs_resset_t *)user_data;
    srs_resctl_event_t  e;

    mrp_del_deferred(d);

    if (set->emul == d)
        set->emul = NULL;

    e.resource.type    = SRS_RESCTL_EVENT_RESOURCE;
    e.resource.granted = SRS_RESCTL_MASK_SREC | SRS_RESCTL_MASK_SYNT;

    set->cb(&e, set->user_data);
}


static int emul_acquire(srs_resset_t *set, int shared)
{
    srs_resctx_t *ctx = set ? set->ctx : NULL;

    MRP_UNUSED(shared);

    if (ctx == NULL)
        return FALSE;

    if (set->emul != NULL)
        return FALSE;

    set->emul = mrp_add_deferred(ctx->srs->ml, emul_acquire_cb, set);

    if (set->emul != NULL)
        return TRUE;
    else
        return FALSE;
}


static void emul_release_cb(mrp_deferred_t *d, void *user_data)
{
    srs_resset_t       *set = (srs_resset_t *)user_data;
    srs_resctl_event_t  e;

    mrp_del_deferred(d);

    if (set->emul == d)
        set->emul = NULL;

    e.resource.type    = SRS_RESCTL_EVENT_RESOURCE;
    e.resource.granted = 0;

    set->cb(&e, set->user_data);
}


static int emul_release(srs_resset_t *set)
{
    srs_resctx_t *ctx = set ? set->ctx : NULL;

    if (ctx == NULL)
        return FALSE;

    if (set->emul != NULL)
        return FALSE;

    set->emul = mrp_add_deferred(set->ctx->srs->ml, emul_release_cb, set);

    if (set->emul != NULL)
        return TRUE;
    else
        return FALSE;
}
