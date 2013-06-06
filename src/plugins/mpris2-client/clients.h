#ifndef __SRS_MPRIS2_CLIENT_H__
#define __SRS_MPRIS2_CLIENT_H__

#include "mpris2-plugin.h"

enum player_state_e {
    UNKNOWN = 0,
    PLAY,
    PAUSE,
    STOP
};

struct player_s {
    context_t *ctx;
    const char *name;
    const char *service;
    const char *object;
    const char *address;
    player_state_t state;
    struct {
        player_state_t state;
        uint64_t time;
    } request;
    bool ready;
    size_t nlist;
    playlist_t *lists;
    playlist_t *active_list;
    double volume;
    pa_time_event *timer;
};

struct playlist_s {
    const char *id;
    const char *name;
};

enum track_e {
    TRACK_UNKNOWN = 0,
    NEXT_TRACK,
    PREVIOUS_TRACK
};


int  clients_create(context_t *ctx);
void clients_destroy(context_t *ctx);

int  clients_register_player(context_t *ctx, const char *name,
                             const char *service, const char *object);

int  clients_start(context_t *ctx);
int  clients_stop(context_t *ctx);

player_t *clients_find_player_by_address(context_t *ctx, const char *address);
player_t *clients_find_player_by_name(context_t *ctx, const char *name);

void clients_player_appeared(context_t *ctx, const char *name,
                             const char *address);
void clients_player_disappeared(context_t *ctx, const char *name);
void clients_player_state_changed(player_t *player, player_state_t state);
void clients_player_status_changed(player_t *player, bool ready);
void clients_player_volume_changed(player_t *player, double volume);
void clients_playlist_changed(player_t *player,size_t nlist,playlist_t *lists);

void clients_player_request_state(player_t *player, player_state_t state);
void clients_player_request_track(player_t *player, track_t track);

void clients_player_adjust_volume(player_t *player, double adjust);

void clients_player_show(player_t *player);
void clients_player_quit(player_t *player);


#endif /* __SRS_MPRIS2_CLIENT_H__ */

/*
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 *
 */
