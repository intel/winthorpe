#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

#include <pulse/pulseaudio.h>
#include <pulse/mainloop.h>

#include <sphinxbase/err.h>
#include <sphinxbase/ad.h>
#include <sphinxbase/cont_ad.h>

#include <pocketsphinx.h>

#include <murphy/common/mm.h>
#include <murphy/common/log.h>

#include "pulse-interface.h"
#include "options.h"
#include "decoder-set.h"
#include "filter-buffer.h"
#include "input-buffer.h"


static void connect_to_server(context_t *ctx);
static int  stream_create(context_t *);

static void state_callback(pa_stream *, void *);
static void read_callback(pa_stream *, size_t, void *);
static void context_callback(pa_context *, void *);
static void event_callback(pa_context *, pa_subscription_event_type_t,
                           uint32_t, void *);


int pulse_interface_create(context_t *ctx, pa_mainloop_api *api)
{
    pulse_interface_t *pulseif;

    if (!(pulseif = mrp_allocz(sizeof(pulse_interface_t))))
        goto failed;

    if (pa_signal_init(api) < 0)
        goto failed;

    pulseif->api = api;

    ctx->pulseif = pulseif;

    connect_to_server(ctx);

    return 0;

 failed:
    if (pulseif)
        mrp_free(pulseif);
    return -1;
}

void pulse_interface_destroy(context_t *ctx)
{
    pulse_interface_t *pulseif;

    if (ctx && (pulseif = ctx->pulseif)) {
        ctx->pulseif = NULL;

        if (pulseif->stream) {
            pa_stream_set_state_callback(pulseif->stream, NULL, NULL);
            pa_stream_set_underflow_callback(pulseif->stream, NULL, NULL);
            pa_stream_set_suspended_callback(pulseif->stream, NULL, NULL);
            pa_stream_set_read_callback(pulseif->stream, NULL, NULL);
        }

        if (pulseif->pactx) {
            pa_context_set_state_callback(pulseif->pactx, NULL, NULL);
            pa_context_set_subscribe_callback(pulseif->pactx, NULL, NULL);
            pa_context_unref(pulseif->pactx);
        }

        mrp_free(pulseif);
    }
}

void pulse_interface_cork_input_stream(context_t *ctx, bool cork)
{
    pulse_interface_t *pulseif;

    if (!ctx || !(pulseif = ctx->pulseif))
        return;

    if ((cork && !pulseif->corked) || (!cork && pulseif->corked)) {
        pulseif->corked = cork;

        if (pulseif->stream && pulseif->conup)
            pa_stream_cork(pulseif->stream, cork, NULL, NULL);
    }

    return;
}

static void connect_to_server(context_t *ctx)
{
    pulse_interface_t *pulseif = ctx->pulseif;
    pa_mainloop_api *api = pulseif->api;
    pa_context *pactx;

    if (pulseif->pactx) {
        pa_context_set_state_callback(pulseif->pactx, NULL, NULL);
        pa_context_set_subscribe_callback(pulseif->pactx, NULL, NULL);
        pa_context_unref(pulseif->pactx);
        pulseif->pactx = NULL;
    }

    if (!(pulseif->pactx = pactx = pa_context_new(api, "sphinx"))) {
        mrp_log_error("pa_context_new() failed");
        return;
    }

    pa_context_set_state_callback(pactx, context_callback, ctx);
    pa_context_set_subscribe_callback(pactx, event_callback, ctx);

    mrp_log_error("Trying to connect to pulseaudio ...");
    pa_context_connect(pactx, NULL, PA_CONTEXT_NOAUTOSPAWN, NULL);
}

