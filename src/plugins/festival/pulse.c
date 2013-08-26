#include <errno.h>

#include <murphy/common/debug.h>
#include <murphy/common/log.h>
#include <murphy/common/mm.h>
#include <murphy/common/list.h>
#include <murphy/common/refcnt.h>

#include "src/plugins/festival/festival-voice.h"
#include "src/plugins/festival/pulse.h"

#define SPEECH "speech"
#define TTS    "text-to-speech"

typedef struct {
    festival_t      *f;                  /* festival voice context */
    pa_mainloop_api *pa;                 /* PA mainloop API */
    pa_context      *pc;                 /* PA context */
    uint32_t         strmid;             /* next stream id */
    mrp_list_hook_t  streams;            /* active streams */
    int              connected;          /* whether connection is up */
    mrp_timer_t     *reconn;             /* reconnect timer */
} pulse_t;


typedef struct {
    pulse_t           *p;                /* our pulse_t context */
    pa_stream         *s;                /* associated PA stream */
    void              *buf;              /* pre-generated sample buffer */
    size_t             size;             /* buffer size */
    size_t             offs;             /* offset to next sample */
    uint32_t           msec;             /* length in milliseconds */
    int                rate;             /* sample rate */
    int                nchannel;         /* number of channels */
    uint32_t           nsample;          /* number of samples */
    uint32_t           id;               /* our stream id */
    int                event_mask;       /* mask of watched events */
    int                fired_mask;       /* mask of delivered events */
    pulse_stream_cb_t  cb;               /* notification callback */
    void              *user_data;        /* callback user data */
    mrp_list_hook_t    hook;             /* hook to list of streams */
    mrp_refcnt_t       refcnt;           /* reference count */
    int                stopped : 1;      /* stopped marker */
    pa_operation      *drain;            /* draining operation */
} stream_t;


static void context_state_cb(pa_context *pc, void *user_data);
static void context_event_cb(pa_context *pc, pa_subscription_event_type_t e,
                             uint32_t idx, void *user_data);
static void stream_state_cb(pa_stream *s, void *user_data);
static void stream_write_cb(pa_stream *s, size_t size, void *user_data);
static void stream_drain_cb(pa_stream *ps, int success, void *user_data);

static void stream_drain(stream_t *s);
static void stream_notify(stream_t *s, srs_voice_event_type_t event);


int pulse_setup(festival_t *f)
{
    pulse_t *p;

    if ((p = mrp_allocz(sizeof(*p))) == NULL)
        return -1;

    mrp_list_init(&p->streams);
    p->f  = f;
    p->pa = f->srs->pa;
    p->pc = pa_context_new(p->pa, "festival");

    if (p->pc == NULL) {
        mrp_free(p);

        return -1;
    }

    f->pulse  = p;
    p->strmid = 1;

    pa_context_set_state_callback(p->pc, context_state_cb, p);
    pa_context_set_subscribe_callback(p->pc, context_event_cb, p);
    pa_context_connect(p->pc, NULL, PA_CONTEXT_NOAUTOSPAWN, NULL);

    return 0;
}


void pulse_cleanup(festival_t *f)
{
    pulse_t *p = (pulse_t *)f->pulse;

    if (p->pc != NULL) {
        pa_context_disconnect(p->pc);
        p->pc = NULL;
    }
}


static void stream_destroy(stream_t *s)
{
    mrp_debug("destroying stream #%d", s->id);

    mrp_list_delete(&s->hook);

    if (s->s != NULL) {
        pa_stream_set_state_callback(s->s, NULL, NULL);
        pa_stream_set_write_callback(s->s, NULL, NULL);
        pa_stream_disconnect(s->s);
        pa_stream_unref(s->s);
        s->s = NULL;
    }

    mrp_free(s);
}


static inline stream_t *stream_ref(stream_t *s)
{
    if (s != NULL) {
        mrp_ref_obj(s, refcnt);
        mrp_debug("stream reference count increased to %d", s->refcnt);
    }

    return s;
}


static inline void stream_unref(stream_t *s)
{
    if (mrp_unref_obj(s, refcnt))
        stream_destroy(s);
    else
        mrp_debug("stream reference count decreased to %d", s->refcnt);
}


