#ifndef __SRS_BLUETOOTH_PLUGIN_H__
#define __SRS_BLUETOOTH_PLUGIN_H__

#include <stdint.h>
#include <stdbool.h>

#include "src/daemon/plugin.h"
#include "src/daemon/client.h"

#define PLUGIN_NAME           "bluetooth-voice-recognition"
#define BLUETOOTH_PREFIX      "bluetooth."

typedef enum   hfp_state_e     hfp_state_t;

typedef struct context_s       context_t;
typedef struct dbusif_s        dbusif_t;
typedef struct pulseif_s       pulseif_t;
typedef struct clients_s       clients_t;
typedef struct modem_s         modem_t;
typedef struct card_s          card_t;
typedef struct device_s        device_t;

struct context_s {
    srs_plugin_t *plugin;
    dbusif_t *dbusif;
    pulseif_t *pulseif;
    clients_t *clients;
};


#endif /* __SRS_BLUETOOTH_PLUGIN_H__ */

/*
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 *
 */
