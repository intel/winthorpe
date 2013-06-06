#ifndef __SRS_BLUETOOTH_CLIENT_H__
#define __SRS_BLUETOOTH_CLIENT_H__

#include "bluetooth-plugin.h"


/*
 * A bluetooth connected device
 * capable of voice recognition
 */
struct device_s {
    context_t *ctx;
    const char *btaddr;
    modem_t *modem;
    card_t *card;
    bool active;
    size_t nsample;
    int16_t *samples;
};


int  clients_create(context_t *ctx);
void clients_destroy(context_t *ctx);

int clients_start(context_t *ctx);
int clients_stop(context_t *ctx);

device_t *clients_add_device(context_t *ctx, const char *btaddr);
void clients_remove_device(device_t *device);
device_t *clients_find_device(context_t *ctx, const char *btaddr);
bool clients_device_is_ready(device_t *device);

void clients_add_card_to_device(device_t *device, card_t *card);
void clients_remove_card_from_device(device_t *device);

void clients_stop_recognising_voice(device_t *device);

#endif /* __SRS_BLUETOOTH_CLIENT_H__ */

/*
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 *
 */
