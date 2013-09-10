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

#include <errno.h>

#include <murphy/common/debug.h>
#include <murphy/common/mainloop.h>
#include <murphy/common/glib-glue.h>

#include <gio/gio.h>

#include "src/daemon/plugin.h"
#include "src/daemon/client.h"

#define WRTC_NAME    "wrt-media-client"
#define WRTC_DESCR   "A demo WRT media player relay client."
#define WRTC_AUTHORS "Krisztian Litkey <krisztian.litkey@intel.com>"
#define WRTC_VERSION "0.0.1"

#define CONFIG_BUS   "wrtc.bus"
#define CONFIG_PLAY  "wrtc.commands.play"
#define CONFIG_STOP  "wrtc.commands.stop"
#define CONFIG_PAUSE "wrtc.commands.pause"
#define CONFIG_NEXT  "wrtc.commands.next"
#define CONFIG_PREV  "wrtc.commands.prev"

#define DEFAULT_BUS   "session"
#define DEFAULT_PLAY  "play music"
#define DEFAULT_STOP  "stop music"
#define DEFAULT_PAUSE "pause music"
#define DEFAULT_NEXT  "play next"
#define DEFAULT_PREV  "play previous"

#define TTS_INTERFACE_XML                                       \
    "<node>"                                                    \
    "  <interface name='org.tizen.srs'>"                        \
    "    <method name='synthesize'>"                            \
    "      <arg type='s' name='message' direction='in'/>"       \
    "      <arg type='s' name='language' direction='in'/>"      \
    "      <arg type='u' name='id' direction='out'/>"           \
    "    </method>"                                             \
    "  </interface>"                                            \
    "</node>"

typedef enum {
    CMD_PLAY = 0,
    CMD_STOP,
    CMD_PAUSE,
    CMD_NEXT,
    CMD_PREV
} wrtc_cmd_t;

typedef struct {
    srs_context_t   *srs;                /* SRS runtime context */
    srs_plugin_t    *self;               /* our plugin instance */
    srs_client_t    *c;                  /* our SRS client */
    GDBusConnection *gdbus;              /* D-Bus connection */
    guint            gnrq;               /* name request ID*/
    int              name;               /* whether we have the name */
    guint            gtts;               /* TTS method registration id */
    GDBusNodeInfo   *intr;               /* introspection data */
    struct {
        const char *bus;                 /* which bus to use */
        const char *play;                /* command to register for playback */
        const char *stop;                /*          -||-       for stop */
        const char *pause;               /*          -||-       for pause */
        const char *next;                /*          -||-       for next */
        const char *prev;                /*          -||-       for prev */
    } config;
} wrtc_t;


static void wrtc_cleanup(wrtc_t *wrtc);
static void tts_setup(wrtc_t *wrtc);
static void tts_cleanup(wrtc_t *wrtc);


static int focus_cb(srs_client_t *c, srs_voice_focus_t focus)
{
    wrtc_t        *wrtc = (wrtc_t *)c->user_data;
    srs_context_t *srs = wrtc->srs;
    const char    *state;

    MRP_UNUSED(wrtc);
    MRP_UNUSED(srs);

    switch (focus) {
    case SRS_VOICE_FOCUS_NONE:      state = "no";      break;
    case SRS_VOICE_FOCUS_SHARED:    state = "shared";    break;
    case SRS_VOICE_FOCUS_EXCLUSIVE: state = "exclusive"; break;
    default:                        state = "unknown";
    }

    mrp_log_info("WRT media client: got %s voice focus.", state);

    return TRUE;
}


static int command_cb(srs_client_t *c, int idx, int ntoken, char **tokens,
                      uint32_t *start, uint32_t *end, srs_audiobuf_t *audio)
{
    static const char *events[] = {
        [CMD_PLAY]  = "play",
        [CMD_STOP]  = "stop",
        [CMD_PAUSE] = "pause",
        [CMD_NEXT]  = "next",
        [CMD_PREV]  = "previous",
    };
    static int nevent = MRP_ARRAY_SIZE(events);

    wrtc_t          *wrtc = (wrtc_t *)c->user_data;
    const char      *event;
    GVariantBuilder *vb;
    GVariant        *args;

    MRP_UNUSED(start);
    MRP_UNUSED(end);
    MRP_UNUSED(audio);
    MRP_UNUSED(tokens);
    MRP_UNUSED(ntoken);

    if (!wrtc->name) {
        mrp_log_error("WRT media client: can't relay, got no D-Bus name.");

        return TRUE;
    }

    if (idx < 0 || idx >= nevent) {
        mrp_log_error("WRT media client: got invalid command #%d.", idx);

        return TRUE;
    }
    else
        event = events[idx];

    mrp_log_info("WRT media client: relaying command %s.", event);

    vb = g_variant_builder_new(G_VARIANT_TYPE("as"));
    g_variant_builder_add(vb, "s", event);
    args = g_variant_new("(as)", vb);
    g_variant_builder_unref(vb);

    g_dbus_connection_emit_signal(wrtc->gdbus, NULL,
                                  "/srs", "org.tizen.srs", "Result",
                                  args, NULL);
    return TRUE;
}


