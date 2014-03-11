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

#include <stdbool.h>
#include <errno.h>

#include <murphy/common/native-types.h>

#include "native-messages.h"

static mrp_typemap_t *map = NULL;

mrp_typemap_t *register_message_types(void)
{
    static bool          done       = false;
    static mrp_typemap_t type_map[] = {
        MRP_TYPEMAP(SRS_REQUEST_REGISTER   , MRP_INVALID_TYPE),
        MRP_TYPEMAP(SRS_REQUEST_UNREGISTER , MRP_INVALID_TYPE),
        MRP_TYPEMAP(SRS_REQUEST_FOCUS      , MRP_INVALID_TYPE),
        MRP_TYPEMAP(SRS_REQUEST_RENDERVOICE, MRP_INVALID_TYPE),
        MRP_TYPEMAP(SRS_REQUEST_CANCELVOICE, MRP_INVALID_TYPE),
        MRP_TYPEMAP(SRS_REQUEST_QUERYVOICES, MRP_INVALID_TYPE),
        MRP_TYPEMAP(SRS_REPLY_STATUS       , MRP_INVALID_TYPE),
        MRP_TYPEMAP(SRS_REPLY_RENDERVOICE  , MRP_INVALID_TYPE),
        MRP_TYPEMAP(SRS_VOICE_ACTOR        , MRP_INVALID_TYPE),
        MRP_TYPEMAP(SRS_REPLY_QUERYVOICES  , MRP_INVALID_TYPE),
        MRP_TYPEMAP(SRS_EVENT_FOCUS        , MRP_INVALID_TYPE),
        MRP_TYPEMAP(SRS_EVENT_COMMAND      , MRP_INVALID_TYPE),
        MRP_TYPEMAP(SRS_EVENT_VOICE        , MRP_INVALID_TYPE),
        MRP_TYPEMAP_END
    };

    MRP_NATIVE_TYPE(reg_req, srs_req_register_t,
                    MRP_UINT32(srs_req_register_t, type    , DEFAULT),
                    MRP_UINT32(srs_req_register_t, reqno   , DEFAULT),
                    MRP_STRING(srs_req_register_t, name    , DEFAULT),
                    MRP_STRING(srs_req_register_t, appclass, DEFAULT),
                    MRP_ARRAY (srs_req_register_t, commands, DEFAULT, SIZED,
                               char *, ncommand),
                    MRP_UINT32(srs_req_register_t, ncommand, DEFAULT));

    MRP_NATIVE_TYPE(bye_req, srs_req_unregister_t,
                    MRP_UINT32(srs_req_unregister_t, type  , DEFAULT),
                    MRP_UINT32(srs_req_unregister_t, reqno , DEFAULT));

    MRP_NATIVE_TYPE(status_rpl, srs_rpl_status_t,
                    MRP_UINT32(srs_rpl_status_t, type  , DEFAULT),
                    MRP_UINT32(srs_rpl_status_t, reqno , DEFAULT),
                    MRP_UINT32(srs_rpl_status_t, status, DEFAULT),
                    MRP_STRING(srs_rpl_status_t, msg   , DEFAULT));

    MRP_NATIVE_TYPE(focus_req, srs_req_focus_t,
                    MRP_UINT32(srs_req_focus_t, type  , DEFAULT),
                    MRP_UINT32(srs_req_focus_t, reqno , DEFAULT),
                    MRP_UINT32(srs_req_focus_t, focus, DEFAULT));

    MRP_NATIVE_TYPE(focus_evt, srs_evt_focus_t,
                    MRP_UINT32(srs_evt_focus_t, type  , DEFAULT),
                    MRP_UINT32(srs_evt_focus_t, focus, DEFAULT));

    MRP_NATIVE_TYPE(voice_req, srs_req_voice_t,
                    MRP_UINT32(srs_req_voice_t, type    , DEFAULT),
                    MRP_UINT32(srs_req_voice_t, reqno   , DEFAULT),
                    MRP_STRING(srs_req_voice_t, msg     , DEFAULT),
                    MRP_STRING(srs_req_voice_t, voice   , DEFAULT),
                    MRP_DOUBLE(srs_req_voice_t, rate    , DEFAULT),
                    MRP_DOUBLE(srs_req_voice_t, pitch   , DEFAULT),
                    MRP_UINT32(srs_req_voice_t, timeout , DEFAULT),
                    MRP_UINT32(srs_req_voice_t, events  , DEFAULT));

    MRP_NATIVE_TYPE(voice_rpl, srs_rpl_voice_t,
                    MRP_UINT32(srs_rpl_voice_t, type , DEFAULT),
                    MRP_UINT32(srs_rpl_voice_t, reqno, DEFAULT),
                    MRP_UINT32(srs_rpl_voice_t, id   , DEFAULT));

    MRP_NATIVE_TYPE(voice_ccl, srs_ccl_voice_t,
                    MRP_UINT32(srs_ccl_voice_t, type , DEFAULT),
                    MRP_UINT32(srs_ccl_voice_t, reqno, DEFAULT),
                    MRP_UINT32(srs_ccl_voice_t, id   , DEFAULT));

    MRP_NATIVE_TYPE(voice_evt, srs_evt_voice_t,
                    MRP_UINT32(srs_evt_voice_t, type , DEFAULT),
                    MRP_UINT32(srs_evt_voice_t, event, DEFAULT),
                    MRP_UINT32(srs_evt_voice_t, id   , DEFAULT),
                    MRP_DOUBLE(srs_evt_voice_t, pcnt , DEFAULT),
                    MRP_UINT32(srs_evt_voice_t, msec , DEFAULT));

    MRP_NATIVE_TYPE(voice_qry, srs_req_voiceqry_t,
                    MRP_UINT32(srs_req_voiceqry_t, type , DEFAULT),
                    MRP_UINT32(srs_req_voiceqry_t, reqno, DEFAULT),
                    MRP_STRING(srs_req_voiceqry_t, lang , DEFAULT));

    MRP_NATIVE_TYPE(voice_act, srs_voice_actor_t,
                    MRP_UINT32(srs_voice_actor_t, id         , DEFAULT),
                    MRP_STRING(srs_voice_actor_t, lang       , DEFAULT),
                    MRP_STRING(srs_voice_actor_t, dialect    , DEFAULT),
                    MRP_UINT16(srs_voice_actor_t, gender     , DEFAULT),
                    MRP_UINT16(srs_voice_actor_t, age        , DEFAULT),
                    MRP_STRING(srs_voice_actor_t, name       , DEFAULT),
                    MRP_STRING(srs_voice_actor_t, description, DEFAULT));

    MRP_NATIVE_TYPE(voiceqry_rpl, srs_rpl_voiceqry_t,
                    MRP_UINT32(srs_rpl_voiceqry_t, type  , DEFAULT),
                    MRP_UINT32(srs_rpl_voiceqry_t, reqno , DEFAULT),
                    MRP_ARRAY (srs_rpl_voiceqry_t, actors, DEFAULT, SIZED,
                               srs_voice_actor_t, nactor),
                    MRP_UINT32(srs_rpl_voiceqry_t, nactor, DEFAULT));

    MRP_NATIVE_TYPE(command_evt, srs_evt_command_t,
                    MRP_UINT32(srs_evt_command_t, type, DEFAULT),
                    MRP_UINT32(srs_evt_command_t, idx   , DEFAULT),
                    MRP_ARRAY (srs_evt_command_t, tokens, DEFAULT, SIZED,
                               char *, ntoken),
                    MRP_UINT32(srs_evt_command_t, ntoken, DEFAULT));

    struct {
        uint32_t           id;
        mrp_native_type_t *type;
    } types[SRS_MSG_MAX] = {
        { SRS_REQUEST_REGISTER   , &reg_req      },
        { SRS_REQUEST_UNREGISTER , &bye_req      },
        { SRS_REQUEST_FOCUS      , &focus_req    },
        { SRS_REQUEST_RENDERVOICE, &voice_req    },
        { SRS_REQUEST_CANCELVOICE, &voice_ccl    },
        { SRS_REQUEST_QUERYVOICES, &voice_qry    },
        { SRS_REPLY_STATUS       , &status_rpl   },
        { SRS_REPLY_RENDERVOICE  , &voice_rpl    },
        { SRS_VOICE_ACTOR        , &voice_act    },
        { SRS_REPLY_QUERYVOICES  , &voiceqry_rpl },
        { SRS_EVENT_FOCUS        , &focus_evt    },
        { SRS_EVENT_COMMAND      , &command_evt  },
        { SRS_EVENT_VOICE        , &voice_evt    },
        { MRP_INVALID_TYPE       , NULL          },
    }, *t;
    mrp_typemap_t *m;

    if (done)
        return map;

    for (t = types, m = type_map; t->type != NULL; t++, m++) {
        if ((m->type_id = mrp_register_native(t->type)) == MRP_INVALID_TYPE)
            return NULL;
    }

    done = true;
    return (map = &type_map[0]);
}


uint32_t message_typeid(uint32_t type)
{
    if (map != NULL && 0 < type && type < SRS_MSG_MAX)
        return map[type - 1].type_id;
    else
        return MRP_INVALID_TYPE;
}


uint32_t message_type(uint32_t type_id)
{
    mrp_typemap_t *m;

    if (map != NULL) {
        for (m = map; m->type_id != MRP_INVALID_TYPE; m++)
            if (m->type_id == type_id)
                return m->mapped;
    }

    return MRP_INVALID_TYPE;
}


int send_message(mrp_transport_t *t, srs_msg_t *msg)
{
    uint32_t type_id = message_typeid(msg->type);

    if (type_id != MRP_INVALID_TYPE) {
        if (mrp_transport_sendnative(t, msg, type_id))
            return 0;
    }
    else
        errno = EINVAL;

    return -1;
}
