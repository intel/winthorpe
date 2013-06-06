#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <murphy/common/debug.h>
#include <murphy/common/dbus.h>

#include "dbusif.h"
#include "clients.h"

#define PLAYLIST_MAX 20

#define MAKE_DBUS_VERSION(major, minor, patch)  \
    (((major) << 16) | ((minor) << 8) | (patch))

#if DBUS_VERSION < MAKE_DBUS_VERSION(1, 6, 8)
/* For old versions, we define DBusBasicValue with the member we use... */
typedef union {
    char        *str;
    double       dbl;
    dbus_bool_t  bool_val;
} DBusBasicValue;
#endif

struct dbusif_s {
    const char *bustype;
    mrp_dbus_t *dbus;
};

static void name_follow_cb(mrp_dbus_t *,const char *,int,const char *,void *);
static void property_query_cb(mrp_dbus_t *, DBusMessage *, void *);
static void introspect_cb(mrp_dbus_t *, DBusMessage *, void *);
static int  property_changed_cb(mrp_dbus_t *, DBusMessage *, void *);
static void playlist_query_cb(mrp_dbus_t *, DBusMessage *, void *);
static int  parse_properties(context_t *, player_t *, DBusMessageIter *);

int dbusif_create(context_t *ctx, mrp_mainloop_t *ml)
{
    dbusif_t *dbusif;

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
    DBusMessage *msg;
    int success;

    if (!player || !player->name || !player->address ||
        !(ctx = player->ctx) || !(dbusif = ctx->dbusif))
        return;

    msg = dbus_message_new_method_call(player->address,
                                       "/org/mpris/MediaPlayer2",
                                       "org.freedesktop.DBus.Properties",
                                       "GetAll");

    if (!msg)
        return;

    success = dbus_message_append_args(msg,
                                       DBUS_TYPE_STRING, &interface,
                                       DBUS_TYPE_INVALID);
    if (success) {
        mrp_dbus_send(dbusif->dbus,
                      player->address,
                      "/org/mpris/MediaPlayer2",
                      "org.freedesktop.DBus.Properties",
                      "GetAll",
                      1000,
                      property_query_cb, player, msg);
    }

    dbus_message_unref(msg);
}

void dbusif_set_player_property(player_t *player,
                                const char *name,
                                const char *type,
                                void *value)
{
    const char *interface = "org.mpris.MediaPlayer2.Player";

    context_t *ctx;
    dbusif_t *dbusif;
    DBusMessage *msg;
    DBusMessageIter mit;
    DBusMessageIter vit;

    if (!player || !player->name || !player->address ||
        !(ctx = player->ctx) || !(dbusif = ctx->dbusif))
        return;

    msg = dbus_message_new_method_call(player->address,
                                       "/org/mpris/MediaPlayer2",
                                       "org.freedesktop.DBus.Properties",
                                       "Set");

    if (!msg)
        return;

    dbus_message_iter_init_append(msg, &mit);
    dbus_message_iter_append_basic(&mit, DBUS_TYPE_STRING, &interface);
    dbus_message_iter_append_basic(&mit, DBUS_TYPE_STRING, &name);
    dbus_message_iter_open_container(&mit, DBUS_TYPE_VARIANT, type, &vit);
    dbus_message_iter_append_basic(&vit, type[0], value);
    dbus_message_iter_close_container(&mit, &vit);

    mrp_dbus_send_msg(dbusif->dbus, msg);

    dbus_message_unref(msg);
}

void dbusif_introspect_player(player_t *player)
{
    context_t *ctx;
    dbusif_t *dbusif;
    DBusMessage *msg;

    if (!player || !player->service || !player->object ||
        !(ctx = player->ctx) || !(dbusif = ctx->dbusif))
        return;

    msg = dbus_message_new_method_call(player->service, player->object,
                                       "org.freedesktop.DBus.Introspectable",
                                       "Introspect");
    if (msg) {
        mrp_dbus_send(dbusif->dbus,
                      player->service,
                      player->object,
                      "org.freedesktop.DBus.Introspectable",
                      "Introspect",
                      3000,
                      introspect_cb, player, msg);
        dbus_message_unref(msg);
    }
}


void dbusif_set_player_state(player_t *player, player_state_t state)
{
    context_t *ctx;
    dbusif_t *dbusif;
    DBusMessage *msg;
    const char *member;

    if (!player || !player->address ||
        !(ctx = player->ctx) || !(dbusif = ctx->dbusif))
        return;

    switch (state) {
    case PLAY:  member = (player->state == PAUSE) ? "Play":"PlayPause"; break;
    case PAUSE: member = "Pause";                                       break;
    case STOP:  member = "Stop";                                        break;
    default:                                                            return;
    }

    msg = dbus_message_new_method_call(player->address,
                                       "/org/mpris/MediaPlayer2",
                                       "org.mpris.MediaPlayer2.Player",
                                       member);
    if (msg) {
        mrp_dbus_send_msg(dbusif->dbus, msg);
        dbus_message_unref(msg);
    }
}

