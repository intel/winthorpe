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
#include "pulseif.h"
#include "clients.h"


#define PLUGIN_DESCRIPTION "Bluetooth voice recognition for external devices"
#define PLUGIN_AUTHORS     "Janos Kovacs <janos.kovacs@intel.com>"
#define PLUGIN_VERSION     "0.0.1"



static int create_bt_voicerec(srs_plugin_t *plugin)
{
    context_t     *ctx = NULL;

    mrp_debug("creating bluetooth voice recognition client plugin");

    if ((ctx = mrp_allocz(sizeof(context_t)))) {
        ctx->plugin = plugin;

        if (clients_create(ctx) == 0)
        {
            plugin->plugin_data = ctx;
            return TRUE;
        }

        mrp_free(ctx);
    }

    mrp_log_error("Failed to create bluetooth voice "
                  "recognition client plugin.");

    return FALSE;
}


static int config_bt_voicerec(srs_plugin_t *plugin, srs_cfg_t *settings)
{
    srs_cfg_t *cfgs, *c;
    srs_context_t *srs_ctx = plugin->srs;
    context_t *ctx = (context_t *)plugin->plugin_data;
    int pfxlen;
    int n, i;

    mrp_debug("configuring bluetooth voice recognition client plugin");


    n = srs_config_collect(settings, BLUETOOTH_PREFIX, &cfgs);
    pfxlen = strlen(BLUETOOTH_PREFIX);

    mrp_log_info("Found %d bluetooth voice recognition configuration keys.",n);

    for (i = 0;   i < n ;   i++) {
        const char *key = NULL;

        c = cfgs + i;
        key = c->key + pfxlen;

        c->used = FALSE;
        mrp_debug("     '%s=%s'", key, c->value);
    }

    srs_config_free(cfgs);

    dbusif_create(ctx, srs_ctx->ml);

    return TRUE;
}


static int start_bt_voicerec(srs_plugin_t *plugin)
{
    srs_context_t *srs = plugin->srs;
    context_t *ctx = (context_t *)plugin->plugin_data;

    mrp_debug("start bluetooth voice recognition client plugin");

    if (clients_start(ctx)           < 0 ||
        pulseif_create(ctx, srs->pa) < 0 ||
        dbusif_start(ctx)            < 0  )
    {
        mrp_log_error("Failed to start bluetooth voice "
                      "recognition client plugin.");
        return FALSE;
    }

    return TRUE;
}


static void stop_bt_voicerec(srs_plugin_t *plugin)
{
    context_t *ctx = (context_t *)plugin->plugin_data;

    mrp_debug("stop bluetooth voice recognition client plugin");

    pulseif_destroy(ctx);
    dbusif_stop(ctx);
    clients_stop(ctx);
}


static void destroy_bt_voicerec(srs_plugin_t *plugin)
{
    context_t *ctx = (context_t *)plugin->plugin_data;

    mrp_debug("destroy bluetooth voice recognition client plugin");

    dbusif_destroy(ctx);
    clients_destroy(ctx);
}



SRS_DECLARE_PLUGIN(PLUGIN_NAME, PLUGIN_DESCRIPTION, PLUGIN_AUTHORS,
                   PLUGIN_VERSION, create_bt_voicerec, config_bt_voicerec,
                   start_bt_voicerec, stop_bt_voicerec, destroy_bt_voicerec)


/*
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 *
 */
