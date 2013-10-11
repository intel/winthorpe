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

#ifndef __SRS_DAEMON_CLIENT_H__
#define __SRS_DAEMON_CLIENT_H__

typedef struct srs_client_s srs_client_t;

#include "src/daemon/context.h"
#include "src/daemon/resctl.h"
#include "src/daemon/audiobuf.h"
#include "srs/daemon/voice.h"


/*
 * client types
 */

typedef enum {
    SRS_CLIENT_TYPE_NONE = 0,
    SRS_CLIENT_TYPE_DBUS,                /* external D-BUS client */
    SRS_CLIENT_TYPE_BUILTIN,             /* builtin client */
} srs_client_type_t;


/*
 * voice focus types
 */

typedef enum {
    SRS_VOICE_FOCUS_NONE = 0,            /* focus released */
    SRS_VOICE_FOCUS_SHARED,              /* normal shared voice focus */
    SRS_VOICE_FOCUS_EXCLUSIVE,           /* exclusive voice focus */
} srs_voice_focus_t;


/*
 * client commands
 */

#define SRS_MAX_TOKENS 64

typedef struct {
    char **tokens;                       /* tokens of this command */
    int    ntoken;                       /* number of tokens */
} srs_command_t;


/*
 * special command tokens
 */

#define SRS_TOKEN_SWITCHDICT "__switch_dict__"
#define SRS_TOKEN_PUSHDICT   "__push_dict__"
#define SRS_TOKEN_POPDICT    "__pop_dict__"
#define SRS_TOKEN_WILDCARD   "*"


/* dictionary pseudo-commands */
#define SRS_DICTCMD_SWITCH    "__switch_dict__"
#define SRS_DICTCMD_PUSH      "__push_dict__"
#define SRS_DICTCMD_POP       "__pop_dict__"

#define SRS_DICT_SWITCH(dict) SRS_DICTCMD_SWITCH"("dict")"
#define SRS_DICT_PUSH(dict)   SRS_DICTCMD_PUSH"("dict")"
#define SRS_DICT_POP()        SRS_DICTCMD_POP


/*
 * special tokens
 */

#define SRS_TOKEN_SWITCHDICT "__switch_dict__"
#define SRS_TOKEN_PUSHDICT   "__push_dict__"
#define SRS_TOKEN_POPDICT    "__pop_dict__"

#define SRS_TOKEN_WILDCARD "*"           /* match till end of utterance */

/*
 * dictionary operations
 */

/* dictionary operations */
typedef enum {
    SRS_DICT_OP_UNKNOWN = 0,
    SRS_DICT_OP_SWITCH,
    SRS_DICT_OP_PUSH,
    SRS_DICT_OP_POP
} srs_dict_op_t;


/*
 * connected clients
 */

typedef struct {
    /* recognizer interface */
    int (*notify_focus)(srs_client_t *c, srs_voice_focus_t focus);
    int (*notify_command)(srs_client_t *c, int idx, int ntoken,
                          char **tokens, uint32_t *start, uint32_t *end,
                          srs_audiobuf_t *audio);
    /* voice rendering interface */
    int (*notify_render)(srs_client_t *c, srs_voice_event_t *event);
} srs_client_ops_t;


struct srs_client_s {
    mrp_list_hook_t         hook;        /* to list of clients */
    srs_client_type_t       type;        /* client type */
    char                   *name;        /* client name */
    char                   *appclass;    /* client application class */
    srs_command_t          *commands;    /* client command set */
    int                     ncommand;    /* number of commands */
    char                   *id;          /* client id */
    srs_context_t          *srs;         /* context back pointer */
    srs_resset_t           *rset;        /* resource set */
    srs_voice_focus_t       requested;   /* requested voice focus */
    int                     granted;     /* granted resources */
    int                     enabled : 1; /* interested in commands */
    int                     shared : 1;  /* whether voice focus is shared */
    mrp_list_hook_t         voices;      /* unfinished voice requests */
    srs_client_ops_t        ops;         /* client ops (notifications)  */
    void                   *user_data;   /* opaque client data */
};


/** Create a new client. */
srs_client_t *client_create(srs_context_t *srs, srs_client_type_t type,
                            const char *name, const char *appclass,
                            char **commands, int ncommand,
                            const char *id, srs_client_ops_t *ops,
                            void *user_data);

/** Destroy a client. */
void client_destroy(srs_client_t *c);

/** Look up a client by its id. */
srs_client_t *client_lookup_by_id(srs_context_t *srs, const char *id);

/** Request client focus change. */
int client_request_focus(srs_client_t *c, srs_voice_focus_t focus);

/** Deliver a command notification event to the client. */
void client_notify_command(srs_client_t *c, int idx, int ntoken,
                           const char **tokens, uint32_t *start, uint32_t *end,
                           srs_audiobuf_t *audio);

/** Request synthesizing a message. */
uint32_t client_render_voice(srs_client_t *c, const char *msg,
                             const char *voice, int timeout, int notify_events);

/** Cancel/stop a synthesizing request. */
void client_cancel_voice(srs_client_t *c, uint32_t id);

/** Create resources for all registered clients. */
void client_create_resources(srs_context_t *srs);

/** Reset the resource sets of all clients. */
void client_reset_resources(srs_context_t *srs);

/** Query voice actors. */
int client_query_voices(srs_client_t *c, const char *language,
                        srs_voice_actor_t **actorsp);

/** Free voice actor query reult. */
    void client_free_queried_voices(srs_voice_actor_t *actors);

#endif /* __SRS_DAEMON_CLIENT_H__ */
