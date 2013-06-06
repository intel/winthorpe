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

#include <errno.h>
#include <limits.h>
#include <dlfcn.h>

#include <murphy/common/mm.h>

#include "src/daemon/context.h"
#include "src/daemon/plugin.h"

static srs_plugin_t *find_plugin(srs_context_t *srs, const char *name)
{
    srs_plugin_t    *plugin;
    mrp_list_hook_t *p, *n;

    mrp_list_foreach(&srs->plugins, p, n) {
        plugin = mrp_list_entry(p, typeof(*plugin), hook);

        if (!strcmp(plugin->name, name))
            return plugin;
    }

    return NULL;
}


srs_plugin_t *srs_create_plugin(srs_context_t *srs, const char *name)
{
    srs_plugin_query_t  query;
    const char         *plugin_name, *description, *authors, *version;
    int                 srs_version;
    srs_plugin_api_t   *api;
    srs_plugin_t       *plugin;
    char                path[PATH_MAX];
    void               *h;

    if (find_plugin(srs, name) != NULL) {
        mrp_log_error("Plugin '%s' already exists.", name);
        errno = EEXIST;
        return NULL;
    }

    if (snprintf(path, sizeof(path), "%s/plugin-%s.so",
                 srs->plugin_dir, name) >= (int)sizeof(path)) {
        errno = EOVERFLOW;
        return NULL;
    }

    mrp_log_info("Loading plugin '%s' (%s)...", name, path);

    plugin = NULL;
    h      = dlopen(path, RTLD_LAZY | RTLD_LOCAL);

    if (h == NULL)
        goto fail;

    plugin = mrp_allocz(sizeof(*plugin));

    if (plugin == NULL)
        goto fail;

    mrp_list_init(&plugin->hook);
    plugin->h = h;
    query     = dlsym(h, SRS_DESCRIBE_PLUGIN_FUNC);

    if (query == NULL) {
        mrp_log_error("Invalid plugin %s (does not export symbol '%s').",
                      path, SRS_DESCRIBE_PLUGIN_FUNC);
        goto fail;
    }

    api = query(&plugin_name, &description, &authors, &version, &srs_version);

    if (api == NULL) {
        mrp_log_error("Invalid plugin %s (provided NULL API).", path);
        goto fail;
    }

    mrp_log_info("Plugin query gave:");
    mrp_log_info("    name:        %s", plugin_name);
    mrp_log_info("    description: %s", description);
    mrp_log_info("    authors:     %s", authors);
    mrp_log_info("    version:     %s", version);

    if (srs_version != SRS_PLUGIN_API_VERSION) {
        mrp_log_error("Plugin %s uses incompatible API version (0x%x != 0x%x).",
                      name, srs_version, SRS_PLUGIN_API_VERSION);
        goto fail;
    }

    plugin->srs         = srs;
    plugin->name        = mrp_strdup(name);
    plugin->description = description;
    plugin->authors     = authors;
    plugin->api         = api;

    if (!plugin->api->create(plugin)) {
        mrp_log_error("Failed to create plugin '%s'.", name);
        goto fail;
    }

    mrp_list_append(&srs->plugins, &plugin->hook);

    return plugin;

 fail:
    if (plugin != NULL)
        srs_destroy_plugin(plugin);
    else
        if (h != NULL)
            dlclose(h);

    return NULL;
}


int srs_configure_plugin(srs_plugin_t *plugin, srs_cfg_t *settings)
{
    if (plugin != NULL) {
        mrp_log_info("Configuring plugin '%s'.", plugin->name);
        return plugin->api->config(plugin, settings);
    }
    else
        return FALSE;
}


int srs_start_plugin(srs_plugin_t *plugin)
{
    if (plugin != NULL) {
        mrp_log_info("Starting plugin '%s'.", plugin->name);
        return plugin->api->start(plugin);
    }
    else
        return FALSE;
}


void srs_stop_plugin(srs_plugin_t *plugin)
{
    if (plugin != NULL) {
        mrp_log_info("Stopping plugin '%s'.", plugin->name);
        plugin->api->stop(plugin);
    }
}


void srs_destroy_plugin(srs_plugin_t *plugin)
{
    if (plugin != NULL) {
        mrp_log_info("Destroying plugin '%s'.", plugin->name);

        mrp_list_delete(&plugin->hook);
        plugin->api->destroy(plugin);

        mrp_free(plugin->name);
        mrp_free(plugin);
    }
}


int srs_configure_plugins(srs_context_t *srs)
{
    srs_plugin_t    *plugin;
    mrp_list_hook_t *p, *n;

    mrp_list_foreach(&srs->plugins, p, n) {
        plugin = mrp_list_entry(p, typeof(*plugin), hook);

        if (!srs_configure_plugin(plugin, srs->settings))
            return FALSE;
    }

    return TRUE;
}


int srs_start_plugins(srs_context_t *srs)
{
    srs_plugin_t    *plugin;
    mrp_list_hook_t *p, *n;

    mrp_list_foreach(&srs->plugins, p, n) {
        plugin = mrp_list_entry(p, typeof(*plugin), hook);

        if (!srs_start_plugin(plugin))
            return FALSE;
    }

    return TRUE;
}


void srs_stop_plugins(srs_context_t *srs)
{
    srs_plugin_t    *plugin;
    mrp_list_hook_t *p, *n;

    mrp_list_foreach(&srs->plugins, p, n) {
        plugin = mrp_list_entry(p, typeof(*plugin), hook);

        srs_stop_plugin(plugin);
    }
}


void srs_destroy_plugins(srs_context_t *srs)
{
    srs_plugin_t    *plugin;
    mrp_list_hook_t *p, *n;

    mrp_list_foreach(&srs->plugins, p, n) {
        plugin = mrp_list_entry(p, typeof(*plugin), hook);

        srs_destroy_plugin(plugin);
    }
}