static void name_acquired_cb(GDBusConnection *gdbus,
                             const gchar *name, gpointer data)
{
    wrtc_t *wrtc = (wrtc_t *)data;

    MRP_UNUSED(gdbus);

    mrp_log_info("WRT media client: acquired name '%s'.", name);

    wrtc->name = TRUE;

    tts_setup(wrtc);
}


static void name_lost_cb(GDBusConnection *gdbus,
                         const gchar *name, gpointer data)
{
    wrtc_t *wrtc = (wrtc_t *)data;

    MRP_UNUSED(gdbus);

    mrp_log_info("WRT media client: lost name '%s'.", name);

    tts_cleanup(wrtc);

    wrtc->gnrq = 0;
    wrtc->name = 0;
}


static void tts_request_cb(GDBusConnection *c, const gchar *sender,
                           const gchar *path, const gchar *interface,
                           const gchar *method, GVariant *args,
                           GDBusMethodInvocation *inv, gpointer user_data)
{
    wrtc_t     *wrtc    = (wrtc_t *)user_data;
    int         timeout = SRS_VOICE_QUEUE;
    int         events  = SRS_VOICE_MASK_NONE;
    char       *voice, *msg;
    uint32_t    id;

    if (strcmp(method, "synthesize"))
        return;

    g_variant_get(args, "(&s&s)", &msg, &voice);

    if (voice == NULL || !*voice)
        voice = "english";

    mrp_log_info("WRT media client: relaying TTS request '%s' in '%s' from %s.",
                 msg, voice, sender);

    id = client_render_voice(wrtc->c, msg, voice, timeout, events);

    g_dbus_method_invocation_return_value(inv, g_variant_new("(u)", id));
}


static void tts_setup(wrtc_t *wrtc)
{
    static GDBusInterfaceVTable vtable = { tts_request_cb, NULL, NULL };

    wrtc->intr = g_dbus_node_info_new_for_xml(TTS_INTERFACE_XML, NULL);

    if (wrtc->intr == NULL) {
        mrp_log_error("WRT media client: failed to create introspection data.");
        return;
    }

    wrtc->gtts = g_dbus_connection_register_object(wrtc->gdbus, "/tts",
                                                   wrtc->intr->interfaces[0],
                                                   &vtable, wrtc, NULL, NULL);
}


static void tts_cleanup(wrtc_t *wrtc)
{
    if (wrtc->gtts != 0) {
        g_dbus_connection_unregister_object(wrtc->gdbus, wrtc->gtts);
        wrtc->gtts = 0;
    }

    if (wrtc->intr != NULL) {
        g_dbus_node_info_unref(wrtc->intr);
        wrtc->intr = NULL;
    }
}


static int wrtc_setup(wrtc_t *wrtc)
{
    static srs_client_ops_t ops = {
        .notify_focus   = focus_cb,
        .notify_command = command_cb
    };

    srs_context_t *srs = wrtc->srs;
    char          *cmds[] = {
        (char *)wrtc->config.play,
        (char *)wrtc->config.stop,
        (char *)wrtc->config.pause,
        (char *)wrtc->config.next,
        (char *)wrtc->config.prev
    };
    int         ncmd   = (int)MRP_ARRAY_SIZE(cmds);
    const char *name   = "wrt-media-client";
    const char *appcls = "player";
    const char *id     = "wrt-media-client";

    if (!strcmp(wrtc->config.bus, "session"))
        wrtc->gdbus = g_bus_get_sync(G_BUS_TYPE_SESSION, NULL, NULL);
    else if (!strcmp(wrtc->config.bus, "system"))
        wrtc->gdbus = g_bus_get_sync(G_BUS_TYPE_SYSTEM, NULL, NULL);
    else {
        int flags = G_DBUS_CONNECTION_FLAGS_MESSAGE_BUS_CONNECTION;
        wrtc->gdbus = g_dbus_connection_new_for_address_sync(wrtc->config.bus,
                                                             flags,
                                                             NULL, NULL, NULL);
    }

    if (wrtc->gdbus == NULL)
        return FALSE;

    wrtc->gnrq = g_bus_own_name(G_BUS_TYPE_SESSION, "org.tizen.srs", 0,
                                NULL, name_acquired_cb, name_lost_cb,
                                wrtc, NULL);

    wrtc->c = client_create(srs, SRS_CLIENT_TYPE_BUILTIN,
                            name, appcls, cmds, ncmd, id, &ops, wrtc);

    if (wrtc->c == NULL) {
        wrtc_cleanup(wrtc);

        return FALSE;
    }

    client_request_focus(wrtc->c, SRS_VOICE_FOCUS_SHARED);

    return TRUE;
}


