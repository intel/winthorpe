#ifndef __SRS_MPRIS2_DBUS_INTERFACE_H__
#define __SRS_MPRIS2_DBUS_INTERFACE_H__

#include <stdint.h>
#include <stdbool.h>

#include <murphy/common/mainloop.h>

#include "mpris2-plugin.h"

int  dbusif_create(context_t *ctx, mrp_mainloop_t *ml);
void dbusif_destroy(context_t *ctx);

int  dbusif_register_player(context_t *ctx, const char *name);
void dbusif_unregister_player(context_t *ctx, const char *name);

void dbusif_query_player_properties(player_t *player);
void dbusif_introspect_player(player_t *player);

void dbusif_set_player_state(player_t *player, player_state_t state);

void dbusif_query_playlists(player_t *player);
void dbusif_set_playlist(player_t *player, const char *id);


#endif /* __SRS_MPRIS2_DBUS_INTERFACE_H__ */

/*
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 *
 */
