#include <sys/time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <errno.h>

#include <murphy/common/debug.h>

#include <murphy/common/hashtbl.h>
#include <murphy/common/utils.h>

#include "clients.h"
#include "dbusif.h"
#include "pulseif.h"


struct clients_s {
    srs_client_t *srs_client;
    mrp_htbl_t *devices;
    device_t *current;
};

static char *commands[] = {
    "call",
    "listen to",
    "siri",
    NULL
};
static int ncommand = (sizeof(commands) / sizeof(commands[0])) - 1;

static int play_samples(context_t *, size_t, int16_t *);
static int notify_focus(srs_client_t *, srs_voice_focus_t);
static int notify_command(srs_client_t *, int, int, char **, uint32_t *,
                          uint32_t *, srs_audiobuf_t *);
static device_t *device_find(clients_t *, const char *);
static void device_free(void *, void *);


int clients_create(context_t *ctx)
{
    clients_t *clients;
    mrp_htbl_config_t cfg;

    if (!ctx)
        return -1;

    if (!(clients = mrp_allocz(sizeof(clients_t))))
        return -1;

    memset(&cfg, 0, sizeof(cfg));
    cfg.nentry = 10;
    cfg.comp = mrp_string_comp;
    cfg.hash = mrp_string_hash;
    cfg.free = device_free;
    cfg.nbucket = cfg.nentry;

    clients->devices = mrp_htbl_create(&cfg);
    clients->current = NULL;

    ctx->clients = clients;

    return 0;
}


void clients_destroy(context_t *ctx)
{
    clients_t *clients;

    if (ctx && (clients = ctx->clients)) {
        ctx->clients = NULL;

        client_destroy(clients->srs_client);
        mrp_htbl_destroy(clients->devices, TRUE);

        free(clients);
    }
}

int clients_start(context_t *ctx)
{
    srs_plugin_t *pl;
    srs_context_t *srs;
    clients_t *clients;
    srs_client_ops_t callbacks;

    if (!ctx || !(pl = ctx->plugin) || !(srs = pl->srs) ||
        !(clients = ctx->clients))
        return -1;

    callbacks.notify_focus = notify_focus;
    callbacks.notify_command = notify_command;

    clients->srs_client = client_create(srs, SRS_CLIENT_TYPE_BUILTIN,
                                        PLUGIN_NAME, "voicerec",
                                        commands, ncommand,
                                        PLUGIN_NAME, &callbacks, ctx);

    client_request_focus(clients->srs_client, SRS_VOICE_FOCUS_SHARED);

    return 0;
}

int clients_stop(context_t *ctx)
{
    MRP_UNUSED(ctx);

    return 0;
}

device_t *clients_add_device(context_t *ctx, const char *btaddr)
{
    clients_t *clients;
    device_t *device = NULL;

    if (ctx && btaddr && (clients = ctx->clients)) {
        if (device_find(clients, btaddr)) {
            mrp_log_error("bluetooth blugin: attempt to add already "
                          "existing device @ %s", btaddr);
        }
        else if ((device = mrp_allocz(sizeof(device_t)))) {
            device->ctx = ctx;
            device->btaddr = mrp_strdup(btaddr);

            mrp_htbl_insert(clients->devices, (void *)device->btaddr, device);
        }
    }

    return device;
}

void clients_remove_device(device_t *device)
{
    context_t *ctx;
    clients_t *clients;
    modem_t *modem;
    card_t *card;

    if (device && (ctx = device->ctx) && (clients = ctx->clients)) {
        if (device == clients->current)
            clients->current = NULL;

        if ((card = device->card))
            card->device = NULL;

        if ((modem = device->modem))
            modem->device = NULL;

        mrp_htbl_remove(clients->devices, (void *)device->btaddr, TRUE);
    }
}

device_t *clients_find_device(context_t *ctx, const char *btaddr)
{
    clients_t *clients;
    device_t *device;

    if (!ctx || !btaddr || !(clients = ctx->clients) || !clients->devices)
        device = NULL;
    else
        device = device_find(clients, btaddr);

    return device;
}

bool clients_device_is_ready(device_t *device)
{
    return device && device->modem && device->card;
}