static void wrtc_cleanup(wrtc_t *wrtc)
{
    if (wrtc->c != NULL)
        client_destroy(wrtc->c);

    tts_cleanup(wrtc);

    if (wrtc->gnrq != 0)
        g_bus_unown_name(wrtc->gnrq);

    if (wrtc->gdbus != NULL)
        g_object_unref(wrtc->gdbus);
}


static int create_wrtc(srs_plugin_t *plugin)
{
    wrtc_t *wrtc;

    mrp_debug("creating WRT media client plugin");

    wrtc = mrp_allocz(sizeof(*wrtc));

    if (wrtc != NULL) {
        wrtc->self = plugin;
        wrtc->srs  = plugin->srs;
        plugin->plugin_data = wrtc;
        return TRUE;
    }
    else
        return FALSE;
}


static int config_wrtc(srs_plugin_t *plugin, srs_cfg_t *settings)
{
    wrtc_t *wrtc = (wrtc_t *)plugin->plugin_data;
    struct {
        const char **addr;
        const char  *key;
        const char  *def;
    } config[] = {
        { &wrtc->config.bus  , CONFIG_BUS  , DEFAULT_BUS   },
        { &wrtc->config.play , CONFIG_PLAY , DEFAULT_PLAY  },
        { &wrtc->config.stop , CONFIG_STOP , DEFAULT_STOP  },
        { &wrtc->config.pause, CONFIG_PAUSE, DEFAULT_PAUSE },
        { &wrtc->config.next , CONFIG_NEXT , DEFAULT_NEXT  },
        { &wrtc->config.prev , CONFIG_PREV , DEFAULT_PREV  },
        { NULL, NULL, NULL }
    }, *cfg;

    mrp_debug("configure WRT media client plugin");

    if (plugin->srs->gl == NULL) {
        mrp_log_error("The WRT media client plugin requires GMainLoop.");
        mrp_log_error("Please set the 'gmainloop' config variable true.");

        if (plugin->srs->config_file != NULL)
            mrp_log_error("You can do this either in the configuration file"
                          "'%s',\n"
                          "or by passing -s gmainloop=true on the command line",
                          plugin->srs->config_file);
        else
            mrp_log_error("You can do this by passing the -s gmainloop=true\n"
                          "command line option.");

        return FALSE;
    }

    for (cfg = config; cfg->key; cfg++)
        *cfg->addr = srs_get_string_config(settings, cfg->key, cfg->def);

    mrp_log_info("WRT media client configuration:");
    mrp_log_info("    D-Bus: '%s'", wrtc->config.bus);
    mrp_log_info("    play command: '%s'", wrtc->config.play);
    mrp_log_info("    stop command: '%s'", wrtc->config.stop);
    mrp_log_info("    pause command: '%s'", wrtc->config.pause);
    mrp_log_info("    next command: '%s'", wrtc->config.next);
    mrp_log_info("    prev command: '%s'", wrtc->config.prev);

    return TRUE;
}


static int start_wrtc(srs_plugin_t *plugin)
{
    wrtc_t *wrtc = (wrtc_t *)plugin->plugin_data;

    MRP_UNUSED(wrtc);

    mrp_debug("start WRT media client plugin");

    return wrtc_setup(wrtc);
}


static void stop_wrtc(srs_plugin_t *plugin)
{
    wrtc_t *wrtc = (wrtc_t *)plugin->plugin_data;

    mrp_debug("stop WRT media client plugin");

    return;
}


static void destroy_wrtc(srs_plugin_t *plugin)
{
    srs_context_t *srs  = plugin->srs;
    wrtc_t        *wrtc = (wrtc_t *)plugin->plugin_data;

    MRP_UNUSED(srs);

    mrp_debug("destroy WRT media client plugin");

    wrtc_cleanup(wrtc);
    mrp_free(wrtc);
}


SRS_DECLARE_PLUGIN(WRTC_NAME, WRTC_DESCR, WRTC_AUTHORS, WRTC_VERSION,
                   create_wrtc, config_wrtc, start_wrtc, stop_wrtc,
                   destroy_wrtc)
