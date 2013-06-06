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

#ifndef __SRS_DAEMON_RECOGNIZER_H__
#define __SRS_DAEMON_RECOGNIZER_H__

/** Type for tokens recognized by a speech recognition backend. */
typedef struct srs_srec_token_s srs_srec_token_t;

/** Type for a backend recognition notification callback. */
typedef void (*srs_srec_notify_t)(srs_srec_token_t *tokens, int ntoken,
                                  void *notify_data);

/*
 * API to a speech recognition backend.
 */
typedef struct {
    /** Activate speech recognition. */
    int (*activate)(void *user_data);
    /** Deactivate speech recognition. */
    void (*deactivate)(void *user_data);
    /** Flush part or whole of the audio buffer. */
    int (*flush)(uint32_t start, uint32_t end, void *user_data);
    /** Schedule a rescan of the given portion of the audio buffer. */
    int (*rescan)(uint32_t start, uint32_t end, void *user_data);
    /** Get a copy of the audio samples in the buffer. */
    void *(*sampledup)(uint32_t start, uint32_t end, void *user_data);
    /** Check if the given language model exists/is usable. */
    int (*check_model)(const char *model, void *user_data);
    /** Check if the given dictionary exists/is usable. */
    int (*check_dictionary)(const char *dictionary, void *user_data);
    /** Set language model to be used. */
    int (*set_model)(const char *model, void *user_data);
    /** Set dictionary to be used. */
    int (*set_dictionary)(const char *dictionary, void *user_data);
} srs_srec_api_t;

/*
 * a single speech token
 */
struct srs_srec_token_s {
    char     *token;                     /* recognized tokens */
    double    score;                     /* correctness probability */
    uint32_t  start;                     /* start in audio buffer */
    uint32_t  end;                       /* end in audio buffer */
    int       flush : 1;                 /* flush from audio buffer */
};


/** Register a speech recognition backend. */
int srs_register_srec(srs_context_t *srs, const char *name,
                      srs_srec_api_t *api, void *api_data,
                      srs_srec_notify_t *notify, void **notify_data);

/** Unregister a speech recognition backend. */
void srs_unregister_srec(srs_context_t *srs, const char *name);

/** Activate speech recognition using the specified backend. */
int srs_activate_srec(srs_context_t *srs, const char *name);

/** Deactivate the specified speech recognition backend. */
void srs_deactivate_srec(srs_context_t *srs, const char *name);

/** Check if a given model exists for a recognition backend. */
int srs_check_model(srs_context_t *srs, const char *name, const char *model);

/** Check if a given dictionary exists for a recognition backend. */
int srs_check_dictionary(srs_context_t *srs, const char *name,
                          const char *dictionary);

/** Check if a given model exists for a recognition backend. */
int srs_set_model(srs_context_t *srs, const char *name, const char *model);

/** Check if a given dictionary exists for a recognition backend. */
int srs_set_dictionary(srs_context_t *srs, const char *name,
                       const char *dictionary);

#endif /* __SRS_DAEMON_RECOGNIZER_H__ */
