#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <murphy/common/debug.h>
#include <murphy/common/dbus.h>
#include <murphy/common/list.h>

#include "dbusif.h"
#include "clients.h"
#include "pulseif.h"

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
    mrp_list_hook_t modems;
};


static modem_t *create_modem(context_t *, const char *, const char *,
                             const char *);
static void destroy_modem(modem_t *);
static modem_t *reference_modem(modem_t *);
static void unreference_modem(modem_t *);
static modem_t *find_modem_by_path(context_t *, const char *);
static void track_modems(context_t *, bool);
static void query_all_modems(context_t *);
static void query_modem(modem_t *);
static void query_handsfree(modem_t *);

static int modem_property_changed_cb(mrp_dbus_t *, DBusMessage *, void *);
static int handsfree_property_changed_cb(mrp_dbus_t *, DBusMessage *, void *);
static void modem_query_all_cb(mrp_dbus_t *, DBusMessage *, void *);
static void modem_query_cb(mrp_dbus_t *, DBusMessage *, void *);
static void handsfree_query_cb(mrp_dbus_t *, DBusMessage *, void *);

static void set_modem_state(modem_t *, hfp_state_t);

static void parse_modem_properties(context_t *,DBusMessageIter *,const char **,
                                   const char **, dbus_bool_t *);
static void parse_handsfree_properties(modem_t *,  DBusMessageIter *,
                                       hfp_state_t *);
static void parse_property(DBusMessageIter *, const char **name,
                           int *type,  DBusBasicValue *);
static void set_property(DBusMessageIter *, const char *, int, void *);


