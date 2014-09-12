#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

#include <pulse/pulseaudio.h>
#include <pulse/mainloop.h>
#include <pulse/introspect.h>

#include <murphy/common/mm.h>
#include <murphy/common/log.h>

#include "pulseif.h"
#include "dbusif.h"
#include "clients.h"

typedef struct {
    mrp_list_hook_t link;
    context_t *ctx;
    pa_operation *op;
} pending_op_t;



static card_t *add_card(context_t *, uint32_t, const char *, const char *,
                        const char *);
static void remove_card(card_t *);

static card_t *find_card_by_index(context_t *, uint32_t);
//static card_t *find_card_by_address(context_t *, const char *);
static card_t *find_card_by_sink(context_t *, uint32_t);
static card_t *find_card_by_source(context_t *, uint32_t);

static pending_op_t *add_pending_op(context_t *);
static void remove_pending_op(pending_op_t *);

static void connect_to_server(context_t *);

static int input_stream_create(card_t *);
static int output_stream_create(card_t *);

static void state_callback(pa_stream *, void *);
static void read_callback(pa_stream *, size_t, void *);
static void write_callback(pa_stream *, size_t, void *);

static void subscribe_succes_callback(pa_context *, int, void *);
static void profile_succes_callback(pa_context *, int, void *);

static void context_callback(pa_context *, void *);
static void event_callback(pa_context *, pa_subscription_event_type_t,
                           uint32_t, void *);

static void card_info_callback(pa_context *, const pa_card_info *,
                               int, void *);
static void source_info_callback(pa_context *, const pa_source_info *,
                                 int, void *);
static void sink_info_callback(pa_context *, const pa_sink_info *,
                               int, void *);


int pulseif_create(context_t *ctx, pa_mainloop_api *pa)
{
    pulseif_t *pulseif = NULL;

    if (!(pulseif = mrp_allocz(sizeof(pulseif_t))))
        goto failed;

    pulseif->paapi = pa;
    pulseif->rate = 16000;
    pulseif->limit.upper = 1500;
    pulseif->limit.lower = 100;

    mrp_list_init(&pulseif->cards);
    mrp_list_init(&pulseif->pending_ops);

    ctx->pulseif = pulseif;

    connect_to_server(ctx);

    return 0;

 failed:
    mrp_log_error("bluetooth plugin: failed to create pulseaudio interface");
    if (pulseif)
        mrp_free(pulseif);
    return -1;
}

void pulseif_destroy(context_t *ctx)
{
    pulseif_t *pulseif;
    mrp_list_hook_t *entry, *n;
    card_t *card;
    pending_op_t *pend;

    if (ctx && (pulseif = ctx->pulseif)) {
        ctx->pulseif = NULL;

        if (pulseif->subscr)
            pa_operation_cancel(pulseif->subscr);

        mrp_list_foreach(&pulseif->cards, entry, n) {
            card = mrp_list_entry(entry, card_t, link);
            remove_card(card);
        }

        mrp_list_foreach(&pulseif->pending_ops, entry, n) {
            pend = mrp_list_entry(entry, pending_op_t, link);
            remove_pending_op(pend);
        }


        if (pulseif->pactx) {
            pa_context_set_state_callback(pulseif->pactx, NULL, NULL);
            pa_context_set_subscribe_callback(pulseif->pactx, NULL, NULL);
            pa_context_unref(pulseif->pactx);
        }

        mrp_free(pulseif);
    }
}

int pulseif_set_card_profile(card_t *card, const char *profnam)
{
    context_t *ctx;
    pulseif_t *pulseif;
    pending_op_t *pend;

    if (!card || !profnam || !(ctx = card->ctx) || !(pulseif = ctx->pulseif))
        return -1;

    if (card->profnam && !strcmp(profnam, card->profnam))
        return 0;

    pend = add_pending_op(ctx);
    pend->op = pa_context_set_card_profile_by_index(pulseif->pactx, card->idx,
                                                    profnam,
                                                    profile_succes_callback,
                                                    pend);
    return 0;
}

