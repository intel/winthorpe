/*
 * Copyright (c) 2012, Intel Corporation
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

/** Identifier indicating failure/invalid voice. */
#define SRS_VOICE_INVALID ((uint32_t)-1)

/** Voice completion notification callback type. */
typedef void (*srs_voice_notify_t)(uint32_t id, void *user_data);

/*
 * API to voice backend
 */
typedef struct {
    /** Load a sound from a file, attempt caching according to the hint. */
    uint32_t (*load)(const char *path, int cache, void *api_data);
    /** Play a sound, call the given notifier when done. */
    uint32_t (*play)(uint32_t id, srs_voice_notify_t notify, void *user_data,
                     void *api_data);
    /** Play a sound from a file, call the given notifier when done. */
    uint32_t (*play_file)(const char *file, srs_voice_notify_t notify,
                          void *user_data, void *api_data);
    /** Synthesize the given message, call the given notifier when done. */
    uint32_t (*say)(const char *msg, srs_voice_notify_t notify, void *user_data,
                    void *api_data);
    /** Cancel a playing sound or message, notify completion if asked. */
    void (*cancel)(uint32_t id, int notify, void *api_data);
} srs_voice_api_t;


/** Register a voice engine. */
int srs_register_voice(srs_context_t *srs, const char *name,
                       srs_voice_api_t *api, void *api_data);

/** Unregister the given voice engine. */
void srs_unregister_voice(srs_context_t *srs, const char *name);

/** Load the given sound file, returning its identifier. */
uint32_t srs_load_sound(srs_context_t *srs, const char *path, int cache);

/** Play the given preloaded sound. */
uint32_t srs_play_sound(srs_context_t *srs, uint32_t id,
                        srs_voice_notify_t notify, void *user_data);

/** Play the given file. */
uint32_t srs_play_sound_file(srs_context_t *srs, const char *path,
                             srs_voice_notify_t notify, void *user_data);

/** Syntehsize the given message. */
uint32_t srs_say_msg(srs_context_t *srs, const char *msg,
                     srs_voice_notify_t notify, void *user_data);

/** Cancel a playing sound or message. */
void srs_cancel_voice(srs_context_t *srs, uint32_t id, int notify);

#endif /* __SRS_DAEMON_VOICE_H__ */
