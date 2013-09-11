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

#ifndef __SRS_DBUS_PLUGIN_CONFIG_H__
#define __SRS_DBUS_PLUGIN_CONFIG_H__

#define SRS_CLIENT_SERVICE   "org.tizen.winthorpe"
#define SRS_CLIENT_PATH      "/winthorpe"
#define SRS_CLIENT_INTERFACE "org.tizen.winthorpe"

#define SRS_CLIENT_REGISTER        "Register"
#define SRS_CLIENT_UNREGISTER      "Unregister"

#define SRS_CLIENT_NOTIFY_COMMAND  "VoiceCommand"
#define SRS_CLIENT_REQUEST_FOCUS   "RequestFocus"
#define SRS_CLIENT_NOTIFY_FOCUS    "FocusChanged"

#define SRS_CLIENT_RENDER_VOICE    "RenderVoice"
#define SRS_CLIENT_CANCEL_VOICE    "CancelVoice"
#define SRS_CLIENT_NOTIFY_VOICE    "VoiceEvent"
#define SRS_CLIENT_VOICE_STARTED   "started"
#define SRS_CLIENT_VOICE_PROGRESS  "progress"
#define SRS_CLIENT_VOICE_COMPLETED "completed"
#define SRS_CLIENT_VOICE_TIMEOUT   "timeout"
#define SRS_CLIENT_VOICE_ABORTED   "aborted"

#define SRS_CLIENT_QUERY_VOICES    "QueryVoices"
#endif /* __SRS_DBUS_PLUGIN_CONFIG_H__ */