int pulseif_add_input_stream_to_card(card_t *card)
{
    device_t *device;
    context_t *ctx;
    pulseif_t *pulseif;

    if (!card || !(device = card->device) || !(ctx = device->ctx) ||
        !(pulseif = ctx->pulseif))
        return -1;

    if (!card->source.name)
        return 0;

    printf("*** creating input stream\n");

    return input_stream_create(card);
}

int pulseif_remove_input_stream_from_card(card_t *card)
{
    device_t *device;
    context_t *ctx;
    pulseif_t *pulseif;

    if (!card || !(device = card->device) || !(ctx = device->ctx) ||
        !(pulseif = ctx->pulseif))
        return -1;

    if (card->input.stream) {
        printf("*** destroying input stream\n");
        pa_stream_disconnect(card->input.stream);
    }

    return 0;
}


int pulseif_add_output_stream_to_card(card_t *card)
{
    device_t *device;
    context_t *ctx;
    pulseif_t *pulseif;

    if (!card || !(device = card->device) || !(ctx = device->ctx) ||
        !(pulseif = ctx->pulseif))
        return -1;

    if (!card->sink.name)
        return 0;

    printf("*** creating output stream\n");

    return output_stream_create(card);
}

int pulseif_remove_output_stream_from_card(card_t *card)
{
    device_t *device;
    context_t *ctx;
    pulseif_t *pulseif;

    if (!card || !(device = card->device) || !(ctx = device->ctx) ||
        !(pulseif = ctx->pulseif))
        return -1;

    if (card->output.stream) {
        printf("*** destroying output stream\n");
        pa_stream_disconnect(card->output.stream);
    }

    return 0;
}


static card_t *add_card(context_t *ctx,
                        uint32_t idx,
                        const char *name,
                        const char *btaddr,
                        const char *profnam)
{
    pulseif_t *pulseif;
    card_t *card = NULL;

    if (ctx &&  (pulseif = ctx->pulseif) && name && btaddr && profnam) {
        if (!find_card_by_index(ctx, idx)) {
            if ((card = mrp_allocz(sizeof(*card)))) {
                mrp_list_prepend(&pulseif->cards, &card->link);
                card->ctx = ctx;
                card->idx = idx;
                card->name = mrp_strdup(name);
                card->btaddr = mrp_strdup(btaddr);
                card->profnam = mrp_strdup(profnam);
                card->sink.idx = -1;
                card->source.idx = -1;
            }
        }
    }

    return card;
}

static void remove_card(card_t *card)
{
    pa_stream *stream;

    if (card) {
        if ((stream = card->input.stream)) {
            pa_stream_set_state_callback(stream, NULL, NULL);
            pa_stream_set_underflow_callback(stream, NULL, NULL);
            pa_stream_set_suspended_callback(stream, NULL, NULL);
            pa_stream_set_read_callback(stream, NULL, NULL);
        }

        if ((stream = card->output.stream)) {
            pa_stream_set_state_callback(stream, NULL, NULL);
            pa_stream_set_underflow_callback(stream, NULL, NULL);
            pa_stream_set_suspended_callback(stream, NULL, NULL);
            pa_stream_set_write_callback(stream, NULL, NULL);
        }

        mrp_list_delete(&card->link);
        mrp_free((void *)card->name);
        mrp_free((void *)card->btaddr);
        mrp_free((void *)card->profnam);
        mrp_free((void *)card->sink.name);
        mrp_free((void *)card->source.name);

        mrp_free((void *)card);
    }
}

static card_t *find_card_by_index(context_t *ctx, uint32_t idx)
{
    pulseif_t *pulseif;
    mrp_list_hook_t *entry, *n;
    card_t *card;

    if (ctx && (pulseif = ctx->pulseif)) {
        mrp_list_foreach(&pulseif->cards, entry, n) {
            card = mrp_list_entry(entry, card_t, link);

            if (idx == card->idx)
                return card;
        }
    }

    return NULL;
}

