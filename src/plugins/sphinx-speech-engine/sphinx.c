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

#include "src/daemon/plugin.h"
#include "src/daemon/recognizer.h"

#define SPHINX_NAME        "sphinx-speech"
#define SPHINX_DESCRIPTION "A CMU Sphinx-based speech engine backend plugin."
#define SPHINX_AUTHORS     "Janos Kovacs <janos.kovacs@intel.com>"
#define SPHINX_VERSION     "0.0.1"
#define SPHINX_PREFIX      "sphinx."

typedef struct {
    srs_plugin_t      *self;             /* us, the backend plugin */
    srs_srec_notify_t  notify;           /* recognition notification callback */
    void              *notify_data;      /* notifiation callback data */
    int                active;           /* whether we're active */
} sphinx_t;


static int sphinx_activate(void *user_data)
{
    sphinx_t *spx = (sphinx_t *)user_data;

    if (!spx->active) {
        mrp_log_info("Activating CMU Sphinx backend.");

        spx->active = TRUE;
    }

    return TRUE;
}


static void sphinx_deactivate(void *user_data)
{
    sphinx_t *spx = (sphinx_t *)user_data;

    if (spx->active) {
        mrp_log_info("Deactivating CMU Sphinx backend.");

        spx->active = FALSE;
    }
}


static int sphinx_flush(uint32_t start, uint32_t end, void *user_data)
{
    sphinx_t *spx = (sphinx_t *)user_data;

    MRP_UNUSED(spx);

    mrp_debug("flushing CMU Sphinx backend buffer (%u - %u)", start, end);

    return TRUE;
}


static int sphinx_rescan(uint32_t start, uint32_t end, void *user_data)
{
    sphinx_t *spx = (sphinx_t *)user_data;

    MRP_UNUSED(spx);

    mrp_debug("scheduling CMU Sphinx backend buffer rescan (%u - %u)",
              start, end);

    return TRUE;
}


static void *sphinx_sampledup(uint32_t start, uint32_t end, void *user_data)
{
    sphinx_t *spx = (sphinx_t *)user_data;

    MRP_UNUSED(spx);

    mrp_debug("duplicating CMU Sphinx backend samples (%u - %u)", start, end);


    return (void *)4;
}


static int sphinx_check_model(const char *model, void *user_data)
{
    sphinx_t *spx = (sphinx_t *)user_data;

    MRP_UNUSED(spx);

    mrp_debug("checking model '%s' for CMU Sphinx backend", model);

    return TRUE;
}


static int sphinx_check_dictionary(const char *dictionary, void *user_data)
{
    sphinx_t *spx = (sphinx_t *)user_data;

    MRP_UNUSED(spx);

    mrp_debug("checking dictionary '%s' for CMU Sphinx backend", dictionary);

    return TRUE;
}


static int sphinx_set_model(const char *model, void *user_data)
{
    sphinx_t *spx = (sphinx_t *)user_data;

    MRP_UNUSED(spx);

    mrp_debug("setting model '%s' for CMU Sphinx backend", model);

    return TRUE;
}


static int sphinx_set_dictionary(const char *dictionary, void *user_data)
{
    sphinx_t *spx = (sphinx_t *)user_data;

    MRP_UNUSED(spx);

    mrp_debug("setting dictionary '%s' for CMU Sphinx backend", dictionary);

    return TRUE;
}


static int create_sphinx(srs_plugin_t *plugin)
{
    srs_srec_api_t api = {
    activate:         sphinx_activate,
    deactivate:       sphinx_deactivate,
    flush:            sphinx_flush,
    rescan:           sphinx_rescan,
    sampledup:        sphinx_sampledup,
    check_model:      sphinx_check_model,
    check_dictionary: sphinx_check_dictionary,
    set_model:        sphinx_set_model,
    set_dictionary:   sphinx_set_dictionary,
    };

    srs_context_t *srs = plugin->srs;
    sphinx_t      *spx;

    mrp_debug("creating CMU Sphinx speech recognition backend plugin");

    spx = mrp_allocz(sizeof(*spx));

    if (spx != NULL) {
        spx->self = plugin;

        if (srs_register_srec(srs, SPHINX_NAME, &api, spx,
                              &spx->notify, &spx->notify_data) == 0) {
            plugin->plugin_data = spx;

            return TRUE;
        }

        mrp_free(spx);
    }

    mrp_log_error("Failed to create CMU Sphinx plugin.");

    return FALSE;
}


static int config_sphinx(srs_plugin_t *plugin, srs_cfg_t *settings)
{
    sphinx_t  *spx = (sphinx_t *)plugin->plugin_data;
    srs_cfg_t *cfg;
    int        n, i;

    MRP_UNUSED(spx);

    mrp_debug("configuring CMU Sphinx speech recognition backend plugin");

    n = srs_collect_config(settings, SPHINX_PREFIX, &cfg);

    mrp_log_info("Found %d CMU Sphinx plugin configuration keys.", n);
    for (i = 0; i < n; i++)
        mrp_log_info("    %s = %s", cfg[i].key, cfg[i].value);

    srs_free_config(cfg);

    return TRUE;
}


static int start_sphinx(srs_plugin_t *plugin)
{
    sphinx_t *spx = (sphinx_t *)plugin->plugin_data;

    MRP_UNUSED(spx);

    mrp_debug("start CMU Sphinx speech recognition backend plugin");

    return TRUE;
}


static void stop_sphinx(srs_plugin_t *plugin)
{
    sphinx_t *spx = (sphinx_t *)plugin->plugin_data;

    MRP_UNUSED(spx);

    mrp_debug("stop CMU Sphinx speech recognition backend plugin");
}


static void destroy_sphinx(srs_plugin_t *plugin)
{
    srs_context_t *srs = plugin->srs;
    sphinx_t      *spx = (sphinx_t *)plugin->plugin_data;

    mrp_debug("destroy CMU Sphinx speech recognition backend plugin");

    if (spx != NULL) {
        srs_unregister_srec(srs, SPHINX_NAME);
        mrp_free(spx);
    }
}


SRS_DECLARE_PLUGIN(SPHINX_NAME, SPHINX_DESCRIPTION, SPHINX_AUTHORS,
                   SPHINX_VERSION, create_sphinx, config_sphinx,
                   start_sphinx, stop_sphinx, destroy_sphinx)
