#ifndef __SRS_FESTIVAL_VOICE_H__
#define __SRS_FESTIVAL_VOICE_H__

#include <pulse/mainloop.h>

#include "srs/daemon/plugin.h"
#include "srs/daemon/voice.h"

typedef struct {
    srs_plugin_t      *self;             /* our plugin instance */
    srs_context_t     *srs;              /* SRS context */
    srs_voice_actor_t *actors;           /* loaded voices */
    int                nactor;           /* number of voices */
    struct {
        srs_voice_notify_t  notify;      /* voice notification callback */
        void               *notify_data; /* opaque notification data */
    } voice;
    struct {
        const char    *voices;           /* configured festival voices */
    } config;
    void              *pulse;            /* PA streams et al. state */
} festival_t;

#endif /* __SRS_FESTIVAL_VOICE_H__ */