#if 0
static card_t *find_card_by_address(context_t *ctx, const char *addr)
{
    pulseif_t *pulseif;
    mrp_list_hook_t *entry, *n;
    card_t *card;

    if (ctx && (pulseif = ctx->pulseif) && addr) {
        mrp_list_foreach(&pulseif->cards, entry, n) {
            card = mrp_list_entry(entry, card_t, link);

            if (!strcmp(addr, card->btaddr))
                return card;
        }
    }

    return NULL;
}
#endif

static card_t *find_card_by_sink(context_t *ctx, uint32_t idx)
{
    pulseif_t *pulseif;
    mrp_list_hook_t *entry, *n;
    card_t *card;

    if (ctx && (pulseif = ctx->pulseif)) {
        mrp_list_foreach(&pulseif->cards, entry, n) {
            card = mrp_list_entry(entry, card_t, link);

            if (idx == card->sink.idx)
                return card;
        }
    }

    return NULL;
}


static card_t *find_card_by_source(context_t *ctx, uint32_t idx)
{
    pulseif_t *pulseif;
    mrp_list_hook_t *entry, *n;
    card_t *card;

    if (ctx && (pulseif = ctx->pulseif)) {
        mrp_list_foreach(&pulseif->cards, entry, n) {
            card = mrp_list_entry(entry, card_t, link);

            if (idx == card->source.idx)
                return card;
        }
    }

    return NULL;
}


static pending_op_t *add_pending_op(context_t *ctx)
{
    pulseif_t *pulseif;
    pending_op_t *pending = NULL;

    if (ctx && (pulseif = ctx->pulseif)) {
        if ((pending = mrp_allocz(sizeof(pending_op_t)))) {
            mrp_list_prepend(&pulseif->pending_ops, &pending->link);
            pending->ctx = ctx;
        }
    }

    return pending;
}

static void remove_pending_op(pending_op_t *pending)
{
    if (pending) {
        mrp_list_delete(&pending->link);
        pa_operation_cancel(pending->op);
        mrp_free((void *)pending);
    }
}

static void connect_to_server(context_t *ctx)
{
    pulseif_t *pulseif = ctx->pulseif;
    pa_mainloop_api *api = pulseif->paapi;
    pa_context *pactx;

    if (pulseif->subscr)
        pa_operation_cancel(pulseif->subscr);

    if (pulseif->pactx) {
        pa_context_set_state_callback(pulseif->pactx, NULL, NULL);
        pa_context_set_subscribe_callback(pulseif->pactx, NULL, NULL);
        pa_context_unref(pulseif->pactx);
        pulseif->pactx = NULL;
    }

    if (!(pulseif->pactx = pactx = pa_context_new(api, "bluetooth"))) {
        mrp_log_error("pa_context_new() failed");
        return;
    }

    pa_context_set_state_callback(pactx, context_callback, ctx);
    pa_context_set_subscribe_callback(pactx, event_callback, ctx);

    mrp_log_error("bluetooth-plugin: Trying to connect to pulseaudio ...");
    pa_context_connect(pactx, NULL, PA_CONTEXT_NOFAIL, NULL);
}


static int input_stream_create(card_t *card)
{
    device_t *device;
    context_t *ctx;
    pulseif_t *pulseif;
    uint32_t minreq = 100;      /* length in msecs */
    uint32_t target = 1000;     /* length in msecs */
    pa_sample_spec spec;
    pa_buffer_attr battr;
    pa_proplist *pl;
    size_t minsiz, bufsiz, extra, size;

    if (!card || !(device = card->device) || !(ctx = device->ctx) ||
        !(pulseif = ctx->pulseif) || !(card->source.name))
        return -1;

    if (card->input.stream)
        return 0;

    memset(&spec, 0, sizeof(spec));
    spec.format = PA_SAMPLE_S16LE;
    spec.rate = pulseif->rate;
    spec.channels = 1; /* ie. MONO */

    minsiz = pa_usec_to_bytes(minreq * PA_USEC_PER_MSEC, &spec);
    bufsiz = pa_usec_to_bytes(target * PA_USEC_PER_MSEC, &spec);
    extra  = minsiz * 2;
    size   = bufsiz + extra;


    pl = pa_proplist_new();
    pa_proplist_sets(pl, PA_PROP_MEDIA_ROLE, "speech");

    card->input.stream = pa_stream_new_with_proplist(pulseif->pactx,
                                                     "speech-recognition",
                                                     &spec, NULL, pl);
    pa_proplist_free(pl);

    if (!card->input.stream) {
        mrp_log_error("bluetooth client: failed to create input stream "
                      "for card %s", card->btaddr);
        return -1;
    }

    battr.maxlength = -1;       /* default (4MB) */
    battr.tlength   = size;
    battr.minreq    = minsiz;
    battr.prebuf    = 2 * battr.tlength;
    battr.fragsize  = battr.tlength;

    pa_stream_set_state_callback(card->input.stream, state_callback, card);
    pa_stream_set_read_callback(card->input.stream, read_callback, card);

    pa_stream_connect_record(card->input.stream, card->source.name, &battr,
                             PA_STREAM_ADJUST_LATENCY);

    card->input.state = ST_BEGIN;

    return 0;
}