static int stream_create(context_t *ctx)
{
    options_t *opts = ctx->opts;
    pulse_interface_t *pulseif = ctx->pulseif;
    input_buf_t *inpbuf = ctx->inpbuf;
    double rate = opts->rate;
    double dsilen = opts->silen;
    const char *source = opts->srcnam;
    uint32_t minreq = 100;      /* length in msecs */
    uint32_t target = 1000;     /* length in msecs */
    uint32_t filtmax = 30000;   /* length in msec */
    pa_sample_spec spec;
    pa_buffer_attr battr;
    uint32_t tlength;
    pa_stream_flags_t flags;
    pa_proplist *pl;
    size_t bufsiz, calsiz, size, hwm, extra, silsiz, minsiz;
    int32_t silen;

    if (rate < 8000.0 || rate > 48000.0) {
        mrp_log_error("sphinx plugin: invalid sample rate %.1lf KHz",
                      rate / 1000.0);
        return -1;
    }

    if (!pulseif->conup) {
        mrp_log_error("sphinx plugin: attempt to create input stream "
                      "when not connected");
        return -1;
    }

    if (!pulseif->stream) {
        memset(&spec, 0, sizeof(spec));
        spec.format = PA_SAMPLE_S16LE;
        spec.rate = rate;
        spec.channels = 1; /* ie. MONO */

        minsiz = pa_usec_to_bytes(minreq * PA_USEC_PER_MSEC, &spec);
        silen = pa_usec_to_bytes(dsilen*PA_USEC_PER_SEC, &spec)/sizeof(int16);

        bufsiz = pa_usec_to_bytes(filtmax * PA_USEC_PER_MSEC, &spec);
        calsiz = cont_ad_calib_size(inpbuf->cont) * sizeof(int16);
        hwm = (bufsiz > calsiz) ? bufsiz : calsiz;
        silsiz = silen * sizeof(int16);
        extra = ((minsiz * 2 > silsiz) ? minsiz * 2 : silsiz) + minsiz;
        size = hwm + extra;

        if (ctx->verbose) {
            mrp_debug("sphinx plugin: calibration requires %u samples "
                      "(%.3lf sec)", calsiz / sizeof(int16),
                      (double)(calsiz / sizeof(int16)) / (double)rate);
        }

        filter_buffer_initialize(ctx, size / sizeof(int16),
                                 hwm / sizeof(int16), silen);

        pl = pa_proplist_new();
        pa_proplist_sets(pl, PA_PROP_MEDIA_ROLE, "speech");

        pulseif->stream = pa_stream_new_with_proplist(pulseif->pactx,
                                                      "speech-recognition",
                                                      &spec, NULL, pl);
        pa_proplist_free(pl);

        if (!pulseif->stream) {
            mrp_log_error("failed to create input stream");
            return -1;
        }

        if (target < (minreq * 3))
            target = minreq * 3;

        tlength = pa_usec_to_bytes(target * PA_USEC_PER_MSEC, &spec);
        size = (tlength > calsiz ? tlength : calsiz) + minsiz * 3;

        input_buffer_initialize(ctx, size, minsiz);

        battr.maxlength = -1;       /* default (4MB) */
        battr.tlength   = tlength;
        battr.minreq    = minsiz;
        battr.prebuf    = 2 * tlength;
        battr.fragsize  = tlength;

        flags = PA_STREAM_ADJUST_LATENCY;

        pa_stream_set_state_callback(pulseif->stream, state_callback, ctx);
#if 0
        pa_stream_set_underflow_callback(pulseif->stream, underflow_callback,
                                         ctx);
        pa_stream_set_suspended_callback(pulseif->stream, suspended_callback,
                                         ctx);
#endif
        pa_stream_set_read_callback(pulseif->stream, read_callback, ctx);

        pa_stream_connect_record(pulseif->stream, source, &battr, flags);
    }

    return 0;
}


