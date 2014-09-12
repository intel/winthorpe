#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <murphy/common/debug.h>
#include <murphy/common/dbus-libdbus.h>
#include <murphy/common/hashtbl.h>
#include <murphy/common/list.h>
#include <murphy/common/utils.h>

#include "dbusif.h"
#include "clients.h"
#include "pulseif.h"

#define MAKE_DBUS_VERSION(major, minor, patch)  \
    (((major) << 16) | ((minor) << 8) | (patch))

struct dbusif_s {
    const char *bustype;
    mrp_dbus_t *dbus;
    mrp_list_hook_t modems;
};

static int mrp_dbus_is_basic_type(mrp_dbus_type_t t)
{
    return t != MRP_DBUS_TYPE_ARRAY && t != MRP_DBUS_TYPE_VARIANT &&
           t != MRP_DBUS_TYPE_STRUCT && t != MRP_DBUS_TYPE_DICT_ENTRY &&
           t != MRP_DBUS_TYPE_INVALID;
}

static const char * mrp_dbus_type_to_string(mrp_dbus_type_t t)
{
    static char type_str[2] = "";

    type_str[0] = t;

    return type_str;
}

static modem_t *reference_modem(modem_t *modem)
{
    if (modem && modem->refcnt >= 0)
        modem->refcnt++;

    return modem->refcnt < 0 ? NULL : modem;
}

