/*
 * Copyright (c) 2012, 2013, Intel Corporation
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

#ifndef __SRS_NATIVE_MESSAGES_H__
#define __SRS_NATIVE_MESSAGES_H__

#include <stdint.h>
#include <murphy/common/native-types.h>
#include <murphy/common/transport.h>

#include "srs/daemon/voice-api-types.h"


/*
 * message types
 */

typedef enum {
    SRS_MSG_UNKNOWN = 0,

    SRS_REQUEST_REGISTER,
    SRS_REQUEST_UNREGISTER,
    SRS_REQUEST_FOCUS,
    SRS_REQUEST_RENDERVOICE,
    SRS_REQUEST_CANCELVOICE,
    SRS_REQUEST_QUERYVOICES,

    SRS_REPLY_STATUS,
    SRS_REPLY_RENDERVOICE,
    SRS_VOICE_ACTOR,
    SRS_REPLY_QUERYVOICES,

    SRS_EVENT_FOCUS,
    SRS_EVENT_COMMAND,
    SRS_EVENT_VOICE,

    SRS_MSG_MAX
} srs_msg_type_t;


/*
 * registration request
 */

typedef struct {
    uint32_t   type;                     /* SRS_REQUEST_REGISTER */
    uint32_t   reqno;                    /* request number */
    char      *name;                     /* application name */
    char      *appclass;                 /* application class */
    char     **commands;                 /* speech commands */
    uint32_t   ncommand;                 /* number of speech commands */
} srs_req_register_t;


/*
 * unregistration request
 */

typedef struct {
    uint32_t type;                       /* SRS_REQUEST_UNREGISTER */
    uint32_t reqno;                      /* request number */
} srs_req_unregister_t;


/*
 * error codes
 */

enum {
    SRS_STATUS_OK = 0,                   /* success */
    SRS_STATUS_FAILED,                   /* request failed */
};


/*
 * a status reply
 */

typedef struct {
    uint32_t  type;                      /* message type */
    uint32_t  reqno;                     /* request number */
    uint32_t  status;                    /* an error code, or 0 */
    char     *msg;                       /* error message, if any */
} srs_rpl_status_t;


/*
 * voice focus request
 */

typedef struct {
    uint32_t type;                       /* SRS_REQUEST_FOCUS */
    uint32_t reqno;                      /* request number */
    uint32_t focus;                      /* current voice focus */
} srs_req_focus_t;


/*
 * voice focus notification
 */

typedef struct {
    uint32_t type;                       /* SRS_EVENT_FOCUS */
    uint32_t focus;                      /* granted focus */
} srs_evt_focus_t;


/*
 * voice render request
 */

typedef struct {
    uint32_t  type;                      /* SRS_REQUEST_VOICERENDER */
    uint32_t  reqno;                     /* request number */
    char     *msg;                       /* message to render */
    char     *voice;                     /* voice to use */
    uint32_t  timeout;                   /* message timeout */
    uint32_t  events;                    /* mask of events to notify about */
} srs_req_voice_t;


/*
 * reply to voice render request
 */

typedef struct {
    uint32_t  type;                      /* SRS_REPLY_RENDERVOICE */
    uint32_t  reqno;                     /* request number */
    uint32_t  id;                        /* client-side id */
} srs_rpl_voice_t;


/*
 * voice cancel request
 */

typedef struct {
    uint32_t  type;                      /* SRS_REQUEST_VOICECANCEL */
    uint32_t  reqno;                     /* request number */
    uint32_t  id;                        /* voice render id */
} srs_ccl_voice_t;


/*
 * voice progress notification event
 */

typedef struct {
    uint32_t type;                       /* SRS_EVENT_VOICE */
    uint32_t event;                      /* SRS_VOICE_EVENT_* */
    uint32_t id;                         /* voice render id */
    double   pcnt;                       /* progress in percentages */
    uint32_t msec;                       /* progress in milliseconds */
} srs_evt_voice_t;


/*
 * voice query request
 */

typedef struct {
    uint32_t  type;                      /* SRS_REQUEST_QUERYVOICES */
    uint32_t  reqno;                     /* request number */
    char     *lang;                      /* language to request voices for */
} srs_req_voiceqry_t;


/*
 * voice query reply
 */

typedef struct {
    uint32_t           type;             /* SRS_REPLY_QUERYVOICES */
    uint32_t           reqno;            /* request number */
    srs_voice_actor_t *actors;           /* queried actors */
    uint32_t           nactor;           /* number of actors */
} srs_rpl_voiceqry_t;


/*
 * command notification event
 */

typedef struct {
    uint32_t   type;                     /* SRS_EVENT_COMMAND */
    uint32_t   idx;                      /* client command index */
    char     **tokens;                   /* command tokens */
    uint32_t   ntoken;                   /* number of tokens */
} srs_evt_command_t;


/*
 * a generic request or reply
 */

typedef struct {
    uint32_t type;                       /* message type */
    uint32_t reqno;                      /* request number */
} srs_req_any_t;

typedef srs_req_any_t srs_rpl_any_t;

/*
 * message
 */

typedef union {
    uint32_t               type;
    srs_req_any_t          any_req;
    srs_rpl_any_t          any_rpl;
    srs_req_register_t     reg_req;
    srs_req_unregister_t   bye_req;
    srs_rpl_status_t       status_rpl;
    srs_req_focus_t        focus_req;
    srs_evt_focus_t        focus_evt;
    srs_req_voice_t        voice_req;
    srs_rpl_voice_t        voice_rpl;
    srs_ccl_voice_t        voice_ccl;
    srs_evt_voice_t        voice_evt;
    srs_req_voiceqry_t     voice_qry;
    srs_rpl_voiceqry_t     voice_lst;
    srs_evt_command_t      command_evt;
} srs_msg_t;


mrp_typemap_t *register_message_types(void);
int send_message(mrp_transport_t *t, srs_msg_t *msg);
uint32_t message_typeid(uint32_t type);
uint32_t message_type(uint32_t typeid);

#endif /* __SRS_NATIVE_MESSAGES_H__ */
