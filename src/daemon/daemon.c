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

#include <murphy/common/macros.h>
#include <murphy/common/mm.h>
#include <murphy/common/log.h>
#include <murphy/common/utils.h>

#include "src/daemon/context.h"
#include "src/daemon/config.h"
#include "src/daemon/dbusif.h"
#include "src/daemon/resourceif.h"
#include "src/daemon/plugin.h"
#include "src/daemon/client.h"
#include "src/daemon/recognizer.h"


static void cleanup_context(srs_context_t *srs)
{
    if (srs != NULL) {
        resource_disconnect(srs);

        if (srs->ml != NULL)
            mrp_mainloop_destroy(srs->ml);

        if (srs->pa != NULL)
            pa_mainloop_free(srs->pa);

        mrp_free(srs);
    }
}


static srs_context_t *create_context(void)
{
    srs_context_t *srs = mrp_allocz(sizeof(*srs));

    if (srs != NULL) {
        mrp_list_init(&srs->clients);
        mrp_list_init(&srs->plugins);
        mrp_list_init(&srs->recognizers);

        srs->pa = pa_mainloop_new();
        srs->ml = mrp_mainloop_pulse_get(pa_mainloop_get_api(srs->pa));

        if (srs->pa != NULL && srs->ml != NULL)
            if (resource_connect(srs))
                return srs;

        cleanup_context(srs);
    }

    return NULL;
}


static void setup_logging(srs_context_t *srs)
{
    const char *target;

    target = mrp_log_parse_target(srs->log_target);

    if (target != NULL)
        mrp_log_set_target(target);
    else
        mrp_log_error("invalid log target '%s'", srs->log_target);
}


static void daemonize(srs_context_t *srs)
{
    if (!srs->foreground) {
        mrp_log_info("Switching to daemon mode.");

        if (!mrp_daemonize("/", "/dev/null", "/dev/null")) {
            mrp_log_error("Failed to daemonize.");
            exit(1);
        }
    }
}


static void run_mainloop(srs_context_t *srs)
{
    pa_mainloop_run(srs->pa, &srs->exit_status);
}


static void quit_mainloop(srs_context_t *srs, int exit_status)
{
    if (srs != NULL)
        pa_mainloop_quit(srs->pa, exit_status);
    else
        exit(exit_status);
}


static void sighandler(mrp_sighandler_t *h, int signum, void *user_data)
{
    static int rlog = FALSE;

    srs_context_t *srs = (srs_context_t *)user_data;

    MRP_UNUSED(h);

    switch (signum) {
    case SIGINT:
        mrp_log_info("Received SIGINT, exiting...");
        quit_mainloop(srs, 0);
        break;

    case SIGTERM:
        mrp_log_info("Received SIGTERM, exiting...");
        quit_mainloop(srs, 0);
        break;

    case SIGUSR2:
        mrp_log_info("%s resource library logging...",
                     rlog ? "Disabling" : "Enabling");
        mrp_res_set_logger(rlog ? NULL : srs->rlog);
        rlog = !rlog;
        break;
    }
}


static void setup_signals(srs_context_t *srs)
{
    mrp_add_sighandler(srs->ml, SIGINT , sighandler, srs);
    mrp_add_sighandler(srs->ml, SIGTERM, sighandler, srs);
    mrp_add_sighandler(srs->ml, SIGUSR2, sighandler, srs);
}


int main(int argc, char *argv[])
{
    srs_context_t *srs;
    const char    *srec;

    srs = create_context();

    if (srs != NULL) {
        srs->rlog = mrp_res_set_logger(NULL);

        setup_signals(srs);
        config_parse_cmdline(srs, argc, argv);
        setup_logging(srs);
        dbusif_setup(srs);

        if (!srs_configure_plugins(srs)) {
            mrp_log_error("Some plugins failed to configure.");
            exit(1);
        }

        if (!srs_start_plugins(srs)) {
            mrp_log_error("Some plugins failed to start.");
            exit(1);
        }

        daemonize(srs);

        srec = srs_get_string_config(srs->settings, "daemon.activate", "fake");
        srs_activate_srec(srs, srec);

        run_mainloop(srs);

        srs_stop_plugins(srs);
        srs_destroy_plugins(srs);

        dbusif_cleanup(srs);
        cleanup_context(srs);

        exit(0);
    }
    else
        exit(1);
}
