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

#ifndef __SRS_DAEMON_RESCTL_H__
#define __SRS_DAEMON_RESCTL_H__

#include <murphy/common/list.h>

typedef struct srs_resctx_s srs_resctx_t;
typedef struct srs_resset_s srs_resset_t;

#include "srs/daemon/context.h"

typedef enum {
    SRS_RESCTL_EVENT_UNKNOWN = 0,
    SRS_RESCTL_EVENT_CONNECTION,         /* connection up/down event */
    SRS_RESCTL_EVENT_RESOURCE,           /* resource state change event */
    SRS_RESCTL_EVENT_DESTROYED,          /* set destroyed event */
} srs_resctl_event_type_t;

typedef enum {
    SRS_RESCTL_MASK_NONE = 0x0,          /* no resources */
    SRS_RESCTL_MASK_SREC = 0x1,          /* speech recognition */
    SRS_RESCTL_MASK_SYNT = 0x2,          /* speech synthesis */
} srs_resctl_mask_t;

typedef union {
    srs_resctl_event_type_t type;        /* event type */
    struct {                             /* connection event */
        srs_resctl_event_type_t type;    /* SRS_RESCTL_CONNECTION */
        int                     up;      /* whether connected */
    } connection;
    struct {                             /* resource state change event */
        srs_resctl_event_type_t type;    /* SRS_RESCTL_STATE */
        srs_resctl_mask_t       granted; /* mask of granted resources */
    } resource;
} srs_resctl_event_t;



typedef void (*srs_resctl_event_cb_t)(srs_resctl_event_t *e, void *user_data);

int srs_resctl_connect(srs_context_t *srs, srs_resctl_event_cb_t cb,
                       void *user_data, int reconnect);
void srs_resctl_disconnect(srs_context_t *srs);

srs_resset_t *srs_resctl_create(srs_context_t *srs, char *appclass,
                                srs_resctl_event_cb_t cb, void *user_data);
void srs_resctl_destroy(srs_resset_t *set);

int srs_resctl_online(srs_context_t *srs, srs_resset_t *set);
void srs_resctl_offline(srs_resset_t *set);

int srs_resctl_acquire(srs_resset_t *set, int shared);
int srs_resctl_release(srs_resset_t *set);

#endif /* __SRS_DAEMON_RESCTL_H__ */
