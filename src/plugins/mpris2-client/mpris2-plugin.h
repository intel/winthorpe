#ifndef __SRS_MPRIS2_PLUGIN_H__
#define __SRS_MPRIS2_PLUGIN_H__

#include <stdint.h>
#include <stdbool.h>

#include "srs/daemon/plugin.h"
#include "srs/daemon/client.h"

#define PLUGIN_NAME    "music-player"
#define MPRIS2_PREFIX  "mpris2."

typedef enum   player_state_e  player_state_t;
typedef enum   track_e         track_t;

typedef struct context_s       context_t;
typedef struct dbusif_s        dbusif_t;
typedef struct clients_s       clients_t;
typedef struct player_s        player_t;
typedef struct playlist_s      playlist_t;

struct context_s {
    srs_plugin_t *plugin;
    dbusif_t *dbusif;
    clients_t *clients;
};


#endif /* __SRS_MPRIS2_PLUGIN_H__ */

/*
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 *
 */