int dbusif_create(context_t *ctx, mrp_mainloop_t *ml)
{
    dbusif_t *dbusif;

    if (!(dbusif = mrp_allocz(sizeof(dbusif_t))))
        return -1;

    dbusif->bustype = mrp_strdup("system");
    dbusif->dbus = mrp_dbus_get(ml, dbusif->bustype, NULL);

    if (!dbusif->dbus) {
        mrp_log_error("bluetooth voice recognition plugin: "
                      "failed to obtain DBus");
        mrp_free(dbusif);
        return -1;
    }

    mrp_list_init(&dbusif->modems);

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

int dbusif_start(context_t *ctx)
{
    track_modems(ctx, TRUE);
    query_all_modems(ctx);
    return 0;
}

void dbusif_stop(context_t *ctx)
{
    dbusif_t *dbusif;
    modem_t *modem;
    mrp_list_hook_t *entry, *n;

    track_modems(ctx, FALSE);

    if (ctx && (dbusif = ctx->dbusif)) {
        mrp_list_foreach(&dbusif->modems, entry, n) {
            modem = mrp_list_entry(entry, modem_t, link);
            destroy_modem(modem);
        }
    }
}

int dbusif_set_voice_recognition(modem_t *modem, hfp_state_t state)
{
    context_t *ctx;
    dbusif_t *dbusif;
    DBusMessage *msg;
    DBusMessageIter mit;
    dbus_bool_t value;

    if (!modem || !modem->path || !(ctx = modem->ctx) ||
        !(dbusif = ctx->dbusif))
        return -1;

    switch (state) {
    case VOICE_RECOGNITION_ON:   value = TRUE;    break;
    case VOICE_RECOGNITION_OFF:  value = FALSE;   break;
    default:                     /* invalid */    return -1;
    }

    msg = dbus_message_new_method_call("org.ofono",
                                       modem->path,
                                       "org.ofono.Handsfree",
                                       "SetProperty");
    if (!msg)
        return -1;

    dbus_message_iter_init_append(msg, &mit);
    set_property(&mit, "VoiceRecognition", DBUS_TYPE_BOOLEAN, &value);

    mrp_dbus_send_msg(dbusif->dbus, msg);

    dbus_message_unref(msg);

    return 0;
}


static modem_t *create_modem(context_t *ctx,
                             const char *path,
                             const char *name,
                             const char *addr)
{
    dbusif_t *dbusif;
    modem_t *modem;

    if (!ctx || !path || !addr || !(dbusif = ctx->dbusif))
        return NULL;

    if (find_modem_by_path(ctx, path))
        return NULL;

    if (!(modem = mrp_allocz(sizeof(modem_t))))
        return NULL;

    modem->path = mrp_strdup(path);
    modem->name = mrp_strdup(name ? name : "<unknown>");
    modem->addr = mrp_strdup(addr);
    modem->ctx = ctx;

    reference_modem(modem);

    mrp_list_prepend(&dbusif->modems, &modem->link);
}

static void destroy_modem(modem_t *modem)
{
    context_t *ctx;
    dbusif_t *dbusif;

    if (modem && (ctx = modem->ctx) && (dbusif = ctx->dbusif)) {
        mrp_list_delete(&modem->link);
        unreference_modem(modem);
    }
}

static modem_t *reference_modem(modem_t *modem)
{
    if (modem && modem->refcnt >= 0)
        modem->refcnt++;

    return modem->refcnt < 0 ? NULL : modem;
}

static void unreference_modem(modem_t *modem)
{
    device_t *dev;

    if (modem) {
        if (modem->refcnt > 1)
            modem->refcnt--;
        else {
            mrp_log_info("remove bluetooth modem '%s' @ %s (paths %s)",
                         modem->name, modem->addr, modem->path);

            if ((dev = modem->device)) {
                modem->device = NULL;
                clients_remove_device(dev);
            }

            mrp_list_delete(&modem->link);

            mrp_free((void *)modem->path);
            mrp_free((void *)modem->name);
            mrp_free((void *)modem->addr);

            mrp_free((void *)modem);
        }
    }
}


static modem_t *find_modem_by_path(context_t *ctx, const char *path)
{
    dbusif_t *dbusif;
    modem_t *modem;
    mrp_list_hook_t *entry, *n;

    if (!ctx || !path || !(dbusif = ctx->dbusif))
        return NULL;

    mrp_list_foreach(&dbusif->modems, entry, n) {
        modem = mrp_list_entry(entry, modem_t, link);
        if (!strcmp(path, modem->path))
            return modem;
    }

    return NULL;
}

static void track_modems(context_t *ctx, bool track)
{
    static const char *modem_interface = "org.ofono.Modem";
    static const char *handsfree_interface = "org.ofono.Handsfree";
    static const char *member = "PropertyChanged";

    dbusif_t *dbusif;

    if (ctx && (dbusif = ctx->dbusif)) {
        if (track) {
            mrp_dbus_add_signal_handler(dbusif->dbus, NULL, NULL,
                                        modem_interface, member,
                                        modem_property_changed_cb, ctx);
            mrp_dbus_add_signal_handler(dbusif->dbus, NULL, NULL,
                                        handsfree_interface, member,
                                        handsfree_property_changed_cb, ctx);
            mrp_dbus_install_filter(dbusif->dbus, NULL, NULL,
                                    modem_interface, member, NULL);
            mrp_dbus_install_filter(dbusif->dbus, NULL, NULL,
                                    handsfree_interface, member, NULL);
        }
        else {
            mrp_dbus_del_signal_handler(dbusif->dbus, NULL, NULL,
                                        modem_interface, member,
                                        modem_property_changed_cb, ctx);
            mrp_dbus_del_signal_handler(dbusif->dbus, NULL, NULL,
                                        handsfree_interface, member,
                                        handsfree_property_changed_cb, ctx);
            mrp_dbus_remove_filter(dbusif->dbus, NULL, NULL,
                                   modem_interface, member, NULL);
            mrp_dbus_remove_filter(dbusif->dbus, NULL, NULL,
                                   handsfree_interface, member, NULL);
        }
    }
}


static void query_all_modems(context_t *ctx)
{
    dbusif_t *dbusif;
    DBusMessage *msg;

    if (!ctx || !(dbusif = ctx->dbusif))
        return;

    msg = dbus_message_new_method_call("org.ofono", "/", "org.ofono.Manager",
                                       "GetModems");
    if (msg) {
        mrp_dbus_send(dbusif->dbus, "org.ofono", "/", "org.ofono.Manager",
                      "GetModems", 1000, modem_query_all_cb, ctx, msg);
    }
}

static void query_modem(modem_t *modem)
{
    context_t *ctx;
    dbusif_t *dbusif;
    DBusMessage *msg;
    const char *path;
    modem_t *ref;

    if (!modem || !(ctx = modem->ctx) || !(path = modem->path) ||
        !(dbusif = ctx->dbusif))
        return;

    msg = dbus_message_new_method_call("org.ofono", path, "org.ofono.Modem",
                                       "GetProperties");
    if (msg) {
        if ((ref = reference_modem(modem))) {
            mrp_dbus_send(dbusif->dbus, "org.ofono", path, "org.ofono.Modem",
                          "GetProperties", 1000, modem_query_cb, ref, msg);
        }
    }
}

static void query_handsfree(modem_t *modem)
{
    context_t *ctx;
    dbusif_t *dbusif;
    DBusMessage *msg;
    const char *path;
    modem_t *ref;

    if (!modem || !(ctx = modem->ctx) || !(path = modem->path) ||
        !(dbusif = ctx->dbusif))
        return;

    msg = dbus_message_new_method_call("org.ofono",path,"org.ofono.Handsfree",
                                       "GetProperties");
    if (msg) {
        if ((ref = reference_modem(modem))) {
            mrp_dbus_send(dbusif->dbus, "org.ofono",path,"org.ofono.Handsfree",
                          "GetProperties", 1000, handsfree_query_cb, ref, msg);
        }
    }
}

static int modem_property_changed_cb(mrp_dbus_t *dbus,
                                     DBusMessage *msg,
                                     void *user_data)
{
    context_t *ctx = (context_t *)user_data;
    dbusif_t *dbusif;
    DBusMessageIter mit;
    const char *path;
    modem_t *modem;
    const char *prop;
    int type;
    DBusBasicValue value;

    if (ctx && (dbusif = ctx->dbusif) && dbus_message_iter_init(msg, &mit)) {
        path = dbus_message_get_path(msg);

        parse_property(&mit, &prop, &type, &value);

        if (path && prop) {
            if (!strcmp(prop, "Online")) {
                modem = find_modem_by_path(ctx, path);

                if (value.bool_val) {
                    if (path && !modem) {
                        if ((modem = create_modem(ctx, path, "", ""))) {
                            query_modem(modem);
                            query_handsfree(modem);
                        }
                    }
                }
                else {
                    if (modem)
                        destroy_modem(modem);
                }
            }
        }
    }

    return FALSE;
}

static int handsfree_property_changed_cb(mrp_dbus_t *dbus,
                                         DBusMessage *msg,
                                         void *user_data)
{
    context_t *ctx = (context_t *)user_data;
    dbusif_t *dbusif;
    DBusMessageIter mit;
    const char *path;
    modem_t *modem;
    const char *prop;
    int type;
    DBusBasicValue value;
    hfp_state_t state;

    if (ctx && (dbusif = ctx->dbusif) && dbus_message_iter_init(msg, &mit)) {
        path = dbus_message_get_path(msg);

        parse_property(&mit, &prop, &type, &value);

        if (path && prop) {
            if (!strcmp(prop, "VoiceRecognition")) {
                if ((modem = find_modem_by_path(ctx, path))) {
                    if (value.bool_val)
                        state = VOICE_RECOGNITION_ON;
                    else
                        state = VOICE_RECOGNITION_OFF;

                    set_modem_state(modem, state);
               }
            }
        }
    }

    return FALSE;
}

static void modem_query_all_cb(mrp_dbus_t *dbus,
                               DBusMessage *msg,
                               void *user_data)
{
    context_t *ctx = (context_t *)user_data;
    dbusif_t *dbusif;
    device_t *dev;
    const char *path;
    const char *addr;
    const char *name;
    dbus_bool_t online;
    hfp_state_t state;
    modem_t *modem;
    DBusMessageIter mit, ait, sit;

    if (ctx && (dbusif = ctx->dbusif) && dbus_message_iter_init(msg, &mit)) {
        if (dbus_message_iter_get_arg_type(&mit) != DBUS_TYPE_ARRAY)
            return;

        dbus_message_iter_recurse(&mit, &ait);

        while (dbus_message_iter_get_arg_type(&ait) == DBUS_TYPE_STRUCT) {
            dbus_message_iter_recurse(&ait, &sit);

            if (dbus_message_iter_get_arg_type(&sit) == DBUS_TYPE_OBJECT_PATH){
                dbus_message_iter_get_basic(&sit, &path);
                dbus_message_iter_next(&sit);
                parse_modem_properties(ctx, &sit, &addr, &name, &online);

                if (path && online) {
                    if ((dev = clients_add_device(ctx, addr)) &&
                        (modem = create_modem(ctx, path, name, addr)))
                    {
                        modem->device = dev;
                        dev->modem = modem;

                        mrp_log_info("created bluetooth modem '%s' @ %s "
                                     "(path %s)", modem->name, modem->addr,
                                     modem->path);
                        query_handsfree(modem);
                    }
                }
            }

            dbus_message_iter_next(&ait);
        }
    }
}

static void modem_query_cb(mrp_dbus_t *dbus,
                           DBusMessage *msg,
                           void *user_data)
{
    modem_t *modem = (modem_t *)user_data;
    device_t *dev;
    context_t *ctx;
    const char *addr;
    const char *name;
    dbus_bool_t online;
    hfp_state_t state;
    DBusMessageIter mit, ait, sit;

    if (modem && (ctx = modem->ctx)) {

        if (dbus_message_get_type(msg) != DBUS_MESSAGE_TYPE_ERROR) {
            if (dbus_message_iter_init(msg, &mit)) {
                parse_modem_properties(ctx, &mit, &addr, &name, &online);

                if (!online || !name || !(dev = clients_add_device(ctx, addr)))
                    destroy_modem(modem);
                else {
                    mrp_free((void *)modem->addr);
                    mrp_free((void *)modem->name);

                    modem->addr = mrp_strdup(addr);
                    modem->name = mrp_strdup(name);
                    modem->device = dev;

                    dev->modem = modem;

                    mrp_log_info("created bluetooth modem '%s' @ %s (path %s)",
                                 modem->name, modem->addr, modem->path);
                }
            }
        }

        unreference_modem(modem);
    }
}

static void handsfree_query_cb(mrp_dbus_t *dbus,
                               DBusMessage *msg,
                               void *user_data)
{
    modem_t *modem = (modem_t *)user_data;
    context_t *ctx;
    hfp_state_t state;
    DBusMessageIter mit, ait, sit;

    if (modem && (ctx = modem->ctx)) {

        if (dbus_message_get_type(msg) != DBUS_MESSAGE_TYPE_ERROR) {

            if (dbus_message_iter_init(msg, &mit)) {
                parse_handsfree_properties(modem, &mit, &state);
                set_modem_state(modem, state);
            }

            unreference_modem(modem);
        }
    }
}

static void set_modem_state(modem_t *modem, hfp_state_t state)
{
    device_t *device;
    card_t *card;

    if (state == modem->state)
        return;

    switch (state) {

    case VOICE_RECOGNITION_ON:
        mrp_log_info("bluetooth modem: setting voicerecognition on "
                     "for modem %s", modem->addr);
        modem->state = VOICE_RECOGNITION_ON;
        if ((device = modem->device) && (card = device->card)) {
            pulseif_set_card_profile(card, "hfgw");
        }
        break;

    case VOICE_RECOGNITION_OFF:
        mrp_log_info("bluetooth modem: setting voicerecognition off "
                     "for modem %s", modem->addr);
        modem->state = VOICE_RECOGNITION_OFF;
        if ((device = modem->device)) {
            clients_stop_recognising_voice(device);
        }
        break;

    default:
        mrp_log_error("bluetooth plugin: attempt to set invalid stte "
                      "for modem %s", modem->addr);
        break;
    }
}

static void parse_modem_properties(context_t *ctx,
                                   DBusMessageIter *sit,
                                   const char **btaddr,
                                   const char **btname,
                                   dbus_bool_t *online)
{
    modem_t *modem;
    const char *prop;
    int type;
    DBusBasicValue value;
    DBusMessageIter ait, dit, vit, iit;
    dbus_bool_t has_handsfree_interface = FALSE;

    if (!ctx || !sit || !btaddr || !btname || !online)
        return;

    *btname = "<unknown>";
    *btaddr = "<unknown>";
    *online = FALSE;

    if (dbus_message_iter_get_arg_type(sit) != DBUS_TYPE_ARRAY)
        return;

    dbus_message_iter_recurse(sit, &ait);

    while (dbus_message_iter_get_arg_type(&ait) == DBUS_TYPE_DICT_ENTRY) {

        dbus_message_iter_recurse(&ait, &dit);

        dbus_message_iter_get_basic(&dit, &prop);
        dbus_message_iter_next(&dit);

        dbus_message_iter_recurse(&dit, &vit);
        type = dbus_message_iter_get_arg_type(&vit);

        if (type != DBUS_TYPE_ARRAY) {
            memset(&value, 0, sizeof(value));
            dbus_message_iter_get_basic(&vit, &value);

            if (!strcmp(prop, "Online"))
                *online = value.bool_val;
            else if(!strcmp(prop, "Name"))
                *btname = value.str;
            else if (!strcmp(prop, "Serial"))
                *btaddr = value.str;
        }
        else {
            if (!strcmp(prop, "Interfaces")) {
                dbus_message_iter_recurse(&vit, &iit);

                while (dbus_message_iter_get_arg_type(&iit)==DBUS_TYPE_STRING){
                    dbus_message_iter_get_basic(&iit, &value);

                    if (!strcmp(value.str, "org.ofono.Handsfree")) {
                        has_handsfree_interface = TRUE;
                        break;
                    }

                    dbus_message_iter_next(&iit);
                }
            }
        }

        dbus_message_iter_next(&ait);
    }

    if (!has_handsfree_interface) {
        *btname = "<unknown>";
        *btaddr = "<unknown>";
        *online = FALSE;
    }
}

static void parse_handsfree_properties(modem_t *modem,
                                       DBusMessageIter *sit,
                                       hfp_state_t *state)
{
    const char *prop;
    int type;
    DBusBasicValue value;
    DBusMessageIter ait, dit, vit, iit;
    dbus_bool_t has_handsfree_interface = FALSE;

    if (!modem || !sit || !state)
        return;

    *state = VOICE_RECOGNITION_UNKNOWN;

    if (dbus_message_iter_get_arg_type(sit) != DBUS_TYPE_ARRAY)
        return;

    dbus_message_iter_recurse(sit, &ait);

    while (dbus_message_iter_get_arg_type(&ait) == DBUS_TYPE_DICT_ENTRY) {

        dbus_message_iter_recurse(&ait, &dit);

        dbus_message_iter_get_basic(&dit, &prop);
        dbus_message_iter_next(&dit);

        dbus_message_iter_recurse(&dit, &vit);
        type = dbus_message_iter_get_arg_type(&vit);

        if (type != DBUS_TYPE_ARRAY) {
            memset(&value, 0, sizeof(value));
            dbus_message_iter_get_basic(&vit, &value);

            if (!strcmp(prop, "VoiceRecognition")) {
                if (value.bool_val)
                    *state = VOICE_RECOGNITION_ON;
                else
                    *state = VOICE_RECOGNITION_OFF;
            }
        }

        dbus_message_iter_next(&ait);
    }
}

static void parse_property(DBusMessageIter *it,
                           const char **name,
                           int *type,
                           DBusBasicValue *value)
{
    DBusMessageIter vit;

    if (it && name && type && value) {
        if (dbus_message_iter_get_arg_type(it) != DBUS_TYPE_STRING)
            goto failed;

        dbus_message_iter_get_basic(it, name);

        if (!dbus_message_iter_next(it))
            goto failed;

        if (dbus_message_iter_get_arg_type(it) != DBUS_TYPE_VARIANT)
            goto failed;

        dbus_message_iter_recurse(it, &vit);

        if ((*type = dbus_message_iter_get_arg_type(&vit)) == DBUS_TYPE_ARRAY)
            goto failed;

        dbus_message_iter_get_basic(&vit, value);

        return;
    }

 failed:
    *name = "<unknown>";
    *type = 0;
    memset(value, 0, sizeof(*value));
}

static void set_property(DBusMessageIter *it,
                         const char *name,
                         int type,
                         void *value)
{
    char type_str[2] = { type, 0 };
    DBusMessageIter vit;

    dbus_message_iter_append_basic(it, DBUS_TYPE_STRING, &name);

    dbus_message_iter_open_container(it, DBUS_TYPE_VARIANT, type_str, &vit);
    dbus_message_iter_append_basic(&vit, type, value);
    dbus_message_iter_close_container(it, &vit);
}

/*
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 *
 */
