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

#ifndef __SRS_DAEMON_CONTEXT_H__
#define __SRS_DAEMON_CONTEXT_H__

#include <murphy/common/list.h>
#include <murphy/common/pulse-glue.h>
#include <murphy/common/dbus.h>
#include <murphy/common/hashtbl.h>

#include <murphy/plugins/resource-native/libmurphy-resource/resource-api.h>

/** SRS daemon context type. */
typedef struct srs_context_s srs_context_t;

#include "src/daemon/config.h"

/*
 * daemon context
 */

struct srs_context_s {
    pa_mainloop       *pa;               /* pulseaudio mainloop */
    mrp_mainloop_t    *ml;               /* associated murphy mainloop */
    mrp_dbus_t        *dbus;             /* D-BUS connection */
    mrp_list_hook_t    clients;          /* connected clients */
    mrp_list_hook_t    plugins;          /* loaded plugins */
    mrp_timer_t       *rtmr;             /* resource reconnect timer */
    mrp_res_context_t *rctx;             /* resource context */
    mrp_res_logger_t   rlog;             /* original resource logger */
    mrp_list_hook_t    recognizers;      /* speech recognition backends */
    void              *default_srec;     /* default backend */
    void              *cached_srec;      /* previously looked up backend */
    mrp_list_hook_t    disambiguators;   /* disambiguators */
    void              *default_disamb;   /* default disambiguator */

    /* files and directories */
    const char      *config_file;        /* configuration file */
    const char      *plugin_dir;         /* plugin directory */

    /* logging settings */
    int              log_mask;           /* what to log */
    const char      *log_target;         /* and where to log to */

    /* miscellaneous runtime settings and status */
    const char      *dbus_address;       /* 'system', 'session', or address */
    int              foreground : 1;     /* whether to stay in foreground */
    int              exit_status;        /* mainloop exit status */

    /* configuration settings */
    srs_cfg_t       *settings;           /* configuration variables */
    int              nsetting;           /* number of variables */

    /* plugins to load */
    char           **requested_plugins;
    int              nrequested_plugin;
};


#endif /* __SRS_DAEMON_CONTEXT_H__ */
