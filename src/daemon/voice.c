/*
 * Copyright (c) 2012 - 2013, Intel Corporation
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

#include <unistd.h>
#include <errno.h>

#include <murphy/common/macros.h>
#include <murphy/common/mm.h>
#include <murphy/common/list.h>

#include "srs/daemon/context.h"
#include "srs/daemon/voice.h"

typedef struct state_s state_t;

/*
 * a speech synthesizer backend
 */

typedef struct {
    int                id;               /* internal backend ID */
    srs_context_t     *srs;              /* main context */
    char              *name;             /* engine name */
    srs_voice_api_t    api;              /* backend API */
    void              *api_data;         /* opaque engine data */
    srs_voice_actor_t *actors;           /* backend voice actors */
    int                nactor;           /* number of actors */
    mrp_list_hook_t    hook;             /* to list of backends */
    state_t           *state;            /* our state */
} renderer_t;


/*
 * languages
 */

typedef struct {
    char            *lang;               /* language name */
    mrp_list_hook_t  hook;               /* to list of languages */
    mrp_list_hook_t  actors;             /* list of actors */
    int              nmale;              /* number of male actors */
    int              nfemale;            /* number of female actors */
} language_t;


/*
 * actors
 */

typedef struct {
    char               *voice;           /* globally unique voice id */
    renderer_t         *r;               /* rendering backend */
    int                 id;              /* backend voice id */
    char               *dialect;         /* language dialect, if any */
    srs_voice_gender_t  gender;          /* actor gender */
    int                 age;             /* actor age */
    char               *description;     /* description */
    mrp_list_hook_t     hook;            /* to list of languages */
} actor_t;


/*
 * ative and queued rendering requests
 */

typedef struct {
    uint32_t            id;              /* request id */
    renderer_t         *r;               /* rendering backend */
    uint32_t            vid;             /* backend id */
    int                 notify_mask;     /* notification event mask */
    srs_voice_notify_t  notify;          /* notification callback */
    void               *notify_data;     /* opaque notification data */
    mrp_timer_t        *timer;           /* request timeout timer */
    mrp_list_hook_t     hook;            /* hook to list of requests */
} request_t;


typedef struct {
    request_t           req;             /* request */
    char               *msg;             /* message to render */
    char              **tags;            /* stream tags */
    uint32_t            actor;           /* actor id */
    int                 timeout;         /* timeout */
} queued_t;


/*
 * speech synthesizer state
 */

struct state_s {
    mrp_list_hook_t  synthesizers;       /* registered synthesizers */
    int              nsynthesizer;       /* number of synthesizers */
    mrp_list_hook_t  languages;          /* list of supported languages */
    uint32_t         nextid;             /* next voice id */
    mrp_list_hook_t  requests;           /* request queue */
    request_t       *active;             /* active request */
    request_t       *cancelling;         /* request being cancelled */
};


static request_t *find_request(state_t *state, uint32_t rid, uint32_t vid);
static request_t *activate_next(state_t *state);


static language_t *find_language(state_t *state, const char *lang, int create)
{
    mrp_list_hook_t *p, *n;
    language_t      *l;

    mrp_list_foreach(&state->languages, p, n) {
        l = mrp_list_entry(p, typeof(*l), hook);

        if (!strcasecmp(l->lang, lang))
            return l;
    }

    if (!create)
        return NULL;

    l = mrp_allocz(sizeof(*l));

    if (l == NULL)
        return NULL;

    mrp_list_init(&l->hook);
    mrp_list_init(&l->actors);

    l->lang = mrp_strdup(lang);

    if (l->lang == NULL) {
        mrp_free(l);
        return NULL;
    }

    mrp_list_append(&state->languages, &l->hook);

    return l;
}


