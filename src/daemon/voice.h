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

#ifndef __SRS_DAEMON_VOICE_H__
#define __SRS_DAEMON_VOICE_H__

#include "src/daemon/context.h"

/*
 * speech synthesizer backend interface
 */

/** Failure/invalid voice identifier. */
#define SRS_VOICE_INVALID ((uint32_t)-1)

/** Voice rendering notification callback events. */
typedef enum {
    SRS_VOICE_EVENT_STARTED,             /* TTS started */
    SRS_VOICE_EVENT_PROGRESS,            /* TTS progressing */
    SRS_VOICE_EVENT_COMPLETED,           /* TTS finished successfully */
    SRS_VOICE_EVENT_TIMEOUT,             /* TTS timed out */
    SRS_VOICE_EVENT_ABORTED,             /* TTS finished abnormally */
    SRS_VOICE_EVENT_MAX
} srs_voice_event_type_t;

/** Voice rendering notification event masks. */
#define SRS_VOICE_MASK_NONE        0
#define SRS_VOICE_MASK_STARTED    (1 << SRS_VOICE_EVENT_STARTED)
#define SRS_VOICE_MASK_PROGRESS   (1 << SRS_VOICE_EVENT_PROGRESS)
#define SRS_VOICE_MASK_COMPLETED  (1 << SRS_VOICE_EVENT_COMPLETED)
#define SRS_VOICE_MASK_TIMEOUT    (1 << SRS_VOICE_EVENT_TIMEOUT)
#define SRS_VOICE_MASK_ABORTED    (1 << SRS_VOICE_EVENT_ABORTED)

#define SRS_VOICE_MASK_ALL       ((1 << SRS_VOICE_EVENT_MAX) - 1)
#define SRS_VOICE_MASK_DONE       (SRS_VOICE_MASK_COMPLETED | \
                                   SRS_VOICE_MASK_TIMEOUT   | \
                                   SRS_VOICE_MASK_ABORTED)

typedef struct {
    srs_voice_event_type_t type;         /* event type */
    uint32_t               id;           /* voice stream id */
    union {                              /* event-specific data */
        struct {
            double   pcnt;               /* progress in percentages */
            uint32_t msec;               /* progress in millisconds */
        } progress;
    } data;
} srs_voice_event_t;

/** Voice rendering notification callback type. */
typedef void (*srs_voice_notify_t)(srs_voice_event_t *event, void *notify_data);

/** Voice actor genders. */
typedef enum {
    SRS_VOICE_GENDER_ANY,                /* any voice actor */
    SRS_VOICE_GENDER_MALE,               /* a male voice actor */
    SRS_VOICE_GENDER_FEMALE,             /* a female voice actor */
} srs_voice_gender_t;

#define SRS_VOICE_FEMALE "female"        /* any female actor */
#define SRS_VOICE_MALE   "male"          /* any male actor */

/** Voice timeout/queuing constants. */
#define SRS_VOICE_IMMEDIATE     0        /* render immediately or fail */
#define SRS_VOICE_QUEUE        -1        /* allow queuing indefinitely */
#define SRS_VOICE_TIMEOUT(sec) (sec)     /* fail if can't start in time */


/*
 * voice actors
 */
typedef struct {
    uint32_t            id;              /* backend actor id */
    char               *lang;            /* spoken language */
    char               *dialect;         /* language dialect, if any */
    srs_voice_gender_t  gender;          /* gender */
    int                 age;             /* actor age */
    char               *name;            /* backend actor name */
    char               *description;     /* human-readable description */
} srs_voice_actor_t;


/*
 * API to voice backend
 */
typedef struct {
    /** Render the given message. */
    uint32_t (*render)(const char *msg, char **tags, int actor,
                       int notify_events, void *api_data);
    /** Cancel the given rendering, notify cancellation if asked for. */
    void (*cancel)(uint32_t id, void *api_data);
} srs_voice_api_t;

/** Register a voice synthesizer backend. */
int srs_register_voice(srs_context_t *srs, const char *name,
                       srs_voice_api_t *api, void *api_data,
                       srs_voice_actor_t *actors, int nactor,
                       srs_voice_notify_t *notify, void **notify_data);

/** Unregister the given voice synthesizer backend. */
void srs_unregister_voice(srs_context_t *srs, const char *name);


/** Render the given message using the given parameters. */
uint32_t srs_render_voice(srs_context_t *srs, const char *msg,
                          char **tags, const char *voice, int timeout,
                          int notify_events, srs_voice_notify_t notify,
                          void *user_data);

/** Cancel the given voice rendering. */
void srs_cancel_voice(srs_context_t *srs, uint32_t id, int notify);

/** Query languages. */
int srs_query_voices(srs_context_t *srs, const char *language,
                     srs_voice_actor_t **actors);

/** Free voice query results. */
void srs_free_queried_voices(srs_voice_actor_t *actors);

#endif /* __SRS_DAEMON_VOICE_H__ */
