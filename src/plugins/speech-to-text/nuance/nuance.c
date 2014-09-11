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

#include "srs/daemon/plugin.h"
#include "srs/daemon/recognizer.h"

#define NUANCE_NAME        "nuance-speech"
#define NUANCE_DESCRIPTION "A Nuance-based speech engine backend plugin."
#define NUANCE_AUTHORS     "Jaska Uimonen <jaska.uimonen@intel.com>"
#define NUANCE_VERSION     "0.0.1"
#define NUANCE_PREFIX      "nuance."

typedef struct {
    srs_plugin_t      *self;             /* us, the backend plugin */
    srs_srec_notify_t  notify;           /* recognition notification callback */
    void              *notify_data;      /* notification callback data */
    int                active;           /* whether we're active */
} nuance_t;


static int nuance_activate(void *user_data)
{
    nuance_t *nua = (nuance_t *)user_data;

    if (!nua->active) {
        mrp_log_info("Activating Nuance backend.");

        nua->active = TRUE;
    }

    return TRUE;
}


static void nuance_deactivate(void *user_data)
{
    nuance_t *nua = (nuance_t *)user_data;

    if (nua->active) {
        mrp_log_info("Deactivating Nuance backend.");

        nua->active = FALSE;
    }
}


static int nuance_flush(uint32_t start, uint32_t end, void *user_data)
{
    nuance_t *nua = (nuance_t *)user_data;

    MRP_UNUSED(nua);

    mrp_debug("flushing Nuance backend buffer (%u - %u)", start, end);

    return TRUE;
}


static int nuance_rescan(uint32_t start, uint32_t end, void *user_data)
{
    nuance_t *nua = (nuance_t *)user_data;

    MRP_UNUSED(nua);

    mrp_debug("scheduling Nuance backend buffer rescan (%u - %u)", start, end);

    return TRUE;
}


static srs_audiobuf_t *nuance_sampledup(uint32_t start, uint32_t end,
                                        void *user_data)
{
    nuance_t          *nua = (nuance_t *)user_data;
    srs_audioformat_t  format;
    uint32_t           rate;
    uint8_t            channels;
    size_t             samples;
    uint32_t           buf[2];

    MRP_UNUSED(nua);

    mrp_debug("duplicating Nuance backend sample (%u - %u)", start, end);

    format   = SRS_AUDIO_S32LE;
    rate     = 16000;
    channels = 2;
    samples  = 1;
    buf[0]   = start;
    buf[1]   = end;

    return srs_create_audiobuf(format, rate, channels, samples, buf);
}


static int nuance_check_decoder(const char *decoder, void *user_data)
{
    nuance_t *nua = (nuance_t *)user_data;

    MRP_UNUSED(nua);

    mrp_debug("checking availability of decoder '%s' for Nuance backend",
              decoder);

    return TRUE;
}


static int nuance_select_decoder(const char *decoder, void *user_data)
{
    nuance_t *nua = (nuance_t *)user_data;

    MRP_UNUSED(nua);

    mrp_debug("setting decoder '%s' for Nuance backend", decoder);

    return TRUE;
}


static int create_nuance(srs_plugin_t *plugin)
{
    srs_srec_api_t api = {
    activate:         nuance_activate,
    deactivate:       nuance_deactivate,
    flush:            nuance_flush,
    rescan:           nuance_rescan,
    sampledup:        nuance_sampledup,
    check_decoder:    nuance_check_decoder,
    select_decoder:   nuance_select_decoder,
    };

    srs_context_t *srs = plugin->srs;
    nuance_t      *nua;

    mrp_debug("creating Nuance speech recognition backend plugin");

    nua = mrp_allocz(sizeof(*nua));

    if (nua != NULL) {
        nua->self = plugin;

        if (srs_register_srec(srs, NUANCE_NAME, &api, nua,
                              &nua->notify, &nua->notify_data) == 0) {
            plugin->plugin_data = nua;

            return TRUE;
        }

        mrp_free(nua);
    }

    mrp_log_error("Failed to create Nuance plugin.");

    return FALSE;
}


static int config_nuance(srs_plugin_t *plugin, srs_cfg_t *settings)
{
    nuance_t  *nua = (nuance_t *)plugin->plugin_data;
    srs_cfg_t *cfg;
    int        n, i;

    MRP_UNUSED(nua);

    mrp_debug("configuring Nuance speech recognition backend plugin");

    n = srs_config_collect(settings, NUANCE_PREFIX, &cfg);

    mrp_log_info("Found %d Nuance plugin configuration keys.", n);
    for (i = 0; i < n; i++)
        mrp_log_info("    %s = %s", cfg[i].key, cfg[i].value);

    srs_config_free(cfg);

    return TRUE;
}


static int start_nuance(srs_plugin_t *plugin)
{
    nuance_t *nua = (nuance_t *)plugin->plugin_data;

    MRP_UNUSED(nua);

    mrp_debug("start Nuance speech recognition backend plugin");

    return TRUE;
}


static void stop_nuance(srs_plugin_t *plugin)
{
    nuance_t *nua = (nuance_t *)plugin->plugin_data;

    MRP_UNUSED(nua);

    mrp_debug("stop Nuance speech recognition backend plugin");
}


static void destroy_nuance(srs_plugin_t *plugin)
{
    srs_context_t *srs = plugin->srs;
    nuance_t      *nua = (nuance_t *)plugin->plugin_data;

    mrp_debug("destroy Nuance speech recognition backend plugin");

    if (nua != NULL) {
        srs_unregister_srec(srs, NUANCE_NAME);
        mrp_free(nua);
    }
}


SRS_DECLARE_PLUGIN(NUANCE_NAME, NUANCE_DESCRIPTION, NUANCE_AUTHORS,
                   NUANCE_VERSION, create_nuance, config_nuance,
                   start_nuance, stop_nuance, destroy_nuance)
