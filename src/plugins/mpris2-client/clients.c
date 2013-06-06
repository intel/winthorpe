#include <sys/time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <murphy/common/debug.h>

#include <murphy/common/hashtbl.h>
#include <murphy/common/utils.h>

#include "clients.h"
#include "dbusif.h"


struct clients_s {
    srs_client_t *srs_client;
    struct {
        mrp_htbl_t *name;
        mrp_htbl_t *addr;
    } player;
    player_t *current;
};

static int notify_focus(srs_client_t *, srs_voice_focus_t);
static int notify_command(srs_client_t *, int, char **);

static void schedule_delayed_request(player_t *);

static int player_register(void *, void *, void *);
static void player_free(void *, void *);

static playlist_t *playlist_dup(size_t, playlist_t *);
static void playlist_free(size_t, playlist_t *);

static uint64_t get_current_time(void);

static char *commands[] = {
    "play music",
    "stop music",
    "show player",
    "hide player",
    NULL
};
static int ncommand = (sizeof(commands) / sizeof(commands[0])) - 1;

int clients_create(context_t *ctx)
{
    clients_t *clients;
    srs_plugin_t *pl;
    srs_context_t *srs;
    srs_client_ops_t callbacks;
    mrp_htbl_config_t cfg;

    if (!ctx || !(pl = ctx->plugin) || !(srs = pl->srs))
        return -1;

    if (!(clients = mrp_allocz(sizeof(clients_t))))
        return -1;

    callbacks.notify_focus = notify_focus;
    callbacks.notify_command = notify_command;

    clients->srs_client = client_create(srs, SRS_CLIENT_TYPE_BUILTIN,
                                        PLUGIN_NAME, "player",
                                        commands, ncommand,
                                        PLUGIN_NAME, &callbacks);

    memset(&cfg, 0, sizeof(cfg));
    cfg.nentry = 10;
    cfg.comp = mrp_string_comp;
    cfg.hash = mrp_string_hash;
    cfg.free = player_free;
    cfg.nbucket = cfg.nentry;
    clients->player.name = mrp_htbl_create(&cfg);

    cfg.free = NULL;
    clients->player.addr = mrp_htbl_create(&cfg);

    clients->current = NULL;

    ctx->clients = clients;
    
    return 0;
}


void clients_destroy(context_t *ctx)
{
    clients_t *clients;

    if (ctx && (clients = ctx->clients)) {
        ctx->clients = NULL;

        client_destroy(clients->srs_client);

        mrp_htbl_destroy(clients->player.addr, FALSE);
        mrp_htbl_destroy(clients->player.name, TRUE);
        free(clients);
    }
}

int clients_start(context_t *ctx)
{
    clients_t *clients;
    player_t *player;

    if (!ctx || !(clients = ctx->clients))
        return -1;

    mrp_htbl_foreach(clients->player.name, player_register, (void *)ctx);

    return 0;
}

int clients_stop(context_t *ctx)
{
    return 0;
}


int clients_register_player(context_t *ctx,
                            const char *name,
                            const char *service,
                            const char *object)
{
    clients_t *clients;
    player_t *player;

    if (!ctx || !name || !(clients = ctx->clients))
        return -1;

    if (!(player = mrp_allocz(sizeof(player_t))))
        return -1;

    player->ctx = ctx;
    player->name = mrp_strdup(name);
    player->service = service ? mrp_strdup(service) : NULL;
    player->object = object ? mrp_strdup(object) : NULL;

    mrp_htbl_insert(clients->player.name, (void*)player->name, (void*)player);

    mrp_log_info("Mpris2 player '%s' (service '%s' object '%s') registered",
                 player->name, player->service ? player->service : "none",
                 player->object ? player->object : "none");

    return 0;
}

player_t *clients_find_player_by_address(context_t *ctx, const char *address)
{
    clients_t *clients;
    player_t *player;

    if (!ctx || !(clients = ctx->clients))
        return NULL;

    player = mrp_htbl_lookup(clients->player.addr, (void *)address);

    return player;
}

player_t *clients_find_player_by_name(context_t *ctx, const char *name)
{
    clients_t *clients;
    player_t *player;

    if (!ctx || !(clients = ctx->clients))
        return NULL;

    player = mrp_htbl_lookup(clients->player.name, (void *)name);

    return player;
}

void clients_player_appeared(context_t *ctx,
                             const char *name,
                             const char *address)
{
    clients_t *clients;
    player_t *player;

    if (ctx && (clients = ctx->clients) && clients->player.name) {
        if ((player = mrp_htbl_lookup(clients->player.name, (void *)name))) {
            mrp_free((void *)player->address);
            player->address = mrp_strdup(address);

            mrp_htbl_insert(clients->player.addr, (void *)player->address,
                            player);

            mrp_log_info("mrpis2 client '%s' appeared (address %s)",
                         name, address);

            if (!clients->current)
                clients->current = player;

            dbusif_query_player_properties(player);
        }
    }
}

