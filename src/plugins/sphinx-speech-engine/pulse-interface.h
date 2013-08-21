#ifndef __SRS_POCKET_SPHINX_PULSE_INTERFACE_H__
#define __SRS_POCKET_SPHINX_PULSE_INTERFACE_H__

#include <pulse/pulseaudio.h>
#include <pulse/mainloop.h>

#include "sphinx-plugin.h"

struct pulse_interface_s {
    pa_mainloop_api *api;
    pa_context *pactx;
    pa_stream *stream;
    bool conup;
    bool corked;
};

int  pulse_interface_create(context_t *ctx, pa_mainloop_api *api);
void pulse_interface_destroy(context_t *ctx);

void pulse_interface_cork_input_stream(context_t *ctx, bool cork);

#endif /* __SRS_POCKET_SPHINX_PULSE_INTERFACE_H__ */

/*
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 *
 */
