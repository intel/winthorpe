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

#ifndef __SRS_NATIVE_CLIENT_H__
#define __SRS_NATIVE_CLIENT_H__

#include <murphy/common/macros.h>
#include <glib.h>

#include "srs/daemon/client-api-types.h"
#include "srs/daemon/voice-api-types.h"

MRP_CDECL_BEGIN

/** Opaque SRS client context type. */
typedef struct srs_s srs_t;

/** Connection status notification callback type. */
typedef void (*srs_connect_notify_t)(srs_t *srs, int status, const char *msg,
                                     void *user_data);

/** Command notification callback type. */
typedef void (*srs_command_notify_t)(srs_t *srs, int idx, char **tokens,
                                     int ntoken, void *user_data);

/** Focus notification callback type. */
typedef void (*srs_focus_notify_t)(srs_t *srs, srs_voice_focus_t granted,
                                   void *user_data);

/** Voice rendering notification callback type. */
typedef void (*srs_render_notify_t)(srs_t *srs, srs_voice_event_t *event,
                                    void *user_data, void *notify_data);

/** Voice query notification callback type. */
typedef void (*srs_voiceqry_notify_t)(srs_t *srs, srs_voice_actor_t *actors,
                                      int nactor, void *user_data,
                                      void *notify_data);

/** Use the given Murphy mainloop as the underlying mainloop. */
void srs_set_mainloop(mrp_mainloop_t *ml);

/** Use the given GMainLoop as the underlying mainloop. */
void srs_set_gmainloop(GMainLoop *gml);

/** Create an SRS client context with the given class and commands. */
srs_t *srs_create(const char *name, const char *appclass, char **commands,
                  size_t ncommand, srs_connect_notify_t connect_notify,
                  srs_focus_notify_t focus_notify,
                  srs_command_notify_t command_notify, void *user_data);

/** Destroy the given SRS client context. */
void srs_destroy(srs_t *srs);

/** Try to establish a connection to the server at the given address. */
int srs_connect(srs_t *srs, const char *server, int reconnect);

/** Close connection to the server if there is one. */
void srs_disconnect(srs_t *srs);

/** Request the given type of focus. */
int srs_request_focus(srs_t *srs, srs_voice_focus_t focus);

/** Request rendering the given message, subscribing for the given events. */
uint32_t srs_render_voice(srs_t *srs, const char *msg, const char *voice,
                          int timeout, int events, srs_render_notify_t cb,
                          void *cb_data);

/** Cancel an ongoing voice render request. */
int srs_cancel_voice(srs_t *srs, uint32_t id);

/** Query the available voices for the given language (or all if omitted). */
int srs_query_voices(srs_t *srs, const char *language,
                     srs_voiceqry_notify_t cb, void *cb_data);

MRP_CDECL_END

#endif /* __SRS_NATIVE_CLIENT_H__ */