static int output_stream_create(card_t *card)
{
    device_t *device;
    context_t *ctx;
    pulseif_t *pulseif;
    uint32_t minreq = 100;     /* length in msecs */
    uint32_t target = 1000;    /* length in msecs */
    pa_sample_spec spec;
    pa_buffer_attr battr;
    pa_proplist *pl;
    size_t minsiz, bufsiz, extra, size;

    if (!card || !(device = card->device) || !(ctx = device->ctx) ||
        !(pulseif = ctx->pulseif) || !(card->sink.name))
        return -1;

    if (card->output.stream)
        return 0;

    memset(&spec, 0, sizeof(spec));
    spec.format = PA_SAMPLE_S16LE;
    spec.rate = pulseif->rate;
    spec.channels = 1; /* ie. MONO */

    minsiz = pa_usec_to_bytes(minreq * PA_USEC_PER_MSEC, &spec);
    bufsiz = pa_usec_to_bytes(target * PA_USEC_PER_MSEC, &spec);
    extra  = minsiz * 2;
    size   = bufsiz + extra;

    pl = pa_proplist_new();
    pa_proplist_sets(pl, PA_PROP_MEDIA_ROLE, "speech");

    card->output.stream = pa_stream_new_with_proplist(pulseif->pactx,
                                                      "speech-recognition",
                                                      &spec, NULL, pl);
    pa_proplist_free(pl);

    if (!card->output.stream) {
        mrp_log_error("bluetooth client: failed to create output stream "
                      "for card %s", card->btaddr);
        return -1;
    }

    battr.maxlength = -1;       /* default (4MB) */
    battr.tlength   = size;
    battr.minreq    = minsiz;
    battr.prebuf    = 2 * battr.tlength;
    battr.fragsize  = battr.tlength;

    pa_stream_set_state_callback(card->output.stream, state_callback, card);
    pa_stream_set_write_callback(card->output.stream, write_callback, card);

    pa_stream_connect_playback(card->output.stream, card->sink.name, &battr,
                               PA_STREAM_ADJUST_LATENCY, NULL, NULL);
    return 0;
}


