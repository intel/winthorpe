#include <errno.h>

#include <murphy/common/debug.h>
#include <murphy/common/log.h>
#include <murphy/common/mm.h>
#include <murphy/common/list.h>
#include <murphy/common/refcnt.h>

#include "pulse.h"

#define SPEECH "speech"
#define TTS    "text-to-speech"

struct srs_pulse_s {
    pa_mainloop_api *pa;                 /* PA mainloop API */
    char            *name;               /* PA context name */
    pa_context      *pc;                 /* PA context */
    uint32_t         strmid;             /* next stream id */
    mrp_list_hook_t  streams;            /* active streams */
    int              connected;          /* whether connection is up */
    pa_time_event   *reconn;             /* reconnect timer */
};


typedef struct {
    srs_pulse_t       *p;                /* our pulse_t context */
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
    srs_stream_cb_t    cb;               /* notification callback */
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


srs_pulse_t *srs_pulse_setup(pa_mainloop_api *pa, const char *name)
{
    srs_pulse_t *p;

    if ((p = mrp_allocz(sizeof(*p))) == NULL)
        return NULL;

    mrp_list_init(&p->streams);
    p->pa   = pa;
    p->name = name ? mrp_strdup(name) : mrp_strdup("Winthorpe");
    p->pc   = pa_context_new(p->pa, p->name);

    if (p->pc == NULL) {
        mrp_free(p);

        return NULL;
    }

    mrp_log_info("pulse: trying to connect to server...");

    p->strmid = 1;

    pa_context_set_state_callback(p->pc, context_state_cb, p);
    pa_context_set_subscribe_callback(p->pc, context_event_cb, p);
    pa_context_connect(p->pc, NULL, PA_CONTEXT_NOFAIL, NULL);

    return p;
}


void srs_pulse_cleanup(srs_pulse_t *p)
{
    if (p->pc != NULL) {
        pa_context_disconnect(p->pc);
        p->pc = NULL;
        mrp_free(p->name);
        p->name = NULL;
        mrp_free(p);
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
        mrp_free(s->buf);
        s->buf = NULL;
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


uint32_t srs_play_stream(srs_pulse_t *p, void *sample_buf, int sample_rate,
                         int nchannel, uint32_t nsample, char **tags,
                         int event_mask, srs_stream_cb_t cb,
                         void *user_data)
{
    char           **t;
    stream_t        *s;
    pa_sample_spec   ss;
    pa_buffer_attr   ba;
    pa_proplist     *props;
    size_t           pamin, pabuf;
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
                      SRS_STREAM_EVENT_COMPLETED : SRS_STREAM_EVENT_ABORTED);
    }
    else
        stream_drain(s);

    stream_unref(s);                     /* remove intial reference */
}


int srs_stop_stream(srs_pulse_t *p, uint32_t id, int drain, int notify)
{
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


static void connect_timer_cb(pa_mainloop_api *api, pa_time_event *e,
                             const struct timeval *tv, void *user_data)
{
    srs_pulse_t *p = (srs_pulse_t *)user_data;

    if (p->pc != NULL) {
        pa_context_unref(p->pc);
        p->pc = NULL;
    }

    p->pc = pa_context_new(p->pa, p->name);

    pa_context_set_state_callback(p->pc, context_state_cb, p);
    pa_context_set_subscribe_callback(p->pc, context_event_cb, p);
    pa_context_connect(p->pc, NULL, PA_CONTEXT_NOFAIL, NULL);

    p->pa->time_free(p->reconn);
    p->reconn = NULL;
}


static void stop_reconnect(srs_pulse_t *p)
{
    if (p->reconn != NULL) {
        p->pa->time_free(p->reconn);
        p->reconn = NULL;
    }
}


static void start_reconnect(srs_pulse_t *p)
{
    struct timeval tv;

    stop_reconnect(p);

    pa_timeval_add(pa_gettimeofday(&tv), 5000);

    p->reconn = p->pa->time_new(p->pa, &tv, connect_timer_cb, p);
}


static void context_state_cb(pa_context *pc, void *user_data)
{
    srs_pulse_t *p = (srs_pulse_t *)user_data;

    switch (pa_context_get_state(pc)) {
    case PA_CONTEXT_CONNECTING:
        mrp_debug("pulse: connection being established...");
        p->connected = FALSE;
        stop_reconnect(p);
        break;

    case PA_CONTEXT_AUTHORIZING:
        mrp_debug("pulse: connection being authenticated...");
        p->connected = FALSE;
        break;

    case PA_CONTEXT_SETTING_NAME:
        mrp_debug("pulse: setting connection name...");
        p->connected = FALSE;
        break;

    case PA_CONTEXT_READY:
        mrp_log_info("pulse: connection up and ready");
        p->connected = TRUE;
        break;

    case PA_CONTEXT_TERMINATED:
        mrp_log_info("pulse: connection terminated");
        p->connected = FALSE;
        start_reconnect(p);
        break;

    case PA_CONTEXT_FAILED:
        mrp_log_error("pulse: connetion failed");
    default:
        p->connected = FALSE;
        start_reconnect(p);
        break;
    }
}


static void context_event_cb(pa_context *pc, pa_subscription_event_type_t e,
                             uint32_t idx, void *user_data)
{
    srs_pulse_t *p = (srs_pulse_t *)user_data;

    MRP_UNUSED(pc);
    MRP_UNUSED(e);
    MRP_UNUSED(idx);
    MRP_UNUSED(user_data);

    MRP_UNUSED(p);

    return;
}


static void stream_notify(stream_t *s, srs_voice_event_type_t event)
{
    int                mask = (1 << event);
    srs_voice_event_t  e;

    if (s->cb == NULL || !(s->event_mask & mask))
        return;

    if ((mask & SRS_STREAM_MASK_ONESHOT) && (s->fired_mask & mask))
        return;

    e.type = event;
    e.id   = s->id;

    switch (event) {
    case SRS_STREAM_EVENT_STARTED:
        e.data.progress.pcnt = 0;
        e.data.progress.msec = 0;
        break;

    case SRS_STREAM_EVENT_PROGRESS:
        e.data.progress.pcnt = ((1.0 * s->offs) / s->size) * 100;
        e.data.progress.msec = ((1.0 * s->offs) / s->size) * s->msec;
        break;

    case SRS_STREAM_EVENT_COMPLETED:
        e.data.progress.pcnt = ((1.0 * s->offs) / s->size) * 100;
        e.data.progress.msec = ((1.0 * s->offs) / s->size) * s->msec;
        break;

    case SRS_STREAM_EVENT_ABORTED:
        e.data.progress.pcnt = 0;
        e.data.progress.msec = 0;
        break;

    default:
        return;
    }

    stream_ref(s);
    s->cb(s->p, &e, s->user_data);
    stream_unref(s);
}


static void stream_state_cb(pa_stream *ps, void *user_data)
{
    stream_t           *s   = (stream_t *)user_data;
    srs_pulse_t        *p   = s->p;
    pa_context_state_t  cst = pa_context_get_state(p->pc);
    pa_stream_state_t   sst;

    if (cst == PA_CONTEXT_TERMINATED || cst == PA_CONTEXT_FAILED)
        return;

    stream_ref(s);

    switch ((sst = pa_stream_get_state(s->s))) {
    case PA_STREAM_CREATING:
        mrp_debug("pulse: stream #%u being created", s->id);
        break;

    case PA_STREAM_READY:
        mrp_debug("pulse: stream #%u ready", s->id);
        stream_notify(s, SRS_STREAM_EVENT_STARTED);
        break;

    case PA_STREAM_TERMINATED:
    case PA_STREAM_FAILED:
    default:
        mrp_debug("pulse: stream #%u state %d", s->id, sst);

        pa_stream_disconnect(s->s);
        pa_stream_set_state_callback(s->s, NULL, NULL);
        pa_stream_set_write_callback(s->s, NULL, NULL);

        if (sst == PA_STREAM_TERMINATED)
            stream_notify(s, SRS_STREAM_EVENT_COMPLETED);
        else
            stream_notify(s, SRS_STREAM_EVENT_ABORTED);
    }

    stream_unref(s);
}


static void stream_drain(stream_t *s)
{
    if (s->drain == NULL) {
        mrp_debug("pulse: stream #%u done, draining", s->id);
        stream_ref(s);
        s->drain = pa_stream_drain(s->s, stream_drain_cb, s);
    }
}


static void stream_drain_cb(pa_stream *ps, int success, void *user_data)
{
    stream_t *s = (stream_t *)user_data;

    mrp_debug("pulse: stream #%u drained %s", s->id,
              success ? "successfully" : "failed");

    pa_operation_unref(s->drain);
    s->drain = NULL;
    stream_notify(s, SRS_STREAM_EVENT_COMPLETED);
    stream_unref(s);
}


static void stream_write_cb(pa_stream *ps, size_t size, void *user_data)
{
    stream_t *s = (stream_t *)user_data;
    int       done;

    stream_notify(s, SRS_STREAM_EVENT_PROGRESS);

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
        mrp_log_error("pulse: failed to write %zd bytes to stream #%u",
                      size, s->id);
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
