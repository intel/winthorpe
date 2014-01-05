/*
 * Copyright (c) 2012, 2013, Intel Corporation
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

#include <sys/types.h>
#include <sys/stat.h>
#include <linux/input.h>
#include <libudev.h>
#include <stdio.h>
#include <stdlib.h>
#include <locale.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

#include <pulse/mainloop.h>
#include <pulse/mainloop-api.h>

#include <murphy/common/debug.h>

#include "srs/daemon/plugin.h"
#include "srs/daemon/context.h"
#include "srs/daemon/recognizer.h"

#define PLUGIN_NAME    "input-handler"
#define PLUGIN_DESCR   "For activating/deactivating voice recognition"
#define PLUGIN_AUTHORS "Janos Kovacs <janos.kovacs@intel.com>"
#define PLUGIN_VERSION "0.0.1"


typedef struct context_s  context_t;
typedef struct input_s    input_t;

struct context_s {
    srs_plugin_t *plugin;
    struct udev *udev;
    size_t nkbd;
    input_t *kbds;
    uint16_t key;
    uint32_t state;
};

struct input_s {
    const char *path;
    const char *id;
    int fd;
    pa_io_event *paev;
};

static void input_event_cb(pa_mainloop_api *, pa_io_event *, int,
                           pa_io_event_flags_t, void *);
static void scan_devices(context_t *);
static void handle_device(context_t *, struct udev_device *);
static int add_input(context_t *, const char *, size_t *, input_t **);
static int remove_input(context_t *, const char *, size_t *, input_t **);


static int create_input(srs_plugin_t *plugin)
{
    context_t *ctx;
    struct udev *udev;

    mrp_log_info("creating input plugin");

    if ((ctx = mrp_allocz(sizeof(context_t))) && (udev = udev_new())) {
        ctx->plugin = plugin;
        ctx->udev = udev;

        plugin->plugin_data = ctx;

        return TRUE;
    }

    return FALSE;
}


static int config_input(srs_plugin_t *plugin, srs_cfg_t *settings)
{
    context_t *ctx = (context_t *)plugin->plugin_data;

    mrp_log_info("configuring input plugin");

    ctx->key = KEY_PAUSE;

    return TRUE;
}


static int start_input(srs_plugin_t *plugin)
{
    context_t *ctx = (context_t *)plugin->plugin_data;

    mrp_log_info("starting input plugin");

    if (ctx->key) {
        scan_devices(ctx);
    }

    return TRUE;
}


static void stop_input(srs_plugin_t *plugin)
{
    context_t *ctx = (context_t *)plugin->plugin_data;

    mrp_log_info("stopping input plugin");

    if (ctx) {
        udev_unref(ctx->udev);

        while (ctx->nkbd > 0)
            remove_input(ctx, ctx->kbds->path, &ctx->nkbd, &ctx->kbds);
        mrp_free(ctx->kbds);

        ctx->nkbd = 0;
        ctx->kbds = NULL;
    }

    return;
}


static void destroy_input(srs_plugin_t *plugin)
{
    context_t *ctx = (context_t *)plugin->plugin_data;
    size_t i;

    mrp_log_info("destroying input plugin");

    if (ctx) {
        mrp_free(ctx);
    }
}

static void input_event_cb(pa_mainloop_api *ea,
                           pa_io_event *ioev,
                           int fd,
                           pa_io_event_flags_t events,
                           void *userdata)
{
    context_t *ctx = (void *)userdata;
    srs_plugin_t *plugin;
    srs_context_t *srs;
    struct input_event inpev;
    int rd;

    MRP_UNUSED(ea);
    MRP_UNUSED(ioev);
    MRP_UNUSED(events);

    if (ctx && (plugin = ctx->plugin) && (srs = plugin->srs)) {
        for (;;) {
            if ((rd = read(fd, &inpev, sizeof(inpev))) != sizeof(inpev)) {
                if (rd < 0 && errno == EINTR)
                    continue;
                else {
                    mrp_debug("input plugin: failed to read input event");
                    return;
                }
            }

            if (inpev.type == EV_KEY && inpev.value == 1) {
                if (inpev.code == ctx->key) {
                    if ((ctx->state ^= 1))
                        srs_activate_srec(srs, "sphinx-speech");
                    else
                        srs_deactivate_srec(srs, "sphinx-speech");
                }
            }

            return;
       } /* for ;; */
    }
}