static void state_callback(pa_stream *stream, void *userdata)
{
#define CHECK_STREAM(pif,s) if (!pif || pif->stream != s) { goto confused; }

    context_t *ctx = (context_t *)userdata;
    pulse_interface_t *pulseif = ctx->pulseif;
    pa_context *pactx = pa_stream_get_context(stream);
    pa_context_state_t ctxst = pa_context_get_state(pactx);
    int err;
    const char *strerr;

    if (ctxst != PA_CONTEXT_TERMINATED && ctxst != PA_CONTEXT_FAILED) {

        switch (pa_stream_get_state(stream)) {

        case PA_STREAM_CREATING:
            CHECK_STREAM(pulseif, stream);
            mrp_debug("sphinx plugin: pulseaudio input stream creating");
            break;

        case PA_STREAM_TERMINATED:
            CHECK_STREAM(pulseif, stream);
            mrp_log_info("sphinx plugin: pulseaudio input stream terminated");
            pulseif->stream = NULL;
            break;

        case PA_STREAM_READY:
            CHECK_STREAM(pulseif, stream);
            mrp_log_info("sphinx plugin: pulseaudio input stream is ready");
            break;

        case PA_STREAM_FAILED:
        default:
            if ((err = pa_context_errno(pactx))) {
                if (!(strerr = pa_strerror(err))) {
                    mrp_log_error("sphinx plugin: pulseaudio input stream "
                                  "error");
                }
                else {
                    mrp_log_error("sphinx plugin: pulseaudio input stream "
                                  "error: %s", strerr);
                }
            }
            break;
        }
    }

    return;

 confused:
    mrp_log_error("sphinx plugin: %s() confused with data structures",
                  __FUNCTION__);
    return;

#undef CHECK_STREAM
}

static void read_callback(pa_stream *stream, size_t bytes, void *userdata)
{
    context_t *ctx = (context_t *)userdata;
    pulse_interface_t *pulseif = ctx->pulseif;
    const void *data;
    size_t size;

    MRP_UNUSED(bytes);

    if (pulseif->stream != stream) {
        mrp_log_error("sphinx plugin: %s() confused with internal "
                      "data structures", __FUNCTION__);
        return;
    }

    pa_stream_peek(stream, &data, &size);

    if (data && size && !pulseif->corked)
        input_buffer_process_data(ctx, data, size);

    if (size)
        pa_stream_drop(stream);
}

static void context_callback(pa_context *pactx, void *userdata)
{
    context_t *ctx = (context_t *)userdata;
    pulse_interface_t *pulseif = ctx->pulseif;
    int err = 0;
    const char *strerr;

    if (!pactx) {
        mrp_log_error("sphinx plugin: %s() called with zero context",
                      __FUNCTION__);
        return;
    }

    if (pulseif->pactx != pactx) {
        mrp_log_error("sphinx plugin: %s(): Confused with data structures",
                      __FUNCTION__);
        return;
    }

    switch (pa_context_get_state(pactx)) {

    case PA_CONTEXT_CONNECTING:
        pulseif->conup = false;
        mrp_debug("sphinx plugin: connecting to pulseaudio server");
        break;

    case PA_CONTEXT_AUTHORIZING:
        pulseif->conup = false;
        mrp_debug("   sphinx plugin: authorizing");
        break;

    case PA_CONTEXT_SETTING_NAME:
        pulseif->conup = false;
        mrp_debug("   sphinx plugin: setting name");
        break;

    case PA_CONTEXT_READY:
        pulseif->conup = true;
        mrp_log_info("sphinx plugin: pulseaudio connection established");
        stream_create(ctx);
        break;

    case PA_CONTEXT_TERMINATED:
        mrp_log_info("sphinx plugin: pulseaudio connection terminated");
        goto disconnect;

    case PA_CONTEXT_FAILED:
    default:
        if ((err = pa_context_errno(pactx)) != 0) {
            if ((strerr = pa_strerror(err)) == NULL)
                strerr = "<unknown>";
            mrp_log_error("sphinx plugin: pulseaudio server connection "
                          "error: %s", strerr);
        }

    disconnect:
        pulseif->conup = false;
        break;
    }
}

static void event_callback(pa_context *pactx,pa_subscription_event_type_t type,
                           uint32_t idx, void *userdata)
{
    context_t *ctx = (context_t *)userdata;
    pulse_interface_t *pulseif = ctx->pulseif;

    (void)idx;

    if (pulseif->pactx != pactx) {
        mrp_log_error("sphinx plugin: %s() confused with data structures",
                      __FUNCTION__);
    }
    else {
        switch (type) {

        case PA_SUBSCRIPTION_EVENT_SOURCE:
            mrp_debug("sphinx plugin: event source");
            break;

        case PA_SUBSCRIPTION_EVENT_SOURCE_OUTPUT:
            mrp_debug("sphinx plugin: event source output");
            break;

        default:
            mrp_debug("sphinx plugin: event %d", type);
            break;
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
