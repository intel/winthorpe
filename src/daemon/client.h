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
#include "src/daemon/resourceif.h"


/*
 * client types
 */

typedef enum {
    SRS_CLIENT_TYPE_UNKNOWN = 0,
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

typedef struct {
    char **tokens;                       /* tokens of this command */
    int    ntoken;                       /* number of tokens */
} srs_command_t;


/*
 * connected clients
 */

typedef struct {
    int (*notify_focus)(srs_client_t *c, srs_voice_focus_t focus);
    int (*notify_command)(srs_client_t *c, int ntoken, char **tokens);
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
    mrp_res_resource_set_t *rset;        /* resource set */
    srs_voice_focus_t       requested;   /* requested voice focus */
    srs_voice_focus_t       granted;     /* granted voice focus */
    srs_voice_focus_t       focus;       /* requested voice focus */
    int                     enabled : 1; /* interested in commands */
    int                     allowed : 1; /* has resource granted */
    srs_client_ops_t       *ops;         /* client ops (notifications)  */
};


/** Create a new client. */
srs_client_t *client_create(srs_context_t *srs, srs_client_type_t type,
                        const char *name, const char *appclass,
                        char **commands, int ncommand,
                        const char *id, srs_client_ops_t *ops);

/** Destroy a client. */
void client_destroy(srs_client_t *c);

/** Look up a client by its id. */
srs_client_t *client_lookup_by_id(srs_context_t *srs, const char *id);

/** Request client focus change. */
int client_request_focus(srs_client_t *c, srs_voice_focus_t focus);

/** Create resources for all registered clients. */
void client_create_resources(srs_context_t *srs);

/** Reset the resource sets of all clients. */
void client_reset_resources(srs_context_t *srs);

/** Deliver a resource notification event to the client. */
void client_resource_event(srs_client_t *c, srs_resset_event_t event);

#endif /* __SRS_DAEMON_CLIENT_H__ */
