#ifndef __SRS_ESPEAK_VOICE_H__
#define __SRS_ESPEAK_VOICE_H__

#include <pulse/mainloop.h>

#include "srs/daemon/plugin.h"
#include "srs/daemon/voice.h"

typedef struct {
    srs_plugin_t      *self;             /* our plugin instance */
    srs_context_t     *srs;              /* SRS context */
    srs_voice_actor_t *actors;           /* loaded voices */
    int                nactor;           /* number of voices */
    struct {
        const char    *voicedir;         /* voice directory */
        int            rate;             /* sample rate */
    } config;
    struct {
        srs_voice_notify_t  notify;      /* voice notification callback */
        void               *notify_data; /* opaque notification data */
    } voice;
} espeak_t;

#endif /* __SRS_ESPEAK_VOICE_H__ */
