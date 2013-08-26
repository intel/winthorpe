#ifndef __SRS_BLUETOOTH_PULSEIF_H__
#define __SRS_BLUETOOTH_PULSEIF_H__

#include <pulse/pulseaudio.h>
#include <pulse/mainloop.h>
#include <pulse/subscribe.h>

#include "bluetooth-plugin.h"

struct pulseif_s {
    pa_mainloop_api *paapi;
    pa_context *pactx;
    pa_operation *subscr;
    mrp_list_hook_t cards;
    mrp_list_hook_t pending_ops;
    bool conup;
    uint32_t rate;
    struct {
        double upper;
        double lower;
    } limit;
};

struct card_s {
    mrp_list_hook_t link;
    context_t *ctx;
    uint32_t idx;
    const char *name;
    const char *btaddr;
    const char *profnam;
    device_t *device;
    struct {
        uint32_t idx;
        const char *name;
    } sink;
    struct {
        uint32_t idx;
        const char *name;
    } source;
    struct {
        pa_stream *stream;
        enum {
            ST_BEGIN = 0,
            ST_CLING,
            ST_READY
        } state;
    } input;
    struct {
        pa_stream *stream;
        size_t sent;
    } output;
};




int  pulseif_create(context_t *ctx, pa_mainloop_api *pa);
void pulseif_destroy(context_t *ctx);

int pulseif_set_card_profile(card_t *card, const char *profnam);

int pulseif_add_output_stream_to_card(card_t *card);
int pulseif_remove_output_stream_from_card(card_t *card);

int pulseif_add_input_stream_to_card(card_t *card);
int pulseif_remove_input_stream_from_card(card_t *card);


#endif /* __SRS_BLUETOOTH_PULSEIF_H__ */

/*
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 *
 */
