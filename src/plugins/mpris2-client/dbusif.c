#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <murphy/common/dbus-libdbus.h>
#include <murphy/common/log.h>

#include "dbusif.h"
#include "clients.h"

#define PLAYLIST_MAX 20

struct dbusif_s {
    const char *bustype;
    mrp_dbus_t *dbus;
};

static int set_player_property(player_t *player,
                               const char *name,
                               mrp_dbus_type_t type,
                               void *value)
{
    const char *interface = "org.mpris.MediaPlayer2.Player";
    context_t *ctx = NULL;
    dbusif_t *dbusif = NULL;
    mrp_dbus_msg_t *msg = NULL;
    char type_str[2] = { (char)type, '\0' };
    int success = FALSE;

    if (!player || !player->name || !player->address ||
        !(ctx = player->ctx) || !(dbusif = ctx->dbusif))
        return FALSE;

    msg = mrp_dbus_msg_method_call(dbusif->dbus, player->address,
                                   "/org/mpris/MediaPlayer2",
                                   "org.freedesktop.DBus.Properties",
                                   "Set");

    if (!msg)
        return FALSE;

    mrp_dbus_msg_append_basic(msg, MRP_DBUS_TYPE_STRING, (void *)interface);
    mrp_dbus_msg_append_basic(msg, MRP_DBUS_TYPE_STRING, (void *)name);
    mrp_dbus_msg_open_container(msg, MRP_DBUS_TYPE_VARIANT, type_str);
    mrp_dbus_msg_append_basic(msg, (char)type, value);
    mrp_dbus_msg_close_container(msg);

    success = mrp_dbus_send_msg(dbusif->dbus, msg);

    mrp_dbus_msg_unref(msg);

    return success;
}

static int parse_properties(player_t *player,
                            mrp_dbus_msg_t *msg /* a{sv} */)
{
    if (!player)
        return FALSE;

    if (mrp_dbus_msg_arg_type(msg, NULL) != MRP_DBUS_TYPE_ARRAY)
        return FALSE;

    mrp_dbus_msg_enter_container(msg, MRP_DBUS_TYPE_ARRAY, "{sv}");

    while (mrp_dbus_msg_enter_container(msg, MRP_DBUS_TYPE_DICT_ENTRY, NULL)) {
        const char *prop = NULL;

        mrp_dbus_msg_read_basic(msg, MRP_DBUS_TYPE_STRING, (void*)&prop);

        mrp_dbus_msg_enter_container(msg, MRP_DBUS_TYPE_VARIANT, NULL);

        if (!strcmp(prop, "PlaybackStatus") &&
             mrp_dbus_msg_arg_type(msg, NULL) == MRP_DBUS_TYPE_STRING) {
            const char *value = NULL;
            player_state_t state;

            mrp_dbus_msg_read_basic(msg, MRP_DBUS_TYPE_STRING, (void *)&value);

            if (!strcmp(value, "Playing"))
                state = PLAY;
            else if (!strcmp(value, "Paused"))
                state = PAUSE;
            else if (!strcmp(value, "Stopped"))
                state = STOP;
            else
                state = UNKNOWN;

            if (state != UNKNOWN)
                 clients_player_state_changed(player, state);
            mrp_log_info("Player state : %s", value);
        }
        else if (!strcmp(prop, "Volume") &&
                  mrp_dbus_msg_arg_type(msg, NULL) == MRP_DBUS_TYPE_DOUBLE) {
            double volume = 0.0;
            mrp_dbus_msg_read_basic(msg, MRP_DBUS_TYPE_DOUBLE, (void *)&volume);
            mrp_log_info ("player volume %.4lf", volume);
            clients_player_volume_changed(player, volume);
        }
        else if (!strcmp(prop, "CanPlay") &&
                 mrp_dbus_msg_arg_type(msg, NULL) == MRP_DBUS_TYPE_BOOLEAN) {
            uint32_t play = FALSE;
            mrp_dbus_msg_read_basic(msg, MRP_DBUS_TYPE_BOOLEAN, (void *)&play);
            clients_player_status_changed(player, !!play);
        }

        mrp_dbus_msg_exit_container(msg);

        mrp_dbus_msg_exit_container(msg);
    }

    mrp_dbus_msg_exit_container(msg);

    return TRUE;
}

static void property_query_cb(mrp_dbus_t *dbus,
                              mrp_dbus_msg_t *msg,
                              void *user_data)
{
    player_t *player = (player_t *)user_data;

    MRP_UNUSED(dbus);

    parse_properties(player, msg);
}