void clients_add_card_to_device(device_t *device, card_t *card)
{
    context_t *ctx;
    clients_t *clients;

    if (device && card && (ctx = device->ctx) && (clients = ctx->clients)) {
        if (device->card && card != device->card) {
            mrp_log_error("bluetooth client: refuse to add card to client @ %s"
                          ". It has already one", device->btaddr);
        }
        device->card = card;

        if (clients_device_is_ready(device)) {
            mrp_log_info("added bluetooth device '%s' @ %s",
                         device->modem->name, device->btaddr);
            if (!clients->current)
                clients->current = device;
        }
    }
}

void clients_remove_card_from_device(device_t *device)
{
    context_t *ctx;
    clients_t *clients;
    card_t *card;

    if (device && (ctx = device->ctx) && (clients = ctx->clients)) {
        if ((card = device->card)) {
            device->card = NULL;

            if (device == clients->current)
                clients->current = NULL;
        }
    }
}

void clients_stop_recognising_voice(device_t *device)
{
    modem_t *modem;
    card_t *card;

    if (device) {
        mrp_free(device->samples);
        device->nsample = 0;
        device->samples = NULL;

        if ((modem = device->modem) && modem->state == VOICE_RECOGNITION_ON) {
            dbusif_set_voice_recognition(modem, VOICE_RECOGNITION_OFF);
        }

        if ((card = device->card)) {
            pulseif_remove_input_stream_from_card(card);
            pulseif_remove_output_stream_from_card(card);
        }
    }
}

static int play_samples(context_t *ctx, size_t nsample, int16_t *samples)
{
    clients_t *clients;
    device_t *device;
    modem_t *modem;
    card_t *card;

    if (!ctx || !nsample || !samples || !(clients = ctx->clients))
        return -1;

    if (!(device = clients->current)) {
        mrp_log_error("bluetooth client: can't play samples: no device");
        return -1;
    }

    if (!(modem = device->modem) || !(card = device->card))
        return -1;

    if (device->nsample || device->samples ||
        modem->state == VOICE_RECOGNITION_ON)
    {
        mrp_log_error("bluetooth client: can't play samples: voicerec "
                      "already in progress");
        return -1;
    }

    device->nsample = nsample;
    device->samples = samples;

    if (dbusif_set_voice_recognition(modem, VOICE_RECOGNITION_ON) < 0 ||
        pulseif_add_input_stream_to_card(card)                    < 0  )
    {
        return -1;
    }

    return 0;
}

static int notify_focus(srs_client_t *srs_client, srs_voice_focus_t focus)
{
    MRP_UNUSED(srs_client);
    MRP_UNUSED(focus);

    return TRUE;
}

static int notify_command(srs_client_t *srs_client, int idx,
                          int ntoken, char **tokens,
                          uint32_t *start, uint32_t *end,
                          srs_audiobuf_t *audio)
{
    context_t *ctx;
    clients_t *clients;
    device_t *device;
    char cmd[2048];
    char *e, *p, *sep;
    int i;

    MRP_UNUSED(idx);
    MRP_UNUSED(ntoken);
    MRP_UNUSED(tokens);
    MRP_UNUSED(start);
    MRP_UNUSED(end);
    MRP_UNUSED(audio);

    if (!srs_client || !(ctx = srs_client->user_data) ||
        !(clients = ctx->clients))
        return FALSE;

    e = (p = cmd) + (sizeof(cmd) - 1);

    for (i = 0, sep = "", *p = 0;   i < ntoken && p < e;   i++, sep = " ")
        p += snprintf(p, e-p, "%s%s", sep, tokens[i]);

    if (!(device = clients->current)) {
        mrp_log_info("no bluetooth device to execute command '%s'", cmd);
        return FALSE;
    }

    mrp_log_info("Bluetooth client got command '%s'\n", cmd);

    play_samples(ctx, samplelen / sizeof(int16_t), samplebuf);

    return TRUE;
}

static device_t *device_find(clients_t *clients, const char *btaddr)
{
    device_t *device;

    if (!clients || !clients->devices || !btaddr)
        device = NULL;
    else
        device = mrp_htbl_lookup(clients->devices, (void *)btaddr);

    return device;
}

static void device_free(void *key, void *object)
{
    device_t *device = (device_t *)object;

    if (strcmp(key, device->btaddr)) {
        mrp_log_error("bluetooth plugin: %s() confused with internal "
                      "data structures", __FUNCTION__);
    }
    else {
        mrp_free((void *)device->btaddr);
        mrp_free((void *)device);
    }
}


/*
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 *
 */
