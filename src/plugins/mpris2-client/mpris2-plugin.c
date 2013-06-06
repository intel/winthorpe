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
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>


#include <murphy/common/debug.h>
#include <murphy/common/mainloop.h>

#include "dbusif.h"
#include "clients.h"


#define PLUGIN_DESCRIPTION "Mpris2 client to drive various media players."
#define PLUGIN_AUTHORS     "Janos Kovacs <janos.kovacs@intel.com>"
#define PLUGIN_VERSION     "0.0.1"



static int create_mpris2(srs_plugin_t *plugin)
{
    srs_context_t *srs = plugin->srs;
    context_t     *ctx = NULL;

    mrp_debug("creating Mpris2 client plugin");

    if ((ctx = mrp_allocz(sizeof(context_t)))) {
        ctx->plugin = plugin;

        if (dbusif_create(ctx, srs->ml) == 0 &&
            clients_create(ctx)         == 0  )
        {
            plugin->plugin_data = ctx;
            return TRUE;
        }

        mrp_free(ctx);
    }

    mrp_log_error("Failed to create Mpris2 client plugin.");

    return FALSE;
}


static int config_mpris2(srs_plugin_t *plugin, srs_cfg_t *settings)
{
    context_t *ctx = (context_t *)plugin->plugin_data;
    srs_cfg_t *cfgs, *c, *s;
    const char *service;
    const char *object;
    char srv[256];
    char obj[256];
    int pfxlen;
    int n, i, j, l;
    int success;

    mrp_debug("configuring Mpris2 client plugin");

    n = srs_collect_config(settings, MPRIS2_PREFIX, &cfgs);
    pfxlen = strlen(MPRIS2_PREFIX);

    mrp_log_info("Found %d Mpris2 configuration keys.", n);

    for (i = 0, success = TRUE;   i < n ;   i++) {
        c = cfgs + i;

        if (!strncmp("player", c->key + pfxlen, 6)) {
            snprintf(srv, sizeof(srv), "%s%s", c->value, ".service");
            snprintf(obj, sizeof(obj), "%s%s", c->value, ".object");
            for (j = 0, service = object = NULL;
                 j < n && (!service || !object);
                 j++)
            {
                s = cfgs + j;
                if (!strcmp(srv, s->key + pfxlen))
                    service = s->value;
                else if (!strcmp(obj, s->key + pfxlen))
                    object = s->value;
            }

            clients_register_player(ctx, c->value, service, object);
        }
        else {
            l = strlen(c->key);

            if (!(l > 8 && !strcmp(c->key + (l - 8), ".service")) ||
                !(l > 7 && !strcmp(c->key + (l - 7), ".object" ))  )
            {
                c->used = FALSE;
                success = FALSE;
            }
        }
    }

    srs_free_config(cfgs);

    return TRUE;
}


static int start_mpris2(srs_plugin_t *plugin)
{
    context_t *ctx = (context_t *)plugin->plugin_data;

    mrp_debug("start Mpris2 client plugin");

    clients_start(ctx);

    return TRUE;
}


static void stop_mpris2(srs_plugin_t *plugin)
{
    context_t *ctx = (context_t *)plugin->plugin_data;

    mrp_debug("stop mpris2 client plugin");

    clients_stop(ctx);
}


static void destroy_mpris2(srs_plugin_t *plugin)
{
    context_t *ctx = (context_t *)plugin->plugin_data;

    mrp_debug("destroy Mpris2 client plugin");

    clients_destroy(ctx);
    dbusif_destroy(ctx);
}



SRS_DECLARE_PLUGIN(PLUGIN_NAME, PLUGIN_DESCRIPTION, PLUGIN_AUTHORS,
                   PLUGIN_VERSION, create_mpris2, config_mpris2,
                   start_mpris2, stop_mpris2, destroy_mpris2)


/*
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 *
 */