static void introspect_cb(mrp_dbus_t *dbus,
                          mrp_dbus_msg_t *msg,
                          void *user_data)
{
    player_t *player = (player_t *)user_data;
    context_t *ctx;
    const char *xml;

    MRP_UNUSED(dbus);

    if (player && (ctx = player->ctx) &&
        mrp_dbus_msg_read_basic(msg, (char)MRP_DBUS_TYPE_STRING, &xml)) {
        mrp_log_info("%s", xml);
    }
}

static void playlist_query_cb(mrp_dbus_t *dbus,
                              mrp_dbus_msg_t *msg,
                              void *user_data)
{
    player_t *player = (player_t *)user_data;
    context_t *ctx;
    playlist_t lists[PLAYLIST_MAX + 1];
    playlist_t *list;
    size_t nlist;

    MRP_UNUSED(dbus);

    if (!player || !(ctx = player->ctx))
        return;

    if (mrp_dbus_msg_arg_type(msg, NULL) != MRP_DBUS_TYPE_ARRAY)
        return;

    if (!mrp_dbus_msg_enter_container(msg, MRP_DBUS_TYPE_ARRAY, "(oss)"))
        return;

    nlist = 0;

    while (mrp_dbus_msg_enter_container(msg, MRP_DBUS_TYPE_STRUCT, "oss")) {
        list = lists + nlist++;

        mrp_dbus_msg_read_basic(msg, MRP_DBUS_TYPE_OBJECT_PATH, &list->id);
        mrp_dbus_msg_read_basic(msg, MRP_DBUS_TYPE_STRING, &list->name);

        mrp_log_info("*** %zd: '%s' '%s'\n", nlist-1, list->id, list->name);

        mrp_dbus_msg_exit_container(msg);
    }

    mrp_dbus_msg_exit_container(msg);

    memset(lists + nlist, 0, sizeof(playlist_t));

    clients_playlist_changed(player, nlist, lists);
}


static int property_changed_cb(mrp_dbus_t *dbus,
                               mrp_dbus_msg_t *msg, /* (sa{sv}as) */
                               void *user_data)
{
    context_t *ctx = (context_t *)user_data;
    const char *sender = mrp_dbus_msg_sender(msg);
    player_t *player = NULL;
    const char *interface = NULL;

    MRP_UNUSED(dbus);

    if (!ctx || !sender)
        return FALSE;

    if (!(player = clients_find_player_by_address(ctx, sender)))
        return FALSE;

    if (mrp_dbus_msg_arg_type(msg, NULL) != MRP_DBUS_TYPE_STRING)
        return FALSE;

    mrp_dbus_msg_read_basic(msg, MRP_DBUS_TYPE_STRING, (void *)&interface);

    if (strcmp(interface, "org.mpris.MediaPlayer2.Player"))
        return FALSE;

    return parse_properties(player, msg);
}

static void name_follow_cb(mrp_dbus_t *dbus,
                           const char *dbus_name,
                           int error,
                           const char *owner,
                           void *user_data)
{
    context_t *ctx = (context_t *)user_data;
    dbusif_t *dbusif;
    const char *dot;

    MRP_UNUSED(dbus);
    MRP_UNUSED(error);

    if (ctx && (dbusif = ctx->dbusif) && (dot = strrchr(dbus_name, '.'))) {
        const char *name = dot + 1;

        if (owner && owner[0]) {
            if (owner[0] == ':') {
                clients_player_appeared(ctx, name, owner);

                mrp_dbus_subscribe_signal(dbusif->dbus, property_changed_cb,
                                          ctx, owner,
                                          "/org/mpris/MediaPlayer2",
                                          "org.freedesktop.DBus.Properties",
                                          "PropertiesChanged", NULL);
            }
        }
        else {
            clients_player_disappeared(ctx, name);

            mrp_dbus_unsubscribe_signal(dbusif->dbus, property_changed_cb, ctx,
                                        owner,
                                        "/org/mpris/MediaPlayer2",
                                        "org.freedesktop.DBus.Properties",
                                        "PropertiesChanged", NULL);
        }
    }
}

int dbusif_create(context_t *ctx, mrp_mainloop_t *ml)
{
    dbusif_t *dbusif = NULL;

    if (!(dbusif = mrp_allocz(sizeof(dbusif_t))))
        return -1;

    dbusif->bustype = mrp_strdup("session");
    dbusif->dbus = mrp_dbus_get(ml, dbusif->bustype, NULL);

    if (!dbusif->dbus) {
        mrp_log_error("mpris2 plugin: failed to obtain DBus");
        mrp_free(dbusif);
        return -1;
    }

    ctx->dbusif = dbusif;

    return 0;
}

