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

#ifndef __SRS_DAEMON_CONFIG_H__
#define __SRS_DAEMON_CONFIG_H__

/** SRS configuration variable type. */
typedef struct srs_cfg_s srs_cfg_t;

#include "src/daemon/context.h"

#ifndef LIBDIR
#    define LIBDIR "/usr/lib"
#endif

#ifndef SYSCONFDIR
#    define SYSCONFDIR "/etc"
#endif

#ifndef SRS_DEFAULT_CONFIG_FILE
#    define SRS_DEFAULT_CONFIG_FILE SYSCONFDIR"/src/srs.conf"
#endif

#ifndef SRS_DEFAULT_PLUGIN_DIR
#    define SRS_DEFAULT_PLUGIN_DIR  LIBDIR"/srs"
#endif


/*
 * a configuration variable/setting
 */

struct srs_cfg_s {
    char *key;                           /* configuration key */
    char *value;                         /* configuration value */
    int   used;                          /* TRUE if ever looked up */
};


/** Parse the daemon command line. */
void config_parse_cmdline(srs_context_t *srs, int argc, char **argv);

/** Get the value of a string configuration variable. */
const char *srs_get_string_config(srs_cfg_t *settings, const char *key,
                                  const char *defval);

/** Get the value of a boolean configuration variable. */
int srs_get_bool_config(srs_cfg_t *settings, const char *key, int defval);

/** Get the value of a 32-bit signed integer configuration variable. */
int32_t srs_get_int32_config(srs_cfg_t *settings, const char *key,
                             int32_t defval);

/** Get the value of a 32-bit unsigned integer configuration variable. */
uint32_t srs_get_uint32_config(srs_cfg_t *settings, const char *key,
                               uint32_t defval);

/** Collect configuration variable matching the given prefix. */
int srs_collect_config(srs_cfg_t *settings, const char *prefix,
                       srs_cfg_t **matching);

/** Free an array of configuration variables. */
void srs_free_config(srs_cfg_t *settings);

/** Set a configuration variable to the given value. */
void srs_set_config(srs_context_t *srs, const char *key, const char *value);
#endif /* __SRS_DAEMON_CONFIG_H__ */
