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

#include <murphy/common/debug.h>
#include <murphy/common/mainloop.h>

#include <pulse/pulseaudio.h>
#include <pulse/mainloop.h>

#include "src/daemon/plugin.h"
#include "src/daemon/recognizer.h"

#include "options.h"
#include "decoder-set.h"
#include "utterance.h"
#include "filter-buffer.h"
#include "input-buffer.h"
#include "pulse-interface.h"


#define SPHINX_NAME        "sphinx-speech"
#define SPHINX_DESCRIPTION "A CMU Sphinx-based speech engine backend plugin."
#define SPHINX_AUTHORS     "Janos Kovacs <janos.kovacs@intel.com>"
#define SPHINX_VERSION     "0.0.1"
#define SPHINX_PREFIX      "sphinx."

struct plugin_s {
    srs_plugin_t *self;               /* us, the backend plugin */
    struct {
        srs_srec_notify_t callback;   /* recognition notification callback */
        void *data;                   /* notifiation callback data */
    } notify;
};


int32_t plugin_utterance_handler(context_t *ctx, utterance_t *utt)
{
    int32_t length = utt->length ? utt->length : -1;

    return length;
}

static int activate(void *user_data)
{
    context_t *ctx = (context_t *)user_data;

    MRP_UNUSED(ctx);

    mrp_log_info("Activating CMU Sphinx backend.");

    return TRUE;
}


static void deactivate(void *user_data)
{
    context_t *ctx = (context_t *)user_data;

    MRP_UNUSED(ctx);

    mrp_log_info("Deactivating CMU Sphinx backend.");
}


static int flush(uint32_t start, uint32_t end, void *user_data)
{
    context_t *ctx = (context_t *)user_data;

    MRP_UNUSED(ctx);

    mrp_debug("flushing CMU Sphinx backend buffer (%u - %u)", start, end);

    return TRUE;
}


static int rescan(uint32_t start, uint32_t end, void *user_data)
{
    context_t *ctx = (context_t *)user_data;

    MRP_UNUSED(ctx);

    mrp_debug("scheduling CMU Sphinx backend buffer rescan (%u - %u)",
              start, end);

    return TRUE;
}


static void *sampledup(uint32_t start, uint32_t end, void *user_data)
{
    context_t *ctx = (context_t *)user_data;

    MRP_UNUSED(ctx);

    mrp_debug("duplicating CMU Sphinx backend samples (%u - %u)", start, end);


    return (void *)4;
}


static int check_decoder(const char *decoder, void *user_data)
{
    context_t *ctx = (context_t *)user_data;

    MRP_UNUSED(ctx);

    mrp_debug("checking availability of decoder '%s' for CMU Sphinx backend",
              decoder);

    return TRUE;
}


static int select_decoder(const char *decoder, void *user_data)
{
    context_t *ctx = (context_t *)user_data;

    MRP_UNUSED(ctx);

    mrp_debug("selecting decoder '%s' for CMU Sphinx backend", decoder);

    return TRUE;
}

static int create_sphinx(srs_plugin_t *plugin)
{
    srs_srec_api_t api = {
    activate:         activate,
    deactivate:       deactivate,
    flush:            flush,
    rescan:           rescan,
    sampledup:        sampledup,
    check_decoder:    check_decoder,
    select_decoder:   select_decoder,
    };

    srs_context_t *srs = plugin->srs;
    context_t     *ctx = NULL;
    plugin_t      *pl = NULL;
    int            sts;

    mrp_debug("creating CMU Sphinx speech recognition backend plugin");

    if ((ctx = mrp_allocz(sizeof(context_t))) &&
        (pl  = mrp_allocz(sizeof(plugin_t)))   )
    {
        ctx->plugin = pl;

        pl->self = plugin;

        sts = srs_register_srec(srs, SPHINX_NAME, &api, ctx,
                                &pl->notify.callback,
                                &pl->notify.data);
        if (sts == 0) {
            plugin->plugin_data = ctx;
            return TRUE;
        }
    }

    mrp_free(pl);
    mrp_free(ctx);

    mrp_log_error("Failed to create CMU Sphinx plugin.");

    return FALSE;
}


static int config_sphinx(srs_plugin_t *plugin, srs_cfg_t *settings)
{
    context_t *ctx = (context_t *)plugin->plugin_data;
    srs_cfg_t *cfg;
    int        n, i;

    mrp_debug("configuring CMU Sphinx speech recognition backend plugin");

    n = srs_collect_config(settings, SPHINX_PREFIX, &cfg);

    mrp_log_info("Found %d CMU Sphinx plugin configuration keys.", n);

    if (options_create(ctx, n, cfg) < 0 ||
        decoder_set_create(ctx)     < 0 ||
        filter_buffer_create(ctx)   < 0 ||
        input_buffer_create(ctx)    < 0  )
    {
        mrp_log_error("Failed to configure CMU Sphinx plugin.");
        return FALSE;
    }

    srs_free_config(cfg);

    return TRUE;
}


static int start_sphinx(srs_plugin_t *plugin)
{
    srs_context_t *srs = plugin->srs;
    context_t *ctx = (context_t *)plugin->plugin_data;

    mrp_debug("start CMU Sphinx speech recognition backend plugin");

    if (pulse_interface_create(ctx, srs->pa) < 0) {
        mrp_log_error("Failed to start CMU Sphinx plugin: can't create "
                      "pulseaudio interface");
    }

    return TRUE;
}


static void stop_sphinx(srs_plugin_t *plugin)
{
    context_t *ctx = (context_t *)plugin->plugin_data;

    mrp_debug("stop CMU Sphinx speech recognition backend plugin");

    pulse_interface_destroy(ctx);
}


static void destroy_sphinx(srs_plugin_t *plugin)
{
    srs_context_t *srs = plugin->srs;
    context_t     *ctx = (context_t *)plugin->plugin_data;

    mrp_debug("destroy CMU Sphinx speech recognition backend plugin");

    if (ctx != NULL) {
        srs_unregister_srec(srs, SPHINX_NAME);
        mrp_free(ctx->plugin);

        input_buffer_destroy(ctx);
        filter_buffer_destroy(ctx);
        decoder_set_destroy(ctx);
        options_destroy(ctx);

        mrp_free(ctx);
    }
}


SRS_DECLARE_PLUGIN(SPHINX_NAME, SPHINX_DESCRIPTION, SPHINX_AUTHORS,
                   SPHINX_VERSION, create_sphinx, config_sphinx,
                   start_sphinx, stop_sphinx, destroy_sphinx)


/*
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 *
 */