void dbusif_change_track(player_t *player, track_t track)
{
    context_t *ctx;
    dbusif_t *dbusif;
    DBusMessage *msg;
    const char *member;

    if (!player || !player->address ||
        !(ctx = player->ctx) || !(dbusif = ctx->dbusif))
        return;

    switch (track) {
    case NEXT_TRACK:       member = "Next";         break;
    case PREVIOUS_TRACK:   member = "Previous";     break;
    default:                                        return;
    }

    msg = dbus_message_new_method_call(player->address,
                                       "/org/mpris/MediaPlayer2",
                                       "org.mpris.MediaPlayer2.Player",
                                       member);
    if (msg) {
        mrp_dbus_send_msg(dbusif->dbus, msg);
        dbus_message_unref(msg);
    }
}

void dbusif_set_playlist(player_t *player, const char *id)
{
    context_t *ctx;
    dbusif_t *dbusif;
    DBusMessage *msg;
    int success;

    printf("playlist id: %s\n", id);

    if (!player || !player->address ||
        !(ctx = player->ctx) || !(dbusif = ctx->dbusif))
        return;

    msg = dbus_message_new_method_call(player->address,
                                       "/org/mpris/MediaPlayer2",
                                       "org.mpris.MediaPlayer2.Playlists",
                                       "ActivatePlaylist");
    if (!msg)
        return;

    success = dbus_message_append_args(msg,
                                       DBUS_TYPE_OBJECT_PATH, &id,
                                       DBUS_TYPE_INVALID);
    if (success)
        mrp_dbus_send_msg(dbusif->dbus, msg);

    dbus_message_unref(msg);
}

void dbusif_query_playlists(player_t *player)
{
    const char    *order = "Alpahbetical";
    dbus_bool_t    reverse = FALSE;
    dbus_uint32_t  index = 0;
    dbus_uint32_t  max_count = PLAYLIST_MAX;

    context_t *ctx;
    dbusif_t *dbusif;
    DBusMessage *msg;
    int success;

    if (!player || !player->name || !player->address ||
        !(ctx = player->ctx) || !(dbusif = ctx->dbusif))
        return;

    msg = dbus_message_new_method_call(player->address,
                                       "/org/mpris/MediaPlayer2",
                                       "org.mpris.MediaPlayer2.Playlists",
                                       "GetPlaylists");

    if (!msg)
        return;

    success = dbus_message_append_args(msg,
                                       DBUS_TYPE_UINT32,  &index,
                                       DBUS_TYPE_UINT32,  &max_count,
                                       DBUS_TYPE_STRING,  &order,
                                       DBUS_TYPE_BOOLEAN, &reverse,
                                       DBUS_TYPE_INVALID);
    if (success) {
        mrp_dbus_send(dbusif->dbus,
                      player->address,
                      "/org/mpris/MediaPlayer2",
                      "org.mpris.MediaPlayer2.Playlists",
                      "GetPlaylists",
                      1000,
                      playlist_query_cb, player, msg);
    }

    dbus_message_unref(msg);
}

void dbusif_raise_player(player_t *player)
{
    context_t *ctx;
    dbusif_t *dbusif;
    DBusMessage *msg;

    if (!player || !player->address ||
        !(ctx = player->ctx) || !(dbusif = ctx->dbusif))
        return;

    msg = dbus_message_new_method_call(player->address,
                                       "/org/mpris/MediaPlayer2",
                                       "org.mpris.MediaPlayer2",
                                       "Raise");
    if (msg) {
        mrp_dbus_send_msg(dbusif->dbus, msg);
        dbus_message_unref(msg);
    }
}