static int register_actor(renderer_t *r, srs_voice_actor_t *act)
{
    state_t    *state = r->state;
    language_t *l;
    actor_t    *a;
    char        voice[256];
    const char *g;
    int        *n;

    l = find_language(state, act->lang, TRUE);

    if (l == NULL)
        return -1;

    a = mrp_allocz(sizeof(*a));

    if (a == NULL)
        return -1;


    mrp_list_init(&a->hook);
    a->r           = r;
    a->id          = act->id;
    a->dialect     = mrp_strdup(act->dialect);
    a->gender      = act->gender;
    a->age         = act->age;
    a->description = mrp_strdup(act->description);

    if ((act->dialect && !a->dialect) ||
        (act->description && !a->description)) {
        mrp_free(a->dialect);
        mrp_free(a->description);
        mrp_free(a);

        return -1;
    }

    if (a->gender == SRS_VOICE_GENDER_MALE) {
    male:
        g = "-male";
        n = &l->nmale;
    }
    else if (a->gender == SRS_VOICE_GENDER_FEMALE) {
        g = "-female";
        n = &l->nfemale;
    }
    else {
        a->gender = SRS_VOICE_GENDER_MALE;
        goto male;
    }

    if (*n > 0)
        snprintf(voice, sizeof(voice), "%s%s%s%s-%d", l->lang,
                 a->dialect ? "-" : "", a->dialect ? a->dialect : "", g, *n);
    else
        snprintf(voice, sizeof(voice), "%s%s%s%s", l->lang,
                 a->dialect ? "-" : "", a->dialect ? a->dialect : "", g);

    a->voice = mrp_strdup(voice);

    if (a->voice == NULL) {
        mrp_free(a->dialect);
        mrp_free(a->description);
        mrp_free(a);

        return -1;
    }

    mrp_list_append(&l->actors, &a->hook);
    *n += 1;

    mrp_log_info("Registered voice %s/%s.", r->name, voice);

    return 0;
}


static void unregister_actors(renderer_t *r)
{
    state_t         *state = r->state;
    mrp_list_hook_t *lp, *ln;
    language_t      *l;
    mrp_list_hook_t *ap, *an;
    actor_t         *a;

    mrp_list_foreach(&state->languages, lp, ln) {
        l = mrp_list_entry(lp, typeof(*l), hook);

        mrp_list_foreach(&l->actors, ap, an) {
            a = mrp_list_entry(ap, typeof(*a), hook);

            if (a->r == r) {
                if (a->gender == SRS_VOICE_GENDER_MALE)
                    l->nmale--;
                else
                    l->nfemale--;

                mrp_log_info("Unregistering voice %s/%s.", r->name, a->voice);

                mrp_list_delete(&l->hook);

                mrp_free(a->voice);
                mrp_free(a->dialect);
                mrp_free(a->description);
                mrp_free(a);
            }
        }

        if (mrp_list_empty(&l->actors)) {
            mrp_list_delete(&l->hook);
            mrp_free(l);
        }
    }
}


static void free_renderer(renderer_t *r)
{
    if (r != NULL) {
        mrp_list_delete(&r->hook);
        unregister_actors(r);
        mrp_free(r->name);
    }
}


static void notify_request(request_t *req, srs_voice_event_t *event)
{
    renderer_t        *r     = req->r;
    int                mask  = (1 << event->type);
    srs_voice_event_t  e;

    if (req->notify != NULL && (mask & req->notify_mask)) {
        e    = *event;
        e.id = req->id;
        req->notify(&e, req->notify_data);
    }
}


static void voice_notify_cb(srs_voice_event_t *event, void *notify_data)
{
    renderer_t        *r     = (renderer_t *)notify_data;
    state_t           *state = (state_t *)r->state;
    uint32_t           vid   = event->id;
    request_t         *req   = find_request(state, -1, vid);
    int                mask  = (1 << event->type);

    if (req == NULL) {
        mrp_log_error("Failed to find request for event 0x%x of <%d>.",
                      event->type, event->id);
        return;
    }

    if (event->type == SRS_VOICE_EVENT_STARTED) {
        mrp_del_timer(req->timer);
        req->timer = NULL;
    }

    notify_request(req, event);

    if (mask & SRS_VOICE_MASK_DONE) {
        mrp_del_timer(req->timer);
        req->timer = NULL;
        if (state->cancelling != req) {
            mrp_list_delete(&req->hook);
            mrp_free(req);
        }

        if (state->active == req) {
            state->active = NULL;
            activate_next(state);
        }
    }
}