static void unreference_modem(modem_t *modem)
{
    device_t *dev = NULL;

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
    modem_t *modem = NULL;
    dbusif_t *dbusif = NULL;
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

static modem_t *create_modem(context_t *ctx,
                             const char *path,
                             const char *name,
                             const char *addr)
{
    modem_t *modem = NULL;
    dbusif_t *dbusif = NULL;

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

    return modem;
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

typedef struct {
  mrp_dbus_type_t type;
  void *value;
  size_t n_values; /* for arrays */
} property_info_t;

static property_info_t * property_info_new(mrp_dbus_type_t type)
{
  property_info_t *p = NULL;

  if ((p = mrp_allocz(sizeof(*p)))) {
    p->type = type;
    p->value = 0;
    p->n_values = 0;
  }

  return p;
}

static void property_hash_free(void *key, void *object)
{
  property_info_t *p = object;

  MRP_UNUSED(key);

  if (p)
    mrp_free(p);
}

static int parse_property_value_basic(mrp_dbus_msg_t *msg,
                                      const char *name,
                                      mrp_dbus_type_t type,
                                      void *value_out)
{
    const char *n = NULL;

    if (!name || !value_out || !mrp_dbus_is_basic_type(type))
      return FALSE;

    if (mrp_dbus_msg_arg_type(msg, NULL) != MRP_DBUS_TYPE_STRING)
        return FALSE;

    mrp_dbus_msg_read_basic(msg, MRP_DBUS_TYPE_STRING, &n);

    if (strcmp(n, name))
      return FALSE;

    if (mrp_dbus_msg_arg_type(msg, NULL) != MRP_DBUS_TYPE_VARIANT)
        return FALSE;

    mrp_dbus_msg_enter_container(msg, MRP_DBUS_TYPE_VARIANT,
                                 mrp_dbus_type_to_string(type));

    mrp_dbus_msg_read_basic(msg, type, value_out);

    mrp_dbus_msg_exit_container(msg); /* v */

    return TRUE;
}

static void parse_propertites(mrp_dbus_msg_t *msg,
                              mrp_htbl_t *prop_tbl,
                              size_t size)
{
    size_t n_found = 0;

    if (mrp_dbus_msg_arg_type(msg, NULL) != MRP_DBUS_TYPE_ARRAY)
        return;

    if (!mrp_dbus_msg_enter_container(msg, MRP_DBUS_TYPE_ARRAY, "{sv}"))
        return;

    while (mrp_dbus_msg_enter_container(msg,  MRP_DBUS_TYPE_DICT_ENTRY, NULL)) {
        const char *prop = NULL;
        property_info_t *p_info = NULL;

        mrp_dbus_msg_read_basic(msg, MRP_DBUS_TYPE_STRING, &prop);

        p_info = (property_info_t *)mrp_htbl_lookup(prop_tbl, (void*)prop);
        if (p_info) {
            mrp_dbus_msg_enter_container(msg, MRP_DBUS_TYPE_VARIANT, NULL);

            /* NOTE: Currently we handle only string arrays, should modify
             *       based on need
             */
            if (p_info->type == MRP_DBUS_TYPE_ARRAY)
                mrp_dbus_msg_read_array(msg, MRP_DBUS_TYPE_STRING,
                                        &p_info->value, &p_info->n_values);
            else if (mrp_dbus_is_basic_type(p_info->type))
                mrp_dbus_msg_read_basic(msg, p_info->type, &p_info->value);

            mrp_dbus_msg_exit_container(msg); /* v */
            n_found++;
        }

        mrp_dbus_msg_exit_container(msg); /* "{sv}" */

        /* check if found all requested properties */
        if (n_found == size)
          break;
    }

    mrp_dbus_msg_exit_container(msg); /* a{sv} */
}

static void parse_modem_properties(mrp_dbus_msg_t *msg,
                                   const char **btaddr,
                                   const char **btname,
                                   uint32_t *online)
{
    bool has_handsfree_interface = FALSE;

    if (!msg || !btaddr || !btname || !online)
        return;

    const char **interfaces = NULL;
    mrp_htbl_config_t conf = {
      .nbucket = 0,
      .nentry = 4,
      .comp = mrp_string_comp,
      .hash = mrp_string_hash,
      .free = property_hash_free
    };
    property_info_t *info = NULL;
    mrp_htbl_t *prop_tbl = mrp_htbl_create(&conf);

    info = property_info_new(MRP_DBUS_TYPE_STRING);
    mrp_htbl_insert(prop_tbl, "Name", info);
    info = property_info_new(MRP_DBUS_TYPE_STRING);
    mrp_htbl_insert(prop_tbl, "Serial", info);
    info = property_info_new(MRP_DBUS_TYPE_BOOLEAN);
    mrp_htbl_insert(prop_tbl, "Online", info);
    info = property_info_new(MRP_DBUS_TYPE_ARRAY);
    mrp_htbl_insert(prop_tbl, "Interfaces", info);

    parse_propertites(msg, prop_tbl, 4);

    info = (property_info_t *)mrp_htbl_lookup(prop_tbl, (void*)"Name");
    *btname = (const char *)info->value;
    info = (property_info_t *)mrp_htbl_lookup(prop_tbl, (void*)"Serial");
    *btaddr = (const char *)info->value;
    info = (property_info_t *)mrp_htbl_lookup(prop_tbl, (void*)"Online");
    *online = *(uint32_t *)info->value;
    info = (property_info_t *)mrp_htbl_lookup(prop_tbl, (void*)"Interfaces");
    interfaces = (const char **)info->value;
    if (interfaces) {
      size_t i;

      for (i = 0; i < info->n_values; i++) {
        if (!strcmp(interfaces[i], "org.ofono.Handsfree")) {
          has_handsfree_interface = TRUE;
          break;
        }
      }
    }

    mrp_htbl_destroy(prop_tbl, TRUE);

    if (!has_handsfree_interface) {
     //   *btname = "<unknown>";
     //   *btaddr = "<unknown>";
        *online = FALSE;
    }
}

static void modem_query_cb(mrp_dbus_t *dbus,
                           mrp_dbus_msg_t *msg,
                           void *user_data)
{
    modem_t *modem = (modem_t *)user_data;
    context_t *ctx = NULL;

    MRP_UNUSED(dbus);

    if (modem && (ctx = modem->ctx)) {
        if (mrp_dbus_msg_type(msg) != MRP_DBUS_MESSAGE_TYPE_ERROR) {
            const char *addr = NULL;
            const char *name = NULL;
            uint32_t online;
            device_t *dev = NULL;

            parse_modem_properties(msg, &addr, &name, &online);

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

        unreference_modem(modem);
    }
}

static void query_modem(modem_t *modem)
{
    context_t *ctx = NULL;
    dbusif_t *dbusif = NULL;
    modem_t *ref = NULL;

    if (!modem || !modem->path || !(ctx = modem->ctx) ||
        !(dbusif = ctx->dbusif))
        return;

    if ((ref = reference_modem(modem))) {
        mrp_dbus_msg_t *msg = mrp_dbus_msg_method_call(dbusif->dbus,
                                                       "org.ofono",
                                                       modem->path,
                                                       "org.ofono.Modem",
                                                       "GetProperties");
        mrp_dbus_send(dbusif->dbus, "org.ofono", modem->path, "org.ofono.Modem",
                      "GetProperties", 1000, modem_query_cb, ref, msg);
        mrp_dbus_msg_unref(msg);
    }
}

static void parse_handsfree_properties(modem_t *modem,
                                       mrp_dbus_msg_t *msg,
                                       hfp_state_t *state)
{
    mrp_htbl_config_t conf = {
      .nbucket = 0,
      .nentry = 1,
      .comp = mrp_string_comp,
      .hash = mrp_string_hash,
      .free = property_hash_free
    };
    property_info_t *info = NULL;
    mrp_htbl_t *prop_tbl = NULL;

    if (!modem || !msg || !state)
        return;

    *state = VOICE_RECOGNITION_UNKNOWN;

    prop_tbl = mrp_htbl_create(&conf);
    info = property_info_new(MRP_DBUS_TYPE_BOOLEAN);
    mrp_htbl_insert(prop_tbl, "VoiceRecognition", info);

    parse_propertites(msg, prop_tbl, 1);

    *state = (uint32_t)(ptrdiff_t)((property_info_t *)mrp_htbl_lookup(
        prop_tbl, (void*)"VoiceRecognition"))->value ?
                  VOICE_RECOGNITION_ON : VOICE_RECOGNITION_OFF;

    mrp_htbl_destroy(prop_tbl, TRUE);
}

static void set_modem_state(modem_t *modem, hfp_state_t state)
{
    device_t *device;
    card_t *card;

    if (state == modem->state)
        return;

    modem->state = state;

    if (!(device = modem->device))
        return;

    switch (state) {

    case VOICE_RECOGNITION_ON:
        mrp_log_info("bluetooth modem: setting voicerecognition on "
                     "for modem %s", modem->addr);
        if ((card = device->card)) {
            pulseif_set_card_profile(card, "hfgw");
        }
        break;

    case VOICE_RECOGNITION_OFF:
        mrp_log_info("bluetooth modem: setting voicerecognition off "
                     "for modem %s", modem->addr);
        clients_stop_recognising_voice(device);
        break;

    default:
        mrp_log_error("bluetooth plugin: attempt to set invalid stte "
                      "for modem %s", modem->addr);
        break;
    }
}

static void handsfree_query_cb(mrp_dbus_t *dbus,
                               mrp_dbus_msg_t *msg,
                               void *user_data)
{
    modem_t *modem = (modem_t *)user_data;
    hfp_state_t state = VOICE_RECOGNITION_UNKNOWN;

    MRP_UNUSED(dbus);

    if (!modem) return;

    if (mrp_dbus_msg_type(msg) != MRP_DBUS_MESSAGE_TYPE_ERROR) {
        parse_handsfree_properties(modem, msg, &state);
        set_modem_state(modem, state);
     }

     unreference_modem(modem);
}

static void query_handsfree(modem_t *modem)
{
    context_t *ctx = NULL;;
    dbusif_t *dbusif = NULL;
    modem_t *ref = NULL;

    if (!modem || !modem->path || !(ctx = modem->ctx) ||
        !(dbusif = ctx->dbusif))
        return;

    if ((ref = reference_modem(modem))) {
        mrp_dbus_msg_t *msg = mrp_dbus_msg_method_call(dbusif->dbus,
                                                       "org.ofono",
                                                       modem->path,
                                                       "org.ofono.Handsfree",
                                                       "GetProperties");
        mrp_dbus_send(dbusif->dbus, "org.ofono", modem->path,
                      "org.ofono.Handsfree", "GetProperties", 1000,
                      handsfree_query_cb, ref, msg);
        mrp_dbus_msg_unref(msg);
    }
}

static int modem_property_changed_cb(mrp_dbus_t *dbus,
                                     mrp_dbus_msg_t *msg,
                                     void *user_data)
{
    context_t *ctx = (context_t *)user_data;
    const char *path = NULL;
    uint32_t is_online = FALSE;

    MRP_UNUSED(dbus);

    if (!ctx || !ctx->dbusif ||
        !(path = mrp_dbus_msg_path(msg)))
        return FALSE;

    if (parse_property_value_basic(msg, "Online", MRP_DBUS_TYPE_BOOLEAN,
                                   &is_online)) {
        modem_t *modem = find_modem_by_path(ctx, path);

        if (is_online) {
            if (!modem) {
                if ((modem = create_modem(ctx, path, "", ""))) {
                    query_modem(modem);
                    query_handsfree(modem);
                }
            }
        } else if (modem) {
            destroy_modem(modem);
        }
    }

    return FALSE;
}


static int handsfree_property_changed_cb(mrp_dbus_t *dbus,
                                         mrp_dbus_msg_t *msg,
                                         void *user_data)
{
    context_t *ctx = (context_t *)user_data;
    const char *path = mrp_dbus_msg_path(msg);
    modem_t *modem = NULL;
    uint32_t state = FALSE;

    MRP_UNUSED(dbus);

    if (!ctx || !ctx->dbusif || !path)
        return FALSE;

    if (!(modem = find_modem_by_path(ctx, path)))
        return FALSE;

    if (parse_property_value_basic(msg, "VoiceRecognition",
                                   MRP_DBUS_TYPE_BOOLEAN, &state)) {
        set_modem_state(modem,
                        state ? VOICE_RECOGNITION_ON : VOICE_RECOGNITION_OFF);
    }

    return FALSE;
}

static void modem_query_all_cb(mrp_dbus_t *dbus,
                               mrp_dbus_msg_t *msg,
                               void *user_data)
{
    context_t *ctx = (context_t *)user_data;
    modem_t *modem;

    MRP_UNUSED(dbus);

    if (!ctx || !ctx->dbusif)
        return;

    if (mrp_dbus_msg_arg_type(msg, NULL) != MRP_DBUS_TYPE_ARRAY)
        return;

    mrp_dbus_msg_enter_container(msg, MRP_DBUS_TYPE_ARRAY, "(oa{sv})");

    while (mrp_dbus_msg_enter_container(msg, MRP_DBUS_TYPE_STRUCT, NULL)) {
        device_t *dev;
        const char *path = NULL;
        const char *addr;
        const char *name;
        uint32_t online;

        mrp_dbus_msg_read_basic(msg, MRP_DBUS_TYPE_OBJECT_PATH, &path);

        parse_modem_properties(msg, &addr, &name, &online);

        mrp_log_info("Modem details: %s %s %s %d", path, addr, name, online);

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

        mrp_dbus_msg_exit_container(msg); /* (oa{sv}) */
    }

    mrp_dbus_msg_exit_container(msg); /* a(oa{sv}) */
}

static void track_modems(context_t *ctx, bool track)
{
    static const char *modem_interface = "org.ofono.Modem";
    static const char *handsfree_interface = "org.ofono.Handsfree";
    static const char *member = "PropertyChanged";
    dbusif_t *dbusif = NULL;

    if (!ctx || !(dbusif = ctx->dbusif))
        return;

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

static void query_all_modems(context_t *ctx)
{
    uint32_t query_id;
    dbusif_t *dbusif = NULL;

    if (!ctx || !(dbusif = ctx->dbusif))
        return;

    query_id = mrp_dbus_call(dbusif->dbus,
                             "org.ofono", "/", "org.ofono.Manager",
                             "GetModems", 1000, modem_query_all_cb, ctx,
                             MRP_DBUS_TYPE_INVALID);
    if (query_id == 0) {
      /* query modems failed */
    }
}

static void set_property(mrp_dbus_msg_t *msg,
                         const char *name,
                         mrp_dbus_type_t type,
                         void *value)
{
    mrp_dbus_msg_open_container(msg, MRP_DBUS_TYPE_DICT_ENTRY, NULL);

    mrp_dbus_msg_append_basic(msg, DBUS_TYPE_STRING, (void*)name);

    mrp_dbus_msg_open_container(msg, MRP_DBUS_TYPE_VARIANT, NULL);
    mrp_dbus_msg_append_basic(msg, (char)type, value);
    mrp_dbus_msg_close_container(msg); /* variant */

    mrp_dbus_msg_close_container(msg); /* dictionary */
}

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
    context_t *ctx = NULL;
    dbusif_t *dbusif = NULL;;
    mrp_dbus_msg_t *msg = NULL;
    uint32_t value;

    if (!modem || !modem->path || !(ctx = modem->ctx) ||
        !(dbusif = ctx->dbusif))
        return -1;

    switch (state) {
    case VOICE_RECOGNITION_ON:   value = TRUE;    break;
    case VOICE_RECOGNITION_OFF:  value = FALSE;   break;
    default:                     /* invalid */    return -1;
    }

    msg = mrp_dbus_msg_method_call(dbusif->dbus, "org.ofono",
                                   modem->path,
                                   "org.ofono.Handsfree",
                                   "SetProperty");

    if (!msg)
        return -1;

    set_property(msg, "VoiceRecognition", MRP_DBUS_TYPE_BOOLEAN, &value);

    mrp_dbus_send_msg(dbusif->dbus, msg);

    mrp_dbus_msg_unref(msg);

    return 0;
}


/*
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 *
 */