void dbusif_destroy(context_t *ctx)
{
    dbusif_t *dbusif;

    if (ctx && (dbusif = ctx->dbusif)) {
        ctx->dbusif = NULL;

        mrp_dbus_unref(dbusif->dbus);

        mrp_free((void *)dbusif->bustype);
        mrp_free((void *)dbusif);
    }
}

int dbusif_register_player(context_t *ctx, const char *name)
{
    dbusif_t *dbusif;
    mrp_dbus_t *dbus;
    char dbus_name[1024];

    if (!ctx || !name || !(dbusif = ctx->dbusif) || !(dbus = dbusif->dbus))
        return -1;

    snprintf(dbus_name, sizeof(dbus_name), "org.mpris.MediaPlayer2.%s", name);

    if (!mrp_dbus_follow_name(dbus, dbus_name, name_follow_cb, ctx))
        return -1;

    return 0;
}

void dbusif_unregister_player(context_t *ctx, const char *name)
{
    dbusif_t *dbusif;
    mrp_dbus_t *dbus;

    if (!ctx || !name || !(dbusif = ctx->dbusif) || !(dbus = dbusif->dbus))
        return;

    mrp_dbus_forget_name(dbus, name, name_follow_cb, ctx);
}

void dbusif_query_player_properties(player_t *player)
{
    const char *interface = "org.mpris.MediaPlayer2.Player";

    context_t *ctx;
    dbusif_t *dbusif;
    mrp_dbus_msg_t *msg = NULL;

    if (!player || !player->name || !player->address ||
        !(ctx = player->ctx) || !(dbusif = ctx->dbusif))
        return;

    msg = mrp_dbus_msg_method_call(dbusif->dbus, player->address,
                                   "/org/mpris/MediaPlayer2",
                                   "org.freedesktop.DBus.Properties",
                                   "GetAll");

    if (!msg)
      return;

    mrp_dbus_msg_append_basic(msg, MRP_DBUS_TYPE_STRING, (void *)interface);
    mrp_dbus_send(dbusif->dbus, player->address,
                  "/org/mpris/MediaPlayer2",
                  "org.freedesktop.DBus.Properties",
                  "GetAll",
                  1000,
                  property_query_cb, player, msg);

    mrp_dbus_msg_unref(msg);
}

int dbusif_set_player_volume(player_t *player, double volume)
{
    if (!player)
        return FALSE;

    return set_player_property(player, "Volume", MRP_DBUS_TYPE_DOUBLE,
                               (void *)&volume);
}

void dbusif_introspect_player(player_t *player)
{
    context_t *ctx = NULL;
    dbusif_t *dbusif = NULL;
    mrp_dbus_msg_t *msg = NULL;

    if (!player || !player->service || !player->object ||
        !(ctx = player->ctx) || !(dbusif = ctx->dbusif))
        return;

    msg = mrp_dbus_msg_method_call(dbusif->dbus, player->service,
                                   player->object,
                                   "org.freedesktop.DBus.Introspectable",
                                   "Introspect");
    if (!msg)
        return;

    mrp_dbus_send(dbusif->dbus, player->service, player->object,
                  "org.freedesktop.DBus.Introspectable",
                  "Introspect", 3000,
                  introspect_cb, player, msg);

    mrp_dbus_msg_unref(msg);
}


void dbusif_set_player_state(player_t *player, player_state_t state)
{
    context_t *ctx = NULL;
    dbusif_t *dbusif = NULL;
    mrp_dbus_msg_t *msg = NULL;
    const char *member = NULL;

    if (!player || !player->address ||
        !(ctx = player->ctx) || !(dbusif = ctx->dbusif))
        return;

    switch (state) {
    case PLAY:  member = (player->state == PAUSE) ? "Play":"PlayPause"; break;
    case PAUSE: member = "Pause"; break;
    case STOP:  member = "Stop"; break;
    default: return;
    }

    msg = mrp_dbus_msg_method_call(dbusif->dbus, player->address,
                                   "/org/mpris/MediaPlayer2",
                                   "org.mpris.MediaPlayer2.Player",
                                   member);
    if (!msg)
        return;

    mrp_dbus_send_msg(dbusif->dbus, msg);
    mrp_dbus_msg_unref(msg);
}