static void scan_devices(context_t *ctx)
{
    struct udev *udev;
    struct udev_enumerate *enm;
    struct udev_list_entry *list, *entry;
    struct udev_device *dev;
    const char *syspath;

    if (!ctx || !(udev = ctx->udev))
        return;

    enm = udev_enumerate_new(udev);

    udev_enumerate_add_match_subsystem(enm, "input");
    udev_enumerate_scan_devices(enm);
    list = udev_enumerate_get_list_entry(enm);

    udev_list_entry_foreach(entry, list) {
        syspath = udev_list_entry_get_name(entry);
        if ((dev = udev_device_new_from_syspath(udev, syspath))) {
            handle_device(ctx, dev);
            udev_device_unref(dev);
        }
    }

    udev_enumerate_unref(enm);
}

static void handle_device(context_t *ctx, struct udev_device *dev)
{
    const char *path;
    const char *kbd;
    const char *key;
    int fd;

    if ((path = udev_device_get_property_value(dev, "DEVNAME"))) {
        key = udev_device_get_property_value(dev, "ID_INPUT_KEY");
        kbd = udev_device_get_property_value(dev, "ID_INPUT_KEYBOARD");

        if (key) {
        }

        if (kbd && ctx->key) {
            add_input(ctx, path, &ctx->nkbd, &ctx->kbds);
        }
    }
}

static int add_input(context_t *ctx,
                     const char *path,
                     size_t *pninput,
                     input_t **pinputs)
{
    static pa_io_event_flags_t flags = PA_IO_EVENT_INPUT;

    srs_plugin_t *plugin = ctx->plugin;
    srs_context_t *srs = plugin->srs;
    pa_mainloop_api *mlapi = srs->pa;
    char id[512];
    size_t idx;
    input_t *inp;
    size_t size;
    int fd;

    if (!mlapi || !path || (fd = open(path, O_RDONLY)) < 0)
        return -1;

    if (ioctl(fd, EVIOCGNAME(sizeof(id)), id) < 0) {
        close(fd);
        return -1;
    }

    idx = *pninput;
    size = sizeof(input_t) * (idx + 1);

    if (!(*pinputs = mrp_realloc(*pinputs, size))) {
        *pninput = 0;
        return -1;
    }

    *pninput = idx + 1;
    inp = *pinputs + idx;

    inp->path = mrp_strdup(path);
    inp->id = mrp_strdup(id);
    inp->fd = fd;
    inp->paev = mlapi->io_new(mlapi, fd, flags, input_event_cb, ctx);

    if (!inp->path || !inp->id || !inp->paev) {
        mrp_free((void *)inp->path);
        mrp_free((void *)inp->id);
        if (inp->paev)
            mlapi->io_free(inp->paev);
        close(inp->fd);
        *pninput = idx;
        return -1;
    }

    mrp_log_info("input plugin: added event source '%s'", id);

    return 0;
}

static int remove_input(context_t *ctx,
                        const char *path,
                        size_t *pninput,
                        input_t **pinputs)
{
    srs_plugin_t *plugin = ctx->plugin;
    srs_context_t *srs = plugin->srs;
    pa_mainloop_api *mlapi = srs->pa;
    input_t *inp, *inputs;
    size_t i, ninput, size;

    if (!mlapi || !path)
        return -1;

    inputs = *pinputs;
    ninput = *pninput;

    for (i = 0;  i < ninput;  i++) {
        inp = inputs + i;

        if (!strcmp(path, inp->path)) {
            if (inp->paev)
                mlapi->io_free(inp->paev);
            if (inp->fd >= 0)
                close(inp->fd);
            mrp_free((void *)inp->path);
            mrp_free((void *)inp->id);

            size = (ninput - (i + 1)) * sizeof(input_t);

            if (size > 0)
                memmove(inp + 1, inp, size);

            *pninput = ninput - 1;

            return 0;
        }
    }

    return -1;
}

SRS_DECLARE_PLUGIN(PLUGIN_NAME, PLUGIN_DESCR, PLUGIN_AUTHORS, PLUGIN_VERSION,
                   create_input, config_input, start_input, stop_input,
                   destroy_input)


/*
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 *
 */
