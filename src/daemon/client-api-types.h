/*
 * Copyright (c) 2012 - 2013, Intel Corporation
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

#ifndef __SRS_CLIENT_API_TYPES_H__
#define __SRS_CLIENT_API_TYPES_H__

/*
 * voice focus types
 */

typedef enum {
    SRS_VOICE_FOCUS_NONE = 0,            /* focus released */
    SRS_VOICE_FOCUS_SHARED,              /* normal shared voice focus */
    SRS_VOICE_FOCUS_EXCLUSIVE,           /* exclusive voice focus */
} srs_voice_focus_t;


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

#endif /* __SRS_CLIENT_API_TYPES_H__ */
