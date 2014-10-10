/*
 * Copyright (c) 2014, Intel Corporation
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

#ifndef __SRS_W3C_SERVER_H__
#define __SRS_W3C_SERVER_H__

/** Transport address configuration key. */
#define CONFIG_ADDRESS  "w3c-speech.address"

/** Default transport address. */
#define DEFAULT_ADDRESS "unxs:@winthorpe.w3c-speech"

/** Transport socket configuration key, set only by socket-based activation. */
#define CONFIG_SOCKET   "w3c-speech.socket"

/** Default transport socket. */
#define DEFAULT_SOCKET  -1

/** Grammar directory configuration key. */
#define CONFIG_GRAMMARDIR "w3c-speech.grammars"

/** Default grammar directory. */
#define DEFAULT_GRAMMARDIR "/etc/speech-recongition/w3c-grammars"

/** Winthorpe W3C grammar URI prefix. */
#define W3C_URI "winthorpe://"

#endif /* __SRS_W3C_SERVER_H__ */