static void state_callback(pa_stream *stream, void *userdata)
{
    card_t *card = (card_t *)userdata;
    pa_context *pactx = pa_stream_get_context(stream);
    pa_context_state_t ctxst = pa_context_get_state(pactx);
    device_t *device;
    context_t *ctx;
    pulseif_t *pulseif;
    int err;
    const char *strerr;
    const char *type;

    if (ctxst == PA_CONTEXT_TERMINATED || ctxst == PA_CONTEXT_FAILED)
        return;

    if (!card || !(device = card->device) || !(ctx = device->ctx) ||
        !(pulseif = ctx->pulseif))
        return;

    switch (pa_stream_get_state(stream)) {

    case PA_STREAM_CREATING:
        if (stream == card->input.stream) {
            mrp_debug("bluetooth plugin: input stream on %s creating",
                      card->btaddr);
        }
        else if (stream == card->output.stream) {
            mrp_debug("bluetooth plugin: output stream on %s creating",
                      card->btaddr);
        }
        break;

    case PA_STREAM_TERMINATED:
        if (stream == card->input.stream) {
            mrp_log_info("bluetooth plugin: input stream on %s terminated",
                         card->btaddr);
            card->input.stream = NULL;
            pa_stream_set_state_callback(stream, NULL, NULL);
            pa_stream_set_underflow_callback(stream, NULL, NULL);
            pa_stream_set_suspended_callback(stream, NULL, NULL);
            pa_stream_set_read_callback(stream, NULL, NULL);
        }
        else if (stream == card->output.stream) {
            mrp_log_info("bluetooth plugin: output stream on %s terminated",
                         card->btaddr);
            card->output.stream = NULL;
            card->output.sent = 0;
            pa_stream_set_state_callback(stream, NULL, NULL);
            pa_stream_set_underflow_callback(stream, NULL, NULL);
            pa_stream_set_suspended_callback(stream, NULL, NULL);
            pa_stream_set_write_callback(stream, NULL, NULL);
        }
        break;

    case PA_STREAM_READY:
        if (stream == card->input.stream) {
            mrp_log_info("bluetooth plugin: input stream on %s is ready",
                         card->btaddr);
        }
        else if (stream == card->output.stream) {
            mrp_log_info("bluetooth plugin: output stream on %s is ready",
                         card->btaddr);
        }
        break;

    case PA_STREAM_FAILED:
    default:
        if ((err = pa_context_errno(pactx))) {
            if (stream == card->input.stream)
                type = "input";
            else if (stream == card->output.stream)
                type = "output";
            else
                break;

            if (!(strerr = pa_strerror(err))) {
                mrp_log_error("bluetooth plugin: %s stream error on %s",
                              type, card->btaddr);
            }
            else {
                mrp_log_error("bluetooth plugin: %s stream error on %s: %s",
                              type, card->btaddr, strerr);
            }
        }
        break;
    }
}

static void read_callback(pa_stream *stream, size_t bytes, void *userdata)
{
    card_t *card = (card_t *)userdata;
    device_t *device;
    context_t *ctx;
    pulseif_t *pulseif;
    const void *data;
    size_t size;
    size_t n, i;
    double sample;
    double m;
    int16_t *s;

    MRP_UNUSED(bytes);

    if (!card || !(device = card->device) || !(ctx = device->ctx) ||
        !(pulseif = ctx->pulseif))
        goto confused;

    if (card->input.stream && stream != card->input.stream)
        goto confused;


    pa_stream_peek(stream, &data, &size);

    if (data && size && card->input.stream) {
        if (card->input.state == ST_BEGIN || card->input.state == ST_CLING) {
            n = size / sizeof(int16_t);
            s = (int16_t *)data;
            m = 0.0;

            for (i = 0;   i < n;   i++) {
                sample = (double)s[i];
                m += sample > 0.0 ? sample : -sample;
            }

            m /= (double)n;

            if (card->input.state == ST_BEGIN) {
                if (m > pulseif->limit.upper)
                    card->input.state = ST_CLING;
            }
            else {
                if (m < pulseif->limit.lower) {
                    printf("*** cling ends\n");
                    card->input.state = ST_READY;

                    if (device->audio.buf && device->audio.end > 0)
                        pulseif_add_output_stream_to_card(card);
                }
            }
        }
    }

    if (size)
        pa_stream_drop(stream);

    return;

 confused:
    mrp_log_error("bluetooth plugin: %s() confused with internal "
                  "data structures", __FUNCTION__);
}