int srs_register_voice(srs_context_t *srs, const char *name,
                       srs_voice_api_t *api, void *api_data,
                       srs_voice_actor_t *actors, int nactor,
                       srs_voice_notify_t *notify, void **notify_data)
{
    state_t           *state = (state_t *)srs->synthesizer;
    renderer_t        *r;
    srs_voice_actor_t *a;
    int                i;

    if (state == NULL) {
        srs->synthesizer = state = mrp_allocz(sizeof(*state));

        if (state == NULL)
            return -1;

        mrp_list_init(&state->synthesizers);
        mrp_list_init(&state->languages);
        mrp_list_init(&state->requests);
        state->nextid = 1;
    }

    if (api == NULL || name == NULL || actors == NULL || nactor < 1) {
        errno = EINVAL;
        return -1;
    }

    r = mrp_allocz(sizeof(*r));

    if (r == NULL)
        return -1;

    mrp_list_init(&r->hook);
    r->id    = state->nsynthesizer++;
    r->srs   = srs;
    r->state = state;
    r->name  = mrp_strdup(name);

    if (r->name == NULL) {
        free_renderer(r);
        return -1;
    }

    r->api      = *api;
    r->api_data = api_data;

    for (i = 0; i < nactor; i++) {
        if (register_actor(r, actors + i) != 0)
            free_renderer(r);
    }

    mrp_log_info("Registered voice/TTS backend '%s'.", r->name);

    mrp_list_append(&state->synthesizers, &r->hook);

    *notify      = voice_notify_cb;
    *notify_data = r;

    return 0;
}


void srs_unregister_voice(srs_context_t *srs, const char *name)
{
    state_t         *state = (state_t *)srs->synthesizer;
    renderer_t      *r;
    mrp_list_hook_t *p, *n;

    if (state != NULL) {
        mrp_list_foreach(&state->synthesizers, p, n) {
            r = mrp_list_entry(p, typeof(*r), hook);

            if (!strcmp(r->name, name)) {
                mrp_log_info("Unregistering voice/TTS backend '%s'.", name);
                free_renderer(r);
                return;
            }
        }
    }
}


static renderer_t *find_renderer(state_t *state, const char *voice,
                                 uint32_t *actor)
{
    language_t      *l;
    actor_t         *a, *fallback;
    mrp_list_hook_t *ap, *an;
    char             lang[128], *e;
    int              n;

    if (state == NULL) {
        errno = ENOSYS;

        return NULL;
    }

    if ((e = strchr(voice, '-')) == NULL)
        l = find_language(state, voice, FALSE);
    else {
        n = e - voice;
        if (snprintf(lang, sizeof(lang), "%*.*s", n, n, voice) >= sizeof(lang))
            l = NULL;
        else
            l = find_language(state, lang, FALSE);
    }

    if (l == NULL)
        return NULL;

    fallback = NULL;
    mrp_list_foreach(&l->actors, ap, an) {
        a = mrp_list_entry(ap, typeof(*a), hook);

        if (!strcmp(a->voice, voice)) {
            *actor = a->id;

            return a->r;
        }

        if (fallback == NULL)
            fallback = a;
    }

    if (fallback != NULL) {
        *actor = fallback->id;

        return fallback->r;
    }

    return NULL;
}


#if 0
static renderer_t *select_renderer(state_t *state, const char *voice,
                                   uint32_t *actid)
{
    renderer_t         *r;
    language_t         *l;
    srs_voice_actor_t  *a;
    srs_voice_gender_t  gender;
    mrp_list_hook_t    *p, *n;
    renderer_t         *dfltv;
    uint32_t            dfltid;
    int                 i;

    if (state == NULL) {
        errno = ENOSYS;
        goto notfound;
    }

    if (!strcmp(actor, SRS_VOICE_FEMALE)) {
        gender = SRS_VOICE_GENDER_FEMALE;
        actor  = NULL;
    }
    else if (!strcmp(actor, SRS_VOICE_MALE)) {
        gender = SRS_VOICE_GENDER_MALE;
        actor  = NULL;
    }
    else
        gender = SRS_VOICE_GENDER_ANY;

    dfltv  = NULL;
    dfltid = SRS_VOICE_INVALID;

    mrp_list_foreach(&state->synthesizers, p, n) {
        r = mrp_list_entry(p, typeof(*r), hook);

        for (i = 0, a = r->actors; i < r->nactor; i++, a++) {
            if (strcmp(a->lang, lang))
                continue;

            if (actor == NULL) {
                if (gender == a->gender) {
                    *actid = a->id;
                    return r;
                }
            }
            else {
                if (!strcmp(a->name, actor)) {
                    *actid = a->id;
                    return r;
                }

                if (dfltid == SRS_VOICE_INVALID) {
                    dfltv  = r;
                    dfltid = a->id;
                }
            }
        }
    }

 notfound:
    *actid = dfltid;
    return dfltv;
}
#endif

static void free_tags(char **tags)
{
    int i;

    if (tags == NULL)
        return;

    for (i = 0; tags[i] != NULL; i++)
        mrp_free(tags[i]);

    mrp_free(tags);
}