uint32_t pulse_play_stream(festival_t *f, void *sample_buf, int sample_rate,
                           int nchannel, uint32_t nsample, char **tags,
                           int event_mask, pulse_stream_cb_t cb,
                           void *user_data)
{
    pulse_t         *p    = (pulse_t *)f->pulse;
    char           **t;
    stream_t        *s;
    pa_sample_spec   ss;
    pa_buffer_attr   ba;
    pa_proplist     *props;
    size_t           pamin, pabuf;
    uint32_t         min, tgt, id;
    int              flags;

    if ((s = mrp_allocz(sizeof(*s))) == NULL)
        return 0;

    mrp_list_init(&s->hook);
    mrp_refcnt_init(&s->refcnt);

    if (tags != NULL) {
        if ((props = pa_proplist_new()) == NULL) {
            mrp_free(s);
            return 0;
        }

        pa_proplist_sets(props, PA_PROP_MEDIA_ROLE, SPEECH);

        for (t = tags; *t; t++)
            pa_proplist_setp(props, *t);
    }
    else
        props = NULL;

    memset(&ss, 0, sizeof(ss));
    ss.format   = PA_SAMPLE_S16LE;
    ss.rate     = sample_rate;
    ss.channels = nchannel;

    pamin  = pa_usec_to_bytes(100 * PA_USEC_PER_MSEC, &ss);
    pabuf  = pa_usec_to_bytes(300 * PA_USEC_PER_MSEC, &ss);

    ba.maxlength = -1;
    ba.tlength   = pabuf;
    ba.minreq    = pamin;
    ba.prebuf    = pabuf;
    ba.fragsize  = -1;

    s->s = pa_stream_new_with_proplist(p->pc, TTS, &ss, NULL, props);
    if (props != NULL)
        pa_proplist_free(props);

    if (s->s == NULL) {
        mrp_free(s);
        return 0;
    }

    s->p          = p;
    s->buf        = sample_buf;
    s->offs       = 0;
    s->rate       = sample_rate;
    s->nchannel   = nchannel;
    s->nsample    = nsample;
    s->size       = 2 * nsample * nchannel;
    s->msec       = (1.0 * nsample) / sample_rate * 1000;
    s->cb         = cb;
    s->user_data  = user_data;
    s->id         = p->strmid++;
    s->event_mask = event_mask;

    pa_stream_set_state_callback(s->s, stream_state_cb, s);
    pa_stream_set_write_callback(s->s, stream_write_cb, s);

    flags = PA_STREAM_ADJUST_LATENCY;
    pa_stream_connect_playback(s->s, NULL, &ba, flags, NULL, NULL);

    mrp_list_append(&p->streams, &s->hook);

    return s->id;
}


static void stream_stop(stream_t *s, int drain, int notify)
{
    if (s->stopped)
        return;
    else
        s->stopped = TRUE;

    if (!notify)
        s->event_mask = 0;

    if (!drain) {
        stream_notify(s, s->offs >= s->size ?
                      PULSE_STREAM_COMPLETED : PULSE_STREAM_ABORTED);
    }
    else
        stream_drain(s);

    stream_unref(s);                     /* remove intial reference */
}


int pulse_stop_stream(festival_t *f, uint32_t id, int drain, int notify)
{
    pulse_t         *p = (pulse_t *)f->pulse;
    mrp_list_hook_t *sp, *sn;
    stream_t        *se, *s;

    mrp_debug("stopping stream #%u", id);

    s = NULL;
    mrp_list_foreach(&p->streams, sp, sn) {
        se = mrp_list_entry(sp, typeof(*se), hook);

        if (se->id == id) {
            s = se;
            break;
        }
    }

    if (s == NULL) {
        errno = ENOENT;
        return -1;
    }

    stream_stop(s, drain, notify);

    return 0;
}


static void connect_timer_cb(mrp_timer_t *t, void *user_data)
{
    pulse_t *p = (pulse_t *)user_data;

    if (p->pc != NULL) {
        pa_context_unref(p->pc);
        p->pc = NULL;
    }

    p->pc = pa_context_new(p->pa, "festival");

    pa_context_set_state_callback(p->pc, context_state_cb, p);
    pa_context_set_subscribe_callback(p->pc, context_event_cb, p);
    pa_context_connect(p->pc, NULL, PA_CONTEXT_NOAUTOSPAWN, NULL);

    p->reconn = NULL;
    mrp_del_timer(t);
}


static void stop_reconnect(pulse_t *p)
{
    if (p->reconn != NULL) {
        mrp_del_timer(p->reconn);
        p->reconn = NULL;
    }
}


static void start_reconnect(pulse_t *p)
{
    stop_reconnect(p);

    p->reconn = mrp_add_timer(p->f->srs->ml, 5000, connect_timer_cb, p);
}


static void context_state_cb(pa_context *pc, void *user_data)
{
    pulse_t *p = (pulse_t *)user_data;

    switch (pa_context_get_state(pc)) {
    case PA_CONTEXT_CONNECTING:
        mrp_debug("PA connection: being established...");
        p->connected = FALSE;
        stop_reconnect(p);
        break;

    case PA_CONTEXT_AUTHORIZING:
        mrp_debug("PA connection: being authenticated...");
        p->connected = FALSE;
        break;

    case PA_CONTEXT_SETTING_NAME:
        mrp_debug("PA connection: setting name...");
        p->connected = FALSE;
        break;

    case PA_CONTEXT_READY:
        mrp_log_info("festival: PA connection up and ready");
        p->connected = TRUE;
        break;

    case PA_CONTEXT_TERMINATED:
        mrp_log_info("festival: PA connection terminated");
        p->connected = FALSE;
        start_reconnect(p);
        break;

    case PA_CONTEXT_FAILED:
        mrp_log_error("festival: PA connetion failed");
    default:
        p->connected = FALSE;
        start_reconnect(p);
        break;
    }
}