static void write_callback(pa_stream *stream, size_t bytes, void *userdata)
{
    static int16_t silence[16000 * sizeof(int16_t)]; /* 1 sec @ 16KHz mono */

    card_t *card = (card_t *)userdata;
    device_t *device;
    context_t *ctx;
    pulseif_t *pulseif;
    size_t size, len, offs;
    int16_t *data;
    srs_audiobuf_t *buf;

    if (!card || !(device = card->device) || !(ctx = device->ctx) ||
        !(pulseif = ctx->pulseif))
        goto confused;

    if (card->output.stream && stream != card->output.stream)
        goto confused;

    size = device->audio.end - device->audio.start;

    while (bytes > 0) {

        if (card->input.state != ST_READY || !(buf = device->audio.buf) ||
            buf->samples <= card->output.sent || size <= card->output.sent)
        {
            len = (sizeof(silence) < bytes) ? sizeof(silence) : bytes;

            if (pa_stream_write(stream,silence,len,NULL,0,PA_SEEK_RELATIVE)<0)
                goto could_not_write;
        }
        else {
            len  = (size - card->output.sent) * sizeof(int16_t);
            offs = device->audio.start + card->output.sent;

            if (len > bytes)
                len = bytes;

            data = (int16_t *)buf->data + offs;

            if (pa_stream_write(stream, data,len, NULL,0,PA_SEEK_RELATIVE) < 0)
                goto could_not_write;

            card->output.sent += len / sizeof(int16_t);
        }

        if (bytes < len)
            bytes = 0;
        else
            bytes -= len;
    }

    return;

 confused:
    mrp_log_error("bluetooth plugin: %s() confused with internal "
                  "data structures", __FUNCTION__);
    return;

 could_not_write:
    mrp_log_error("bluetooth plugin: could not write %zd bytes to stream %s",
                  bytes, device->btaddr);
}


static void subscribe_succes_callback(pa_context *c,int success,void *userdata)
{
    context_t *ctx = (context_t *)userdata;
    pulseif_t *pulseif;

    if (!ctx || !(pulseif = ctx->pulseif) || c != pulseif->pactx) {
        mrp_log_error("bluetooth plugin: confused with internal data "
                      "structures");
        return;
    }

    if (!success) {
        mrp_log_error("bluetooth plugin: failed to subscribe "
                      "pulseaudio events");
    }

    pulseif->subscr = NULL;
}

static void profile_succes_callback(pa_context *c, int success, void *userdata)
{
    context_t *ctx = (context_t *)userdata;
    pulseif_t *pulseif;

    if (!ctx || !(pulseif = ctx->pulseif) || c != pulseif->pactx) {
        mrp_log_error("bluetooth plugin: confused with internal data "
                      "structures");
        return;
    }

    if (!success) {
        mrp_log_error("bluetooth plugin: failed to subscribe "
                      "pulseaudio events");
    }

    pulseif->subscr = NULL;
}

static void context_callback(pa_context *pactx, void *userdata)
{
    static pa_subscription_mask_t mask =
        PA_SUBSCRIPTION_MASK_CARD   |
        PA_SUBSCRIPTION_MASK_SINK   |
        PA_SUBSCRIPTION_MASK_SOURCE ;

    context_t *ctx = (context_t *)userdata;
    pulseif_t *pulseif = ctx->pulseif;
    int err = 0;
    const char *strerr;
    pending_op_t *pend;

    if (!pactx) {
        mrp_log_error("bluetooth plugin: %s() called with zero context",
                      __FUNCTION__);
        return;
    }

    if (pulseif->pactx != pactx) {
        mrp_log_error("bluetooth plugin: %s(): Confused with data structures",
                      __FUNCTION__);
        return;
    }

    switch (pa_context_get_state(pactx)) {

    case PA_CONTEXT_CONNECTING:
        pulseif->conup = false;
        mrp_debug("bleutooth plugin: connecting to pulseaudio server");
        break;

    case PA_CONTEXT_AUTHORIZING:
        pulseif->conup = false;
        mrp_debug("   bluetooth plugin: authorizing");
        break;

    case PA_CONTEXT_SETTING_NAME:
        pulseif->conup = false;
        mrp_debug("   bluetooth plugin: setting name");
        break;

    case PA_CONTEXT_READY:
        pulseif->conup = true;
        pulseif->subscr = pa_context_subscribe(pactx, mask,
                                               subscribe_succes_callback, ctx);
        pend = add_pending_op(ctx);
        pend->op = pa_context_get_card_info_list(pactx,
                                                 card_info_callback, pend);
        pend = add_pending_op(ctx);
        pend->op = pa_context_get_sink_info_list(pactx,
                                                 sink_info_callback, pend);
        pend = add_pending_op(ctx);
        pend->op = pa_context_get_source_info_list(pactx,
                                                   source_info_callback, pend);
        mrp_log_info("bluetooth plugin: pulseaudio connection established");
        break;

    case PA_CONTEXT_TERMINATED:
        mrp_log_info("bluetooth plugin: pulseaudio connection terminated");
        goto disconnect;

    case PA_CONTEXT_FAILED:
    default:
        if ((err = pa_context_errno(pactx)) != 0) {
            if ((strerr = pa_strerror(err)) == NULL)
                strerr = "<unknown>";
            mrp_log_error("bluetooth plugin: pulseaudio server "
                          "connection error: %s", strerr);
        }

    disconnect:
        pulseif->conup = false;
        break;
    }
}