static char **copy_tags(char **tags)
{
    char **cp = NULL;
    int    i;

    if (tags == NULL)
        return NULL;

    for (i = 0; tags[i] != NULL; i++) {
        if (!mrp_reallocz(cp, i, i + 1))
            goto fail;
        if ((cp[i] = mrp_strdup(tags[i])) == NULL)
            goto fail;
    }

    if (mrp_reallocz(cp, i, i + 1))
        return cp;
    /* fall through */
 fail:
    free_tags(cp);
    return NULL;
}


static void request_timer_cb(mrp_timer_t *t, void *user_data)
{
    queued_t          *qr  = (queued_t *)user_data;
    request_t         *req = &qr->req;
    srs_voice_event_t  event;

    mrp_log_info("Voice/TTS request #%u timed out.", qr->req.id);

    mrp_del_timer(req->timer);
    req->timer = NULL;

    mrp_clear(&event);
    event.type = SRS_VOICE_EVENT_TIMEOUT;
    event.id   = req->id;

    notify_request(req, &event);

    mrp_list_delete(&req->hook);

    mrp_free(qr->msg);
    free_tags(qr->tags);

    mrp_free(qr);
}


static request_t *enqueue_request(state_t *state, const char *msg, char **tags,
                                  renderer_t *r, uint32_t actor, int timeout,
                                  int notify_mask, srs_voice_notify_t notify,
                                  void *notify_data)
{
    queued_t *qr;

    qr = mrp_allocz(sizeof(*qr));

    if (qr == NULL)
        return NULL;

    mrp_list_init(&qr->req.hook);

    qr->req.id          = state->nextid++;
    qr->req.r           = r;
    qr->req.vid         = SRS_VOICE_INVALID;
    qr->req.notify_mask = notify_mask;
    qr->req.notify      = notify;
    qr->req.notify_data = notify_data;

    qr->msg     = mrp_strdup(msg);
    qr->tags    = copy_tags(tags);
    qr->actor   = actor;
    qr->timeout = timeout;

    if (qr->msg != NULL && (qr->tags != NULL || tags == NULL)) {
        mrp_list_append(&state->requests, &qr->req.hook);

        if (timeout > 0)
            qr->req.timer = mrp_add_timer(r->srs->ml, timeout,
                                          request_timer_cb, qr);

        return &qr->req;
    }
    else {
        mrp_free(qr->msg);
        free_tags(qr->tags);
        mrp_free(qr);

        return NULL;
    }
}


static request_t *activate_next(state_t *state)
{
    queued_t        *qr;
    renderer_t      *r;
    mrp_list_hook_t *p, *n;

    if (state->active != NULL)
        return NULL;

    qr = NULL;
    mrp_list_foreach(&state->requests, p, n) {
        mrp_list_delete(p);
        qr = mrp_list_entry(p, typeof(*qr), req.hook);
        break;
    }

    if (qr == NULL)
        return NULL;

    mrp_del_timer(qr->req.timer);
    qr->req.timer = NULL;

    r = qr->req.r;
    qr->req.vid = r->api.render(qr->msg, qr->tags, qr->actor,
                                qr->req.notify_mask, r->api_data);

    mrp_free(qr->msg);
    qr->msg = NULL;
    free_tags(qr->tags);
    qr->tags = NULL;

    if (qr->req.vid == SRS_VOICE_INVALID) {
        if (qr->req.notify != NULL &&
            (qr->req.notify_mask & 1 << SRS_VOICE_EVENT_ABORTED)) {
            srs_voice_event_t e;

            mrp_clear(&e);
            e.type = SRS_VOICE_EVENT_ABORTED;
            e.id   = qr->req.id;

            voice_notify_cb(&e, qr->req.notify_data);

            free(qr);
        }

        return NULL;
    }
    else {
        state->active = &qr->req;

        return &qr->req;
    }
}


request_t *render_request(state_t *state, const char *msg, char **tags,
                          renderer_t *r, uint32_t actor, int timeout,
                          int notify_mask, srs_voice_notify_t notify,
                          void *notify_data)
{
    request_t *req;

    req = mrp_allocz(sizeof(*req));

    if (req == NULL)
        return NULL;

    mrp_list_init(&req->hook);
    req->id  = state->nextid++;
    req->r   = r;
    req->vid = r->api.render(msg, tags, actor, notify_mask, r->api_data);

    if (req->vid == SRS_VOICE_INVALID) {
        mrp_free(req);
        return NULL;
    }

    req->notify      = notify;
    req->notify_mask = notify_mask;
    req->notify_data = notify_data;

    state->active = req;

    return req;
}


