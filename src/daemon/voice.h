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

#include "srs/daemon/context.h"
#include "srs/daemon/voice-api-types.h"

/*
 * speech synthesizer backend interface
 */

/** Voice rendering notification callback type. */
typedef void (*srs_voice_notify_t)(srs_voice_event_t *event, void *notify_data);

/*
 * API to voice backend
 */
typedef struct {
    /** Render the given message. */
    uint32_t (*render)(const char *msg, char **tags, int actor, double rate,
                       double pitch, int notify_events, void *api_data);
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
                          char **tags, const char *voice, double rate,
                          double pitch, int timeout, int notify_events,
                          srs_voice_notify_t notify, void *user_data);

/** Cancel the given voice rendering. */
void srs_cancel_voice(srs_context_t *srs, uint32_t id, int notify);

/** Query languages. */
int srs_query_voices(srs_context_t *srs, const char *language,
                     srs_voice_actor_t **actors);

/** Free voice query results. */
void srs_free_queried_voices(srs_voice_actor_t *actors);

#endif /* __SRS_DAEMON_VOICE_H__ */