static void event_callback(pa_context *c,
                           pa_subscription_event_type_t t,
                           uint32_t idx,
                           void *userdata)
{
    context_t *ctx;
    pulseif_t *pulseif;
    pa_subscription_event_type_t facility;
    pa_subscription_event_type_t type;
    card_t *card;
    device_t *dev;
    pending_op_t *pend;

    ctx = (context_t *)userdata;
    facility = t & PA_SUBSCRIPTION_EVENT_FACILITY_MASK;
    type = t & PA_SUBSCRIPTION_EVENT_TYPE_MASK;

    if (!ctx || !(pulseif = ctx->pulseif) || c != pulseif->pactx) {
        mrp_log_error("bluetooth plugin: %s() confused with internal"
                      "data structures", __FUNCTION__);
        return;
    }

    switch (facility) {

    case PA_SUBSCRIPTION_EVENT_CARD:
        switch (type) {
        case PA_SUBSCRIPTION_EVENT_NEW:
            mrp_debug("bletooth module: pulseudio module %u appeared\n", idx);
            pend = add_pending_op(ctx);
            pend->op = pa_context_get_card_info_by_index(c, idx,
                                                         card_info_callback,
                                                         pend);
            break;
        case PA_SUBSCRIPTION_EVENT_REMOVE:
            if ((card = find_card_by_index(ctx, idx))) {
                printf("*** card %u gone\n", idx);
                if ((dev = card->device))
                    clients_remove_card_from_device(dev);
                remove_card(card);
            }
            break;
        case PA_SUBSCRIPTION_EVENT_CHANGE:
            break;
        default:
            goto cant_handle_it;
        }
        break;

    case PA_SUBSCRIPTION_EVENT_SINK:
        switch (type) {
        case PA_SUBSCRIPTION_EVENT_NEW:
            mrp_debug("bletooth module: pulseudio sink %u appeared\n", idx);
            pend = add_pending_op(ctx);
            pend->op = pa_context_get_sink_info_by_index(c, idx,
                                                         sink_info_callback,
                                                         pend);
            break;
        case PA_SUBSCRIPTION_EVENT_REMOVE:
            if ((card = find_card_by_sink(ctx, idx))) {
                printf("*** sink %u gone\n", idx);
                mrp_free((void *)card->sink.name);
                card->sink.name = NULL;
                card->sink.idx = -1;
            }
            break;
        case PA_SUBSCRIPTION_EVENT_CHANGE:
            break;
        default:
            goto cant_handle_it;
        }
        break;

    case PA_SUBSCRIPTION_EVENT_SOURCE:
        switch (type) {
        case PA_SUBSCRIPTION_EVENT_NEW:
            mrp_debug("bletooth module: pulseudio source %u appeared\n", idx);
            pend = add_pending_op(ctx);
            pend->op = pa_context_get_source_info_by_index(c, idx,
                                                          source_info_callback,
                                                          pend);
            break;
        case PA_SUBSCRIPTION_EVENT_REMOVE:
            if ((card = find_card_by_source(ctx, idx))) {
                printf("*** source %u gone\n", idx);
                mrp_free((void *)card->source.name);
                card->source.name = NULL;
                card->source.idx = -1;
            }
            break;
        case PA_SUBSCRIPTION_EVENT_CHANGE:
            break;
        default:
            goto cant_handle_it;
        }
        break;

    default:
        goto cant_handle_it;
    }

    return;

 cant_handle_it:
    mrp_log_error("bluetooth plugin: invalid pulseaudio event");
}