uint32_t srs_render_voice(srs_context_t *srs, const char *msg,
                          char **tags, const char *voice, int timeout,
                          int notify_mask, srs_voice_notify_t notify,
                          void *user_data)
{
    state_t    *state = (state_t *)srs->synthesizer;
    renderer_t *r;
    request_t  *req;
    uint32_t    actid;

    if (state == NULL) {
        errno = ENOSYS;

        return SRS_VOICE_INVALID;
    }

    r = find_renderer(state, voice, &actid);

    if (r == NULL) {
        errno = EINVAL;

        return SRS_VOICE_INVALID;
    }

    if (state->active == NULL)
        req = render_request(state, msg, tags, r, actid, timeout,
                             notify_mask, notify, user_data);
    else {
        if (timeout == SRS_VOICE_IMMEDIATE) {
            errno = EBUSY;
            req   = NULL;
        }
        else
            req = enqueue_request(state, msg, tags, r, actid, timeout,
                                  notify_mask, notify, user_data);
    }

    if (req != NULL)
        return req->id;
    else
        return SRS_VOICE_INVALID;
}


static request_t *find_request(state_t *state, uint32_t rid, uint32_t vid)
{
    mrp_list_hook_t *p, *n;
    request_t       *req;

    if (state == NULL) {
        errno = ENOSYS;
        goto error;
    }

    if ((req = state->active) != NULL) {
        if ((rid == -1 || req->id == rid) && (vid == -1 || req->vid == vid))
            return req;
    }

    mrp_list_foreach(&state->requests, p, n) {
        req = mrp_list_entry(p, typeof(*req), hook);

        if ((rid == -1 || req->id == rid) && (vid == -1 || req->vid == vid))
            return req;
    }

    errno = EINVAL;

 error:
    return NULL;
}


void srs_cancel_voice(srs_context_t *srs, uint32_t rid, int notify)
{
    state_t    *state = (state_t *)srs->synthesizer;
    request_t  *req   = find_request(state, rid, -1);
    renderer_t *voice = req ? req->r : NULL;

    if (req == NULL)
        return;

    mrp_del_timer(req->timer);
    req->timer = NULL;
    state->cancelling = req;

    voice->api.cancel(req->vid, voice->api_data);

    mrp_list_delete(&req->hook);
    mrp_free(req);
}


int srs_query_voices(srs_context_t *srs, const char *language,
                     srs_voice_actor_t **actorsp)
{
    state_t            *state = (state_t *)srs->synthesizer;
    srs_voice_actor_t  *actors, *actor;
    int                 nactor, i;
    language_t         *l;
    mrp_list_hook_t    *lp, *ln;
    actor_t            *a;
    mrp_list_hook_t    *ap, *an;

    if (state == NULL) {
        *actorsp = NULL;

        return 0;
    }

    actors = NULL;
    nactor = 0;

    mrp_list_foreach(&state->languages, lp, ln) {
        l = mrp_list_entry(lp, typeof(*l), hook);

        if (language != NULL && strcasecmp(l->lang, language))
            continue;

        mrp_list_foreach(&l->actors, ap, an) {
            a = mrp_list_entry(ap, typeof(*a), hook);

            if (mrp_reallocz(actors, nactor, nactor + 1) == NULL)
                goto fail;

            actor = actors + nactor++;

            actor->name        = mrp_strdup(a->voice);
            actor->lang        = mrp_strdup(l->lang);
            actor->dialect     = mrp_strdup(a->dialect);
            actor->gender      = a->gender;
            actor->age         = a->age;
            actor->description = mrp_strdup(a->description);

            if (!actor->name || !actor->lang ||
                (!actor->dialect && a->dialect) ||
                (!actor->description && a->description))
                goto fail;
        }
    }

    if (actors != NULL) {
        if (!mrp_reallocz(actors, nactor, nactor + 1))
            goto fail;
    }

    *actorsp = actors;

    return nactor;

 fail:
    for (i = 0; i < nactor; i++) {
        mrp_free(actors[i].name);
        mrp_free(actors[i].lang);
        mrp_free(actors[i].dialect);
        mrp_free(actors[i].description);
    }

    return -1;
}


void srs_free_queried_voices(srs_voice_actor_t *actors)
{
    srs_voice_actor_t *a;

    if (actors != NULL) {
        for (a = actors; a->lang; a++) {
            mrp_free(a->name);
            mrp_free(a->lang);
            mrp_free(a->dialect);
            mrp_free(a->description);
        }
        mrp_free(actors);
    }
}
