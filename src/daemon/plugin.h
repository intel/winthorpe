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

#ifndef __SRS_DAEMON_PLUGIN_H__
#define __SRS_DAEMON_PLUGIN_H__

#include "src/daemon/context.h"

#define SRS_PLUGIN_API_VERSION ((0 << 24) | (0 << 16) | 1)

/* Type definition for an SRS plugin. */
typedef struct srs_plugin_s     srs_plugin_t;
typedef struct srs_plugin_api_s srs_plugin_api_t;

/*
 * SRS plugin API functions
 */
typedef srs_plugin_api_t *(*srs_plugin_query_t)(const char **name,
                                                const char **description,
                                                const char **authors,
                                                const char **version,
                                                int         *srs_version);

struct srs_plugin_api_s {
    /* perform basic plugin initialization, memory allocations, etc. */
    int  (*create)(srs_plugin_t *plugin);
    /* perform plugin configuration, hook up with SRS infra */
    int  (*config)(srs_plugin_t *plugin, srs_cfg_t *settings);
    /* perform remaining plugin startup steps if any */
    int  (*start)(srs_plugin_t *plugin);
    /* initiate plugin shutdown sequence */
    void (*stop)(srs_plugin_t *plugin);
    /* perform final plugin cleanup, free memory, etc. */
    void (*destroy)(srs_plugin_t *plugin);
};

/** Macro to declare a plugin. */
#define SRS_DESCRIBE_PLUGIN_FUNC "__srs_describe_plugin"

#define SRS_DECLARE_PLUGIN(_name, _descr, _authors, _version, ...)     \
    srs_plugin_api_t *__srs_describe_plugin(const char **name,         \
                                            const char **description,  \
                                            const char **authors,      \
                                            const char **version,      \
                                            int         *srs_version)  \
    {                                                                  \
        static srs_plugin_api_t api = { __VA_ARGS__ };                 \
                                                                       \
        *name         = _name;                                         \
        *description  = _descr;                                        \
        *authors      = _authors;                                      \
        *version      = _version;                                      \
        *srs_version  = SRS_PLUGIN_API_VERSION;                        \
                                                                       \
        return &api;                                                   \
    }



/*
 * plugin states
 */
typedef enum {
    SRS_PLUGIN_UNKNOWN = 0,
    SRS_PLUGIN_CREATED,                  /* plugin successfully created */
    SRS_PLUGIN_CONFIGURED,               /* plugin successfully configured */
    SRS_PLUGIN_STARTED,                  /* plugin successfully started */
    SRS_PLUGIN_STOPPED,                  /* plugin successfully stopped */
} srs_plugin_state_t;

/*
 * an SRS plugin
 */
struct srs_plugin_s {
    mrp_list_hook_t     hook;            /* hook to list of plugins */
    srs_context_t      *srs;             /* SRS context */
    char               *name;            /* plugin name */
    const char         *description;     /* verbose plugin description */
    const char         *authors;         /* plugin authors */
    void               *plugin_data;     /* opaque plugin data */
    srs_plugin_api_t   *api;             /* plugin API functions */
    void               *h;               /* plugin (DSO) handle */
    srs_plugin_state_t  state;           /* plugin state */
};


/** Create (ie. load and initialize) a plugin. */
srs_plugin_t *srs_create_plugin(srs_context_t *srs, const char *name);

/** Configure the given plugin plugin. */
int srs_configure_plugin(srs_plugin_t *plugin, srs_cfg_t *settings);

/** Start the given plugin. */
int srs_start_plugin(srs_plugin_t *plugin);

/** Stop the given plugin. */
void srs_stop_plugin(srs_plugin_t *plugin);

/** Destroy the given plugin. */
void srs_destroy_plugin(srs_plugin_t *plugin);

/** Configure all loaded plugins. */
int srs_configure_plugins(srs_context_t *srs);

/** Start all loaded plugins. */
int srs_start_plugins(srs_context_t *srs);

/** Stop all loaded plugins. */
void srs_stop_plugins(srs_context_t *srs);

/** Destroy all loaded plugins. */
void srs_destroy_plugins(srs_context_t *srs);

#endif /* __SRS_DAEMON_PLUGIN_H__ */