void dbusif_quit_player(player_t *player)
{
    context_t *ctx;
    dbusif_t *dbusif;
    DBusMessage *msg;

    if (!player || !player->address ||
        !(ctx = player->ctx) || !(dbusif = ctx->dbusif))
        return;

    msg = dbus_message_new_method_call(player->address,
                                       "/org/mpris/MediaPlayer2",
                                       "org.mpris.MediaPlayer2",
                                       "Quit");
    if (msg) {
        mrp_dbus_send_msg(dbusif->dbus, msg);
        dbus_message_unref(msg);
    }
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
    const char *name;

    MRP_UNUSED(dbus);
    MRP_UNUSED(error);

    if (ctx && (dbusif = ctx->dbusif) && (dot = strrchr(dbus_name, '.'))) {
        name = dot + 1;

        if (owner && owner[0]) {
            if (owner[0] == ':') {
                clients_player_appeared(ctx, name, owner);

                mrp_dbus_subscribe_signal(dbusif->dbus,property_changed_cb,ctx,
                                          owner,
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

static void property_query_cb(mrp_dbus_t *dbus,
                              DBusMessage *msg,
                              void *user_data)
{
    player_t *player = (player_t *)user_data;
    context_t *ctx;
    DBusMessageIter mit;

    MRP_UNUSED(dbus);

    if (player && (ctx = player->ctx) && dbus_message_iter_init(msg, &mit))
        parse_properties(ctx, player, &mit);
}

static void introspect_cb(mrp_dbus_t *dbus,
                          DBusMessage *msg,
                          void *user_data)
{
    player_t *player = (player_t *)user_data;
    context_t *ctx;
    dbus_bool_t success;
    const char *xml;

    MRP_UNUSED(dbus);

    if (player && (ctx = player->ctx)) {
        success = dbus_message_get_args(msg, NULL,
                                        DBUS_TYPE_STRING, &xml,
                                        DBUS_TYPE_INVALID);
        if (success)
            mrp_log_info("%s", xml);
    }
}

static void playlist_query_cb(mrp_dbus_t *dbus,
                              DBusMessage *msg,
                              void *user_data)
{
    player_t *player = (player_t *)user_data;
    context_t *ctx;
    DBusMessageIter mit;
    DBusMessageIter ait;
    DBusMessageIter sit;
    playlist_t lists[PLAYLIST_MAX + 1];
    playlist_t *list;
    size_t nlist;

    MRP_UNUSED(dbus);

    if (!player || !(ctx = player->ctx))
        return;

    if (!dbus_message_iter_init(msg, &mit))
        return;

    if (dbus_message_iter_get_arg_type(&mit) != DBUS_TYPE_ARRAY)
        return;

    dbus_message_iter_recurse(&mit, &ait);

    nlist = 0;

    while (dbus_message_iter_get_arg_type(&ait) == DBUS_TYPE_STRUCT) {
        list = lists + nlist++;

        dbus_message_iter_recurse(&ait, &sit);

        dbus_message_iter_get_basic(&sit, &list->id);
        dbus_message_iter_next(&sit);
        dbus_message_iter_get_basic(&sit, &list->name);

        printf("*** %zd: '%s' '%s'\n", nlist-1, list->id, list->name);

        dbus_message_iter_next(&ait);
    }

    memset(lists + nlist, 0, sizeof(playlist_t));

    clients_playlist_changed(player, nlist, lists);
}

static int property_changed_cb(mrp_dbus_t *dbus,
                               DBusMessage *msg,
                               void *user_data)
{
    context_t *ctx = (context_t *)user_data;
    const char *sender = dbus_message_get_sender(msg);
    player_t *player;
    const char *interface;
    DBusMessageIter mit;

    MRP_UNUSED(dbus);

    if (!ctx || ! sender)
        return FALSE;

    if (!(player = clients_find_player_by_address(ctx, sender)))
        return FALSE;

    if (!dbus_message_iter_init(msg, &mit))
        return FALSE;

    if (dbus_message_iter_get_arg_type(&mit) != DBUS_TYPE_STRING)
        return FALSE;

    dbus_message_iter_get_basic(&mit, &interface);

    if (strcmp(interface, "org.mpris.MediaPlayer2.Player"))
        return FALSE;

    if (!dbus_message_iter_next(&mit))
        return FALSE;

    return parse_properties(ctx, player, &mit);
}

static int parse_properties(context_t *ctx,
                            player_t *player,
                            DBusMessageIter *mit)
{
    const char *prop;
    int type;
    DBusBasicValue value;
    DBusMessageIter ait;
    DBusMessageIter dit;
    DBusMessageIter vit;
    player_state_t state;

    if (!ctx || !player)
        return FALSE;

    if (dbus_message_iter_get_arg_type(mit) != DBUS_TYPE_ARRAY)
        return FALSE;

    dbus_message_iter_recurse(mit, &ait);

    while (dbus_message_iter_get_arg_type(&ait) == DBUS_TYPE_DICT_ENTRY) {
        dbus_message_iter_recurse(&ait, &dit);

        dbus_message_iter_get_basic(&dit, &prop);
        dbus_message_iter_next(&dit);

        dbus_message_iter_recurse(&dit, &vit);
        type = dbus_message_iter_get_arg_type(&vit);

        if (type != DBUS_TYPE_ARRAY) {
            memset(&value, 0, sizeof(value));
            dbus_message_iter_get_basic(&vit, &value);

            if (!strcmp(prop, "PlaybackStatus") && type == DBUS_TYPE_STRING) {
                if (!strcmp(value.str, "Playing"))
                    state = PLAY;
                else if (!strcmp(value.str, "Paused"))
                    state = PAUSE;
                else if (!strcmp(value.str, "Stopped"))
                    state = STOP;
                else
                    state = UNKNOWN;

                // printf("*** state %d\n", state);

                if (state != UNKNOWN)
                    clients_player_state_changed(player, state);
            }
            else if (!strcmp(prop, "Volume") && type == DBUS_TYPE_DOUBLE) {
                printf("*** volume %.4lf\n", value.dbl);
                clients_player_volume_changed(player, value.dbl);
            }
            else if (!strcmp(prop, "CanPlay") && type == DBUS_TYPE_BOOLEAN) {
                //printf("*** %s play\n", value.bool_val ? "can":"unable to");
                clients_player_status_changed(player, value.bool_val);
            }
        }

        dbus_message_iter_next(&ait);
    }


    return TRUE;
}


/*
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 *
 */
