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

#include <unistd.h>
#include <errno.h>

#include <murphy/common/macros.h>
#include <murphy/common/mm.h>

#include "src/daemon/context.h"
#include "src/daemon/voice.h"

/*
 * voice engine instance
 */

typedef struct {
    srs_context_t   *srs;                /* main context */
    char            *name;               /* engine name */
    srs_voice_api_t  api;                /* voice engine API */
    void            *api_data;           /* opaque engine data */
} srs_voice_t;


int srs_register_voice(srs_context_t *srs, const char *name,
                       srs_voice_api_t *api, void *api_data)
{
    srs_voice_t *voice;

    if (srs->voice != NULL) {
        errno = EEXIST;
        return -1;
    }

    if (api == NULL || name == NULL) {
        errno = EINVAL;
        return -1;
    }

    voice = mrp_allocz(sizeof(*voice));

    if (voice == NULL)
        return -1;

    voice->srs      = srs;
    voice->name     = mrp_strdup(name);
    voice->api      = *api;
    voice->api_data = api_data;

    if (voice->name == NULL) {
        mrp_free(voice);
        return -1;
    }

    srs->voice = voice;

    return 0;
}


void srs_unregister_voice(srs_context_t *srs, const char *name)
{
    srs_voice_t *voice;

    if ((voice = srs->voice) != NULL) {
        if (!strcmp(voice->name, name)) {
            mrp_free(voice->name);
            mrp_free(voice);

            srs->voice = NULL;
        }
    }
}


uint32_t srs_load_sound(srs_context_t *srs, const char *path, int cache)
{
    srs_voice_t *voice = srs->voice;
    uint32_t     id;

    if (voice != NULL)
        id = voice->api.load(path, cache, voice->api_data);
    else
        id = SRS_VOICE_INVALID;

    return id;
}


uint32_t srs_play_sound(srs_context_t *srs, uint32_t id,
                        srs_voice_notify_t notify, void *user_data)
{
    srs_voice_t *voice = srs->voice;

    if (voice != NULL)
        id = voice->api.play(id, notify, user_data, voice->api_data);
    else
        id = SRS_VOICE_INVALID;

    return id;
}


uint32_t srs_play_sound_file(srs_context_t *srs, const char *path,
                             srs_voice_notify_t notify, void *user_data)
{
    srs_voice_t *voice = srs->voice;
    uint32_t     id;

    if (voice != NULL)
        id = voice->api.play_file(path, notify, user_data, voice->api_data);
    else
        id = SRS_VOICE_INVALID;

    return id;
}


uint32_t srs_say_msg(srs_context_t *srs, const char *msg,
                     srs_voice_notify_t notify, void *user_data)
{
    srs_voice_t *voice = srs->voice;
    uint32_t     id;

    if (voice != NULL)
        id = voice->api.say(msg, notify, user_data, voice->api_data);
    else
        id = SRS_VOICE_INVALID;

    return id;
}


void srs_cancel_voice(srs_context_t *srs, uint32_t id, int notify)
{
    srs_voice_t *voice = srs->voice;

    if (voice != NULL)
        voice->api.cancel(id, notify, voice->api_data);
}