static void card_info_callback(pa_context *c,
                               const pa_card_info *i,
                               int eol,
                               void *userdata)
{
    pending_op_t *pend = (pending_op_t *)userdata;
    context_t *ctx;
    card_t *card;
    device_t *dev;
    pa_card_profile_info *p;
    bool has_hfgw_profile;
    const char *btaddr;
    uint32_t j;

    MRP_UNUSED(c);

    if (pend && (ctx = pend->ctx)) {
        if (eol)
            remove_pending_op(pend);
        else if (i && !strncmp(i->name, "bluez_card.", 11)) {
            btaddr = pa_proplist_gets(i->proplist, "device.string");

            for (j = 0, has_hfgw_profile = false;   j < i->n_profiles;   j++) {
                p = i->profiles + j;

                if (!strcmp(p->name, "hfgw") && p->n_sinks && p->n_sources) {
                    has_hfgw_profile = true;
                    break;
                }
            }

            if (btaddr && has_hfgw_profile && (p = i->active_profile)) {
                printf("*** card %u '%s' %s %s\n",
                       i->index, i->name, btaddr, p->name);

                if ((dev = clients_find_device(ctx, btaddr)) && !dev->card &&
                    (card = add_card(ctx, i->index, i->name, btaddr, p->name)))
                {
                    printf("*** card added\n");

                    card->device = dev;
                    clients_add_card_to_device(dev, card);
                }
            }
        }
    }
}

static void source_info_callback(pa_context *c,
                                 const pa_source_info *i,
                                 int eol,
                                 void *userdata)
{
    pending_op_t *pend = (pending_op_t *)userdata;
    context_t *ctx;
    card_t *card;
    device_t *dev;
    modem_t *modem;
    const char *proto;
    const char *btaddr;

    MRP_UNUSED(c);

    if (pend && (ctx = pend->ctx)) {
        if (eol)
            remove_pending_op(pend);
        else if (i && !strncmp(i->name, "bluez_source.", 11)) {
            proto  = pa_proplist_gets(i->proplist, "bluetooth.protocol");
            btaddr = pa_proplist_gets(i->proplist, "device.string");

            if (btaddr && proto && !strcmp(proto, "hfgw")) {
                if ((dev = clients_find_device(ctx, btaddr)) &&
                    (card = dev->card) && (modem = dev->modem))
                {
                    printf("*** source %u %s %s\n", i->index, i->name, btaddr);
                    mrp_free((void *)card->source.name);
                    card->source.name = mrp_strdup(i->name);
                    card->source.idx = i->index;

                    if (modem->state == VOICE_RECOGNITION_ON)
                        pulseif_add_input_stream_to_card(card);
                }
            }
        }
    }
}

static void sink_info_callback(pa_context *c,
                               const pa_sink_info *i,
                               int eol,
                               void *userdata)
{
    pending_op_t *pend = (pending_op_t *)userdata;
    context_t *ctx;
    card_t *card;
    device_t *dev;
    modem_t *modem;
    const char *proto;
    const char *btaddr;

    MRP_UNUSED(c);

    if (pend && (ctx = pend->ctx)) {
        if (eol)
            remove_pending_op(pend);
        else if (i && !strncmp(i->name, "bluez_sink.", 11)) {
            proto  = pa_proplist_gets(i->proplist, "bluetooth.protocol");
            btaddr = pa_proplist_gets(i->proplist, "device.string");

            if (btaddr && proto && !strcmp(proto, "hfgw")) {
                if ((dev = clients_find_device(ctx, btaddr)) &&
                    (card = dev->card) && (modem = dev->modem))
                {
                    printf("*** sink %u %s %s\n", i->index, i->name, btaddr);
                    mrp_free((void *)card->sink.name);
                    card->sink.name = mrp_strdup(i->name);
                    card->sink.idx = i->index;

                    if (modem->state == VOICE_RECOGNITION_ON &&
                        card->input.state == ST_READY)
                    {
                        pulseif_add_output_stream_to_card(card);
                    }
                }
            }
        }
    }
}

/*
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 *
 */
