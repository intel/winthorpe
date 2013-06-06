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

#include "src/daemon/client.h"

/*
 * speech recognition backend interface
 */

/** Type for tokens recognized by a speech recognition backend. */
typedef struct srs_srec_utterance_s srs_srec_utterance_t;

/** Type for a backend recognition notification callback. */
typedef int (*srs_srec_notify_t)(srs_srec_utterance_t *utt, void *notify_data);

/** Notification callback return value for flushing the full audio buffer. */
#define SRS_SREC_FLUSH_ALL -1

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
    void *(*sampledup)(uint32_t start, uint32_t end, size_t *size,
                       void *user_data);
    /** Check if the given language model exists/is usable. */
    int (*check_decoder)(const char *decoder, void *user_data);
    /** Set language model to be used. */
    int (*select_decoder)(const char *decoder, void *user_data);
    /** Get the used language model. */
    const char *(*active_decoder)(void *user_data);
} srs_srec_api_t;

/*
 * a single speech token
 */
typedef struct {
    const char *token;                     /* recognized tokens */
    double      score;                     /* correctness probability */
    uint32_t    start;                     /* start in audio buffer */
    uint32_t    end;                       /* end in audio buffer */
} srs_srec_token_t;

/*
 * a single candidate (essentially a set of speech tokens)
 */
typedef struct {
    double            score;             /* overall candidate quality score */
    size_t            ntoken;            /* number of tokens in candidate */
    srs_srec_token_t *tokens;            /* actual tokens of this candidate */
} srs_srec_candidate_t;

/*
 * an utterance (candidates for a silence-terminated audio sequence)
 */
struct srs_srec_utterance_s {
    const char            *id;           /* backend ID for this utterance */
    double                 score;        /* overall quality score */
    uint32_t               length;       /* length in the audio buffer */
    size_t                 ncand;        /* number of candidates */
    srs_srec_candidate_t **cands;        /* actual candidates */
};

/** Register a speech recognition backend. */
int srs_register_srec(srs_context_t *srs, const char *name,
                      srs_srec_api_t *api, void *api_data,
                      srs_srec_notify_t *notify, void **notify_data);

/** Unregister a speech recognition backend. */
void srs_unregister_srec(srs_context_t *srs, const char *name);

/** Macro to refer to the default recognizer backend. */
#define SRS_DEFAULT_RECOGNIZER NULL

/** Activate speech recognition using the specified backend. */
int srs_activate_srec(srs_context_t *srs, const char *name);

/** Deactivate the specified speech recognition backend. */
void srs_deactivate_srec(srs_context_t *srs, const char *name);

/** Check if a decoder (model/dictionary combination) exists for a backend. */
int srs_check_decoder(srs_context_t *srs, const char *name,
                      const char *decoder);

/** Select a decoder for a backend. */
int srs_set_decoder(srs_context_t *srs, const char *name, const char *decoder);


/*
 * speech recognition disambiguator interface
 */

/** Type for disambiguated speech recognition results. */
typedef struct srs_srec_result_s srs_srec_result_t;

/*
 * disambiguation result
 */

typedef enum {
    SRS_DISAMB_UNKNOWN = 0,
    SRS_DISAMB_MATCH,                    /* full match */
    SRS_DISAMB_RESCAN,                   /* rescan (after dictionary switch) */
    SRS_DISAMB_AMBIGUOUS,                /* failed to (fully) disambiguate */
} srs_disamb_type_t;

typedef enum {
    SRS_SREC_RESULT_UNKNOWN = 0,         /* unknown result */
    SRS_SREC_RESULT_MATCH,               /* full command match */
    SRS_SREC_RESULT_DICT,                /* dictionary switch required */
    SRS_SREC_RESULT_AMBIGUOUS,           /* further disambiguation needed */
} srs_srec_result_type_t;

typedef struct {
    mrp_list_hook_t   hook;              /* to more commands */
    srs_client_t     *client;            /* actual client */
    int               index;             /* client command index */
    double            score;             /* backend score */
    int               fuzz;              /* disambiguation fuzz */
    char            **tokens;            /* command tokens */
} srs_srec_match_t;

struct srs_srec_result_s {
    srs_srec_result_type_t   type;       /* result type */
    mrp_list_hook_t          hook;       /* to list of results */
    void                    *samplebuf;  /* utterance audio */
    size_t                   samplelen;  /* audio buffer length */
    uint32_t                 sampleoffs; /* extra audio offset */
    char                   **tokens;     /* matched tokens */
    uint32_t                *start;      /* token start offset */
    uint32_t                *end;        /* token end offsets */
    int                      ntoken;     /* number of tokens */
    char                   **dicts;      /* dictionary stack */
    int                      ndict;      /* stack depth */

    union {                              /* type specific data */
        mrp_list_hook_t    matches;      /* full match(es) */
        struct {
            srs_dict_op_t  op;           /* push/pop/switch */
            char          *dict;         /* dictionary for switch/push */
            int            rescan;       /* rescan starting at this token */
            void          *state;        /* disambiguator continuation */
        } dict;
    } result;
};


/*
 * API to a disambiguator implementation.
 */

typedef struct {
    /** Register the commands of a client. */
    int (*add_client)(srs_client_t *client, void *api_data);
    /** Unregister the commands of a client. */
    void (*del_client)(srs_client_t *client, void *api_data);
    /** Disambiguate an utterance with candidates. */
    int (*disambiguate)(srs_srec_utterance_t *utt, srs_srec_result_t **result,
                        void *api_data);
} srs_disamb_api_t;


/** Register a disambiguator implementation. */
int srs_register_disambiguator(srs_context_t *srs, const char *name,
                               srs_disamb_api_t *api, void *api_data);

/** Unregister a disambiguator implementation. */
void srs_unregister_disambiguator(srs_context_t *srs, const char *name);

/** Register a client for speech recognition. */
int srs_srec_add_client(srs_context_t *srs, srs_client_t *client);

/** Unregister a client from speech recognition. */
void srs_srec_del_client(srs_context_t *srs, srs_client_t *client);


/** Macro to refer to the default disambiguator. */
#define SRS_DEFAULT_DISAMBIGUATOR NULL

#endif /* __SRS_DAEMON_RECOGNIZER_H__ */
