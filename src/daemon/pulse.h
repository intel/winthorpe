/*
 * Copyright (c) 2012-2014, Intel Corporation
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

#ifndef __SRS_DAEMON_PULSE_H__
#define __SRS_DAEMON_PULSE_H__

#include <stdint.h>
#include <murphy/common/macros.h>
#include <pulse/pulseaudio.h>

#include "srs/daemon/voice.h"

MRP_CDECL_BEGIN

/*
 * PA stream events
 */

typedef enum {
    SRS_STREAM_EVENT_NONE      = SRS_VOICE_EVENT_STARTED - 1,
    SRS_STREAM_EVENT_STARTED   = SRS_VOICE_EVENT_STARTED,
    SRS_STREAM_EVENT_PROGRESS  = SRS_VOICE_EVENT_PROGRESS,
    SRS_STREAM_EVENT_COMPLETED = SRS_VOICE_EVENT_COMPLETED,
    SRS_STREAM_EVENT_TIMEOUT   = SRS_VOICE_EVENT_TIMEOUT,
    SRS_STREAM_EVENT_ABORTED   = SRS_VOICE_EVENT_ABORTED,
    SRS_STREAM_EVENT_CORKED,
    SRS_STREAM_EVENT_UNCORKED,
} srs_stream_event_type_t;

#define SRS_STREAM_MASK_NONE      SRS_VOICE_MASK_NONE
#define SRS_STREAM_MASK_STARTED   SRS_VOICE_MASK_STARTED
#define SRS_STREAM_MASK_PROGRESS  SRS_VOICE_MASK_PROGRESS
#define SRS_STREAM_MASK_COMPLETED SRS_VOICE_MASK_COMPLETED
#define SRS_STREAM_MASK_ABORTED   SRS_VOICE_MASK_ABORTED
#define SRS_STREAM_MASK_CORKED    (1 << SRS_STREAM_EVENT_CORKED)
#define SRS_STREAM_MASK_UNCORKED  (1 << SRS_STREAM_EVENT_UNCORKED)
#define SRS_STREAM_MASK_ONESHOT   (~(SRS_STREAM_EVENT_PROGRESS))
#define SRS_STREAM_MASK_ALL       (SRS_STREAM_MASK_STARTED   | \
                                   SRS_STREAM_MASK_PROGRESS  | \
                                   SRS_STREAM_MASK_COMPLETED | \
                                   SRS_STREAM_MASK_ABORTED)

typedef srs_voice_event_t srs_stream_event_t;

typedef void (*srs_stream_cb_t)(srs_pulse_t *p, srs_stream_event_t *event,
                                void *user_data);

/** Set up the PulseAudio interface. */
srs_pulse_t *srs_pulse_setup(pa_mainloop_api *pa, const char *name);

/** Clean up the audio interface. */
void srs_pulse_cleanup(srs_pulse_t *p);

/** Render an stream (a buffer of audio samples). */
uint32_t srs_play_stream(srs_pulse_t *p, void *sample_buf, int sample_rate,
                         int nchannel, uint32_t nsample, char **tags,
                         int event_mask, srs_stream_cb_t cb,
                         void *user_data);

/** Stop an ongoing stream. */
int srs_stop_stream(srs_pulse_t *p, uint32_t id, int drain, int notify);

MRP_CDECL_END

#endif /* __SRS_DAEMON_PULSE_H__ */
