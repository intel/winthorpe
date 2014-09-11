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

#include <glib-object.h>

#include "srs/daemon/context.h"
#include "srs/daemon/config.h"
#include "srs/daemon/resctl.h"
#include "srs/daemon/plugin.h"
#include "srs/daemon/client.h"
#include "srs/daemon/recognizer.h"
#include "srs/daemon/pulse.h"

static void cleanup_mainloop(srs_context_t *srs);
static void resctl_state_change(srs_resctl_event_t *e, void *user_data);

static void cleanup_context(srs_context_t *srs)
{
    if (srs != NULL) {
        srs_resctl_disconnect(srs);
        srs_pulse_cleanup(srs->pulse);
        cleanup_mainloop(srs);

        /*
         * XXX TODO: should purge recognizers, disambiguators, synthesizers...
         */

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
        mrp_list_init(&srs->disambiguators);
    }

    return srs;
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


static void create_mainloop(srs_context_t *srs)
{
    if (srs_config_get_bool(srs->settings, "gmainloop", FALSE)) {
        mrp_log_info("Configured to run with glib mainloop.");

#ifndef GLIB_VERSION_2_36
        g_type_init();
#endif
        srs->gl = g_main_loop_new(NULL, FALSE);

        if (srs->gl == NULL) {
            mrp_log_error("Failed to create GMainLoop.");
            exit(1);
        }
    }
    else
        mrp_log_info("Configured to run with native PA mainloop.");

    if (srs->gl == NULL) {
        srs->pl = pa_mainloop_new();
        srs->pa = pa_mainloop_get_api(srs->pl);
        srs->ml = mrp_mainloop_pulse_get(srs->pa);
    }
    else {
        srs->pl = pa_glib_mainloop_new(g_main_loop_get_context(srs->gl));
        srs->pa = pa_glib_mainloop_get_api(srs->pl);
        srs->ml = mrp_mainloop_glib_get(srs->gl);
    }

    srs->pulse = srs_pulse_setup(srs->pa, "SRS daemon");

    if (srs->pa != NULL && srs->pulse != NULL && srs->ml != NULL) {
        if (srs_resctl_connect(srs, resctl_state_change, srs, TRUE))
            return;
    }

    cleanup_context(srs);
    exit(1);
}


static void run_mainloop(srs_context_t *srs)
{
    if (srs->gl == NULL)
        pa_mainloop_run((pa_mainloop *)srs->pl, &srs->exit_status);
    else
        g_main_loop_run(srs->gl);
}


static void quit_mainloop(srs_context_t *srs, int exit_status)
{
    if (srs != NULL) {
        if (srs->gl != NULL)
            g_main_loop_quit(srs->gl);
        else
            pa_mainloop_quit((pa_mainloop *)srs->pl, exit_status);
    }
    else
        exit(exit_status);
}


static void cleanup_mainloop(srs_context_t *srs)
{
    mrp_mainloop_destroy(srs->ml);
    srs->ml = NULL;

    if (srs->gl == NULL) {
        if (srs->pl != NULL)
            pa_mainloop_free(srs->pl);
    }
    else {
        pa_glib_mainloop_free(srs->pl);
        g_main_loop_unref(srs->gl);
        srs->gl = NULL;
    }

    srs->pl = NULL;
    srs->pa = NULL;
}


static void resctl_state_change(srs_resctl_event_t *e, void *user_data)
{
    srs_context_t *srs = (srs_context_t *)user_data;

    if (e->type != SRS_RESCTL_EVENT_CONNECTION)
        return;

    if (e->connection.up) {
        mrp_log_info("Resource control connection is up.");
        client_create_resources(srs);
    }
    else {
        mrp_log_info("Resource control connection is down.");
        client_reset_resources(srs);
    }
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


int main(int argc, char *argv[], char *env[])
{
    srs_context_t *srs;
    srs_cfg_t     *cfg;
    const char    *srec;

    srs = create_context();

    if (srs != NULL) {
        srs->rlog = mrp_res_set_logger(NULL);

        config_parse_cmdline(srs, argc, argv, env);
        setup_logging(srs);

        create_mainloop(srs);
        setup_signals(srs);

        if (!srs_configure_plugins(srs)) {
            mrp_log_error("Some plugins failed to configure.");
            exit(1);
        }

        if (!srs_start_plugins(srs)) {
            mrp_log_error("Some plugins failed to start.");
            exit(1);
        }

        cfg  = srs->settings;
        srec = srs_config_get_string(cfg, "daemon.speech-backend", NULL);
        srs_activate_srec(srs, srec);

        daemonize(srs);

        run_mainloop(srs);

        srs_stop_plugins(srs);
        srs_destroy_plugins(srs);

        cleanup_context(srs);

        exit(0);
    }
    else
        exit(1);
}
