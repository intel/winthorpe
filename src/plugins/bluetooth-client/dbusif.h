#ifndef __SRS_MPRIS2_DBUS_INTERFACE_H__
#define __SRS_MPRIS2_DBUS_INTERFACE_H__

#include <stdint.h>
#include <stdbool.h>

#include <murphy/common/mainloop.h>

#include "bluetooth-plugin.h"

enum hfp_state_e {
    VOICE_RECOGNITION_UNKNOWN,
    VOICE_RECOGNITION_ON,
    VOICE_RECOGNITION_OFF
};

struct modem_s {
    mrp_list_hook_t link;
    const char *path;
    const char *name;
    const char *addr;
    context_t *ctx;
    hfp_state_t state;
    device_t *device;
    int refcnt;
};

int  dbusif_create(context_t *ctx, mrp_mainloop_t *ml);
void dbusif_destroy(context_t *ctx);

int  dbusif_start(context_t *ctx);
void dbusif_stop(context_t *ctx);

int dbusif_set_voice_recognition(modem_t *modem, hfp_state_t state);


#endif /* __SRS_MPRIS2_DBUS_INTERFACE_H__ */

/*
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 *
 */