void clients_player_disappeared(context_t *ctx, const char *name)
{
    clients_t *clients;
    player_t *player, *removed;

    if (ctx && (clients = ctx->clients) && clients->player.name) {
        if ((player = mrp_htbl_lookup(clients->player.name, (void *)name))) {
            removed = mrp_htbl_remove(clients->player.addr,
                                      (void *)player->address,
                                      FALSE);
            if (player != removed) {
                mrp_log_error("mpris2 client: confused with data structures "
                              "when removing '%s'", player->address);
            }
            else {
                mrp_free((void *)player->address);
                player->address = NULL;
                player->state = UNKNOWN;
                player->ready = false;

                playlist_free(player->nlist, player->lists);
                player->nlist = 0;
                player->lists = NULL;
                player->active_list = NULL;

                mrp_log_info("mrpis2 client '%s' disappeared", name);

                if (player == clients->current) {
                    clients->current = NULL;
                }
            }
        }
    }
}

void clients_player_state_changed(player_t *player, player_state_t state)
{
    if (player)
        player->state = state;
}

void clients_player_status_changed(player_t *player, bool ready)
{
    if (player) {
        if (!player->ready && ready)
            schedule_delayed_request(player);

        player->ready = ready;
    }
}

void clients_playlist_changed(player_t *player, size_t nlist,playlist_t *lists)
{
    context_t *ctx;
    playlist_t *list, *active;
    size_t i, idx_of_active_list;

    if (!player || !(ctx = player->ctx))
        return;

    idx_of_active_list = 0;

    if ((active = player->active_list)) {
        for (i = 0;  i < nlist;  i++) {
            list = lists + i;

            if (!strcmp(active->id, list->id)) {
                idx_of_active_list = i;
                break;
            }
        }
    }

    playlist_free(player->nlist, player->lists);
    player->nlist = nlist;
    player->lists = playlist_dup(nlist, lists);
    player->active_list = player->lists + idx_of_active_list;
}

void clients_player_request_state(player_t *player, player_state_t state)
{
    if (!player || (state != PLAY && state != PAUSE && state != STOP))
        return;

    if (state == player->state)
        return;

    player->request.state = state;
    player->request.time = get_current_time();

    if (player->address)
        dbusif_set_player_state(player, state);
    else if (state == PLAY)
        /* this supposed to launch the player */
        dbusif_introspect_player(player);
}

static int notify_focus(srs_client_t *srs_client, srs_voice_focus_t focus)
{
    return TRUE;
}

static int notify_command(srs_client_t *srs_client, int ntoken, char **tokens)
{
    return TRUE;
}

static void handle_delayed_request(pa_mainloop_api *api,
                                   pa_time_event *e,
                                   const struct timeval *tv,
                                   void *user_data)
{
    player_t *player = (player_t *)user_data;

    dbusif_query_playlists(player);
    dbusif_set_player_state(player, player->request.state);
    memset(&player->request, 0, sizeof(player->request));
}

static void schedule_delayed_request(player_t *player)
{
    context_t *ctx;
    uint64_t age_of_request;
    srs_plugin_t *pl;
    srs_context_t *srs;
    pa_mainloop_api *api;
    struct timeval tv;
    
    if (!player || !(ctx = player->ctx) ||
        !(pl = ctx->plugin) || !(srs = pl->srs) ||
        !(api = pa_mainloop_get_api(srs->pa)))
        return;

    age_of_request = get_current_time() - player->request.time;

    if (player->request.state && player->request.state != player->state) {
        if (age_of_request < 3000000ULL) {
            gettimeofday(&tv, NULL);
            tv.tv_sec += 2;

            if (player->timer)
                api->time_restart(player->timer, &tv);
            else {
                player->timer = api->time_new(api, &tv, handle_delayed_request,
                                              player);
            }
        }
    }
}

static int player_register(void *key, void *object, void *user_data)
{
    context_t *ctx = (context_t *)user_data;
    player_t *player = (player_t *)object;
    int sts;

    dbusif_register_player(ctx, player->name);

    return MRP_HTBL_ITER_MORE;
}

static void player_free(void *key, void *object)
{
    player_t *player = (player_t *)object;
    context_t *ctx;
    srs_plugin_t *pl;
    srs_context_t *srs;
    pa_mainloop_api *api;
    

    if (strcmp(key, player->name))
        mrp_log_error("mpris2-client: corrupt hashtable (key '%s')", key);
    else {
        if (player->timer && (ctx = player->ctx) && (pl = ctx->plugin) &&
            (srs = pl->srs) && (api = pa_mainloop_get_api(srs->pa)))
        {
            api->time_free(player->timer);
        }

        mrp_free((void *)player->name);
        mrp_free((void *)player->service);
        mrp_free((void *)player->object);
        mrp_free((void *)player);
        playlist_free(player->nlist, player->lists);
    }
}

static playlist_t *playlist_dup(size_t nlist, playlist_t *lists)
{
    playlist_t *dup = mrp_allocz(sizeof(playlist_t) * (nlist + 1));
    playlist_t *src, *dst;
    size_t i;

    for (i = 0;  i < nlist;  i++) {
        src = lists + i;
        dst = dup + i;

        dst->id = mrp_strdup(src->id);
        dst->name = mrp_strdup(src->name);
    }

    return dup;
}

static void playlist_free(size_t nlist, playlist_t *lists)
{
    playlist_t *list;
    size_t i;

    if (lists) {
        for (i = 0;  i < nlist;  i++) {
            list = lists + i;

            free((void *)list->id);
            free((void *)list->name);
        }

        free((void *)lists);
    }
}


static uint64_t get_current_time(void)
{
    struct timeval tv;
    uint64_t now;

    gettimeofday(&tv, NULL);

    now = (uint64_t)tv.tv_sec * 1000000ULL + tv.tv_usec;

    return now;
}


/*
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 *
 */