static void context_event_cb(pa_context *pc, pa_subscription_event_type_t e,
                             uint32_t idx, void *user_data)
{
    pulse_t *p = (pulse_t *)user_data;

    return;
}


static void stream_notify(stream_t *s, srs_voice_event_type_t event)
{
    int                mask = (1 << event);
    srs_voice_event_t  e;
    uint32_t           id;

    if (s->cb == NULL || !(s->event_mask & mask))
        return;

    if ((mask & PULSE_MASK_ONESHOT) && (s->fired_mask & mask))
        return;

    e.type = event;
    e.id   = s->id;

    switch (event) {
    case PULSE_STREAM_STARTED:
        e.data.progress.pcnt = 0;
        e.data.progress.msec = 0;
        break;

    case PULSE_STREAM_PROGRESS:
        e.data.progress.pcnt = ((1.0 * s->offs) / s->size) * 100;
        e.data.progress.msec = ((1.0 * s->offs) / s->size) * s->msec;
        break;

    case PULSE_STREAM_COMPLETED:
        e.data.progress.pcnt = ((1.0 * s->offs) / s->size) * 100;
        e.data.progress.msec = ((1.0 * s->offs) / s->size) * s->msec;
        break;

    case PULSE_STREAM_ABORTED:
        e.data.progress.pcnt = 0;
        e.data.progress.msec = 0;
        break;

    default:
        return;
    }

    stream_ref(s);
    s->cb(s->p->f, &e, s->user_data);
    stream_unref(s);
}


static void stream_state_cb(pa_stream *ps, void *user_data)
{
    stream_t           *s   = (stream_t *)user_data;
    pulse_t            *p   = s->p;
    pa_context_state_t  cst = pa_context_get_state(p->pc);
    pa_stream_state_t   sst;

    if (cst == PA_CONTEXT_TERMINATED || cst == PA_CONTEXT_FAILED)
        return;

    stream_ref(s);

    switch ((sst = pa_stream_get_state(s->s))) {
    case PA_STREAM_CREATING:
        mrp_debug("stream #%u being created", s->id);
        break;

    case PA_STREAM_READY:
        mrp_debug("stream #%u ready", s->id);
        stream_notify(s, PULSE_STREAM_STARTED);
        break;

    case PA_STREAM_TERMINATED:
    case PA_STREAM_FAILED:
    default:
        mrp_debug("stream #%u state %d", s->id, sst);

        pa_stream_disconnect(s->s);
        pa_stream_set_state_callback(s->s, NULL, NULL);
        pa_stream_set_write_callback(s->s, NULL, NULL);

        if (sst == PA_STREAM_TERMINATED)
            stream_notify(s, PULSE_STREAM_COMPLETED);
        else
            stream_notify(s, PULSE_STREAM_ABORTED);
    }

    stream_unref(s);
}


static void stream_drain(stream_t *s)
{
    if (s->drain == NULL) {
        mrp_debug("stream #%u done, draining", s->id);
        stream_ref(s);
        s->drain = pa_stream_drain(s->s, stream_drain_cb, s);
    }
}


static void stream_drain_cb(pa_stream *ps, int success, void *user_data)
{
    stream_t *s = (stream_t *)user_data;

    mrp_debug("stream #%u drained %s", s->id,
              success ? "successfully" : "failed");

    pa_operation_unref(s->drain);
    s->drain = NULL;
    stream_notify(s, PULSE_STREAM_COMPLETED);
    stream_unref(s);
}


static void stream_write_cb(pa_stream *ps, size_t size, void *user_data)
{
    stream_t *s = (stream_t *)user_data;
    int       done;

    stream_notify(s, PULSE_STREAM_PROGRESS);

    if (s->offs == s->size) {
        pa_stream_set_write_callback(s->s, NULL, NULL);
        return;
    }

    stream_ref(s);

    if (s->offs + size >= s->size) {
        size = s->size - s->offs;
        done = TRUE;
    }
    else
        done = FALSE;

    if (pa_stream_write(s->s, s->buf + s->offs, size, NULL, 0,
                        PA_SEEK_RELATIVE) < 0) {
        mrp_log_error("festival: failed to write %zd bytes", size);
        goto out;
    }
    else {
        s->offs += size;

        if (done)
            stream_stop(s, TRUE, TRUE);
    }

 out:
    stream_unref(s);
}