void dbusif_change_track(player_t *player, track_t track)
{
    context_t *ctx = NULL;
    dbusif_t *dbusif = NULL;
    mrp_dbus_msg_t *msg = NULL;
    const char *member = NULL;

    if (!player || !player->address ||
        !(ctx = player->ctx) || !(dbusif = ctx->dbusif))
        return;

    switch (track) {
    case NEXT_TRACK:       member = "Next";         break;
    case PREVIOUS_TRACK:   member = "Previous";     break;
    default:                                        return;
    }

    msg = mrp_dbus_msg_method_call(dbusif->dbus, player->address,
                                   "/org/mpris/MediaPlayer2",
                                   "org.mpris.MediaPlayer2.Player",
                                   member);
    if (!msg)
        return;

    mrp_dbus_send_msg(dbusif->dbus, msg);
    mrp_dbus_msg_unref(msg);
}

void dbusif_set_playlist(player_t *player, const char *id)
{
    context_t *ctx = NULL;
    dbusif_t *dbusif = NULL;
    mrp_dbus_msg_t *msg = NULL;

    mrp_log_info("playlist id: %s\n", id);

    if (!player || !player->address ||
        !(ctx = player->ctx) || !(dbusif = ctx->dbusif))
        return;

    msg = mrp_dbus_msg_method_call(dbusif->dbus, player->address,
                                   "/org/mpris/MediaPlayer2",
                                   "org.mpris.MediaPlayer2.Playlists",
                                   "ActivatePlaylist");
    if (!msg)
        return;

    if (mrp_dbus_msg_append_basic(msg, MRP_DBUS_TYPE_OBJECT_PATH, (void *)&id))
        mrp_dbus_send_msg(dbusif->dbus, msg);

    mrp_dbus_msg_unref(msg);
}

void dbusif_query_playlists(player_t *player)
{
    const char *order = "Alpahbetical";
    uint32_t reverse = FALSE;
    uint32_t index = 0;
    uint32_t max_count = PLAYLIST_MAX;

    context_t *ctx = NULL;
    dbusif_t *dbusif = NULL;
    mrp_dbus_msg_t *msg = NULL;

    if (!player || !player->name || !player->address ||
        !(ctx = player->ctx) || !(dbusif = ctx->dbusif))
        return;

    msg = mrp_dbus_msg_method_call(dbusif->dbus, player->address,
                                   "/org/mpris/MediaPlayer2",
                                   "org.mpris.MediaPlayer2.Playlists",
                                   "GetPlaylists");

    if (!msg)
        return;

    mrp_dbus_msg_append_basic(msg, MRP_DBUS_TYPE_UINT32, (void *)&index);
    mrp_dbus_msg_append_basic(msg, MRP_DBUS_TYPE_UINT32, (void *)&max_count);
    mrp_dbus_msg_append_basic(msg, MRP_DBUS_TYPE_STRING, (void *)order);
    mrp_dbus_msg_append_basic(msg, MRP_DBUS_TYPE_BOOLEAN, (void *)&reverse);

    mrp_dbus_send(dbusif->dbus,
                  player->address,
                  "/org/mpris/MediaPlayer2",
                  "org.mpris.MediaPlayer2.Playlists",
                  "GetPlaylists",
                  1000,
                  playlist_query_cb, player, msg);

    mrp_dbus_msg_unref(msg);
}

void dbusif_raise_player(player_t *player)
{
    context_t *ctx = NULL;
    dbusif_t *dbusif = NULL;
    mrp_dbus_msg_t *msg = NULL;

    if (!player || !player->address ||
        !(ctx = player->ctx) || !(dbusif = ctx->dbusif))
        return;

    msg = mrp_dbus_msg_method_call(dbusif->dbus, player->address,
                                   "/org/mpris/MediaPlayer2",
                                   "org.mpris.MediaPlayer2",
                                   "Raise");
    if (!msg)
        return;

    mrp_dbus_send_msg(dbusif->dbus, msg);
    mrp_dbus_msg_unref(msg);
}

void dbusif_quit_player(player_t *player)
{
    context_t *ctx = NULL;
    dbusif_t *dbusif = NULL;
    mrp_dbus_msg_t *msg = NULL;

    if (!player || !player->address ||
        !(ctx = player->ctx) || !(dbusif = ctx->dbusif))
        return;

    msg = mrp_dbus_msg_method_call(dbusif->dbus, player->address,
                                   "/org/mpris/MediaPlayer2",
                                   "org.mpris.MediaPlayer2",
                                   "Quit");
    if (!msg)
        return;

    mrp_dbus_send_msg(dbusif->dbus, msg);
    mrp_dbus_msg_unref(msg);
}

/*
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 *
 */
