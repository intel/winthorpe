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

#ifndef __SRS_DAEMON_AUDIOBUF_H__
#define __SRS_DAEMON_AUDIOBUF_H__

#include <murphy/common/refcnt.h>
#include <pulse/sample.h>

/*
 * audio formats
 */

typedef enum {
#define MAP(type) SRS_AUDIO_##type = PA_SAMPLE_##type
    MAP(INVALID),
    MAP(U8),
    MAP(ALAW),
    MAP(ULAW),
    MAP(S16LE),
    MAP(S16BE),
    MAP(FLOAT32LE),
    MAP(FLOAT32BE),
    MAP(S32LE),
    MAP(S32BE),
    MAP(S24LE),
    MAP(S24BE),
    MAP(S24_32LE),
    MAP(S24_32BE),
    MAP(MAX),
#undef MAP
} srs_audioformat_t;


/*
 * a reference-counted audio buffer
 */

typedef struct {
    mrp_refcnt_t       refcnt;           /* reference count */
    srs_audioformat_t  format;           /* audio format */
    uint32_t           rate;             /* sample rate */
    uint8_t            channels;         /* number of channels */
    size_t             samples;          /* amount of sample data */
    void              *data;             /* actual sample data */
} srs_audiobuf_t;

/** Create a new audio buffer. */
srs_audiobuf_t *srs_create_audiobuf(srs_audioformat_t format, uint32_t rate,
                                    uint8_t channels, size_t samples,
                                    void *data);

/** Add a reference to the given audio buffer. */
srs_audiobuf_t *srs_ref_audiobuf(srs_audiobuf_t *buf);

/** Remove a reference from the given audio buffer, potentially freeing it. */
void srs_unref_audiobuf(srs_audiobuf_t *buf);

#endif /* __SRS_DAEMON_AUDIOBUF_H__ */
