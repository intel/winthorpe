/*
 * Copyright (c) 2012, Intel Corporation
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *   * Redistributions of source code must retain the above copyright notice,
 *     this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *   * Neither the name of Intel Corporation nor the names of its contributors
 *     may be used to endorse or promote products derived from this software
 *     without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <murphy/common/debug.h>
#include <murphy/common/mainloop.h>

#include "srs/daemon/plugin.h"
#include "srs/daemon/recognizer.h"

#define FAKE_NAME        "fake-speech"
#define FAKE_DESCRIPTION "A fake/test SRS speech engine to test the infra."
#define FAKE_AUTHORS     "Krisztian Litkey <krisztian.litkey@intel.com>"
#define FAKE_VERSION     "0.0.1"


typedef struct {
    char **tokens;
    int    ntoken;
} fake_candidate_t;


typedef struct {
    srs_plugin_t      *self;             /* fake speech backend plugin */
    srs_srec_notify_t  notify;           /* recognition notification callback */
    void              *notify_data;      /* notification callback data */
    int                active;           /* have been activated */
    fake_candidate_t  *cand;             /* fake candidates to push */
    int                candidx;          /* next candidate index */
    mrp_timer_t       *toktmr;           /* timer for next token */
    char               decoder[256];     /* current decoder */
} fake_t;


static const char *cmd_hal[]  = { "hal", "open", "the", "pod", "bay", "doors" };
static const char *cmd_cant[] = { "I", "am", "afraid", "I", "can't", "do",
                                  "that", "Dave" };
static const char *cmd_music[] = { "hal", "play", "music" };
static const char *cmd_exit[] = { "hal", "exit" };
static const char *cmd_dial[] = { "hal", "dial", "nine", "one", "one" };
static const char *cmd_dial1[] = { "hal", "dial" };
static const char *cmd_artist[] = { "hal", "play", "artist", "foobar" };
static const char *cmd_search1[] = { "search", "for", "google", "glass" };
static const char *cmd_search2[] = { "google", "for", "george", "w", "bush" };
static const char *cmd_search3[] = { "search", "for" };

static fake_candidate_t commands[] = {
    { tokens: (char **)cmd_search3, ntoken: MRP_ARRAY_SIZE(cmd_search3)  },
    { tokens: (char **)cmd_search1, ntoken: MRP_ARRAY_SIZE(cmd_search1)  },
    { tokens: (char **)cmd_search2, ntoken: MRP_ARRAY_SIZE(cmd_search2)  },
    { tokens: (char **)cmd_hal   , ntoken: MRP_ARRAY_SIZE(cmd_hal)    },
    { tokens: (char **)cmd_cant  , ntoken: MRP_ARRAY_SIZE(cmd_cant)   },
    { tokens: (char **)cmd_music , ntoken: MRP_ARRAY_SIZE(cmd_music)  },
    { tokens: (char **)cmd_exit  , ntoken: MRP_ARRAY_SIZE(cmd_exit)   },
    { tokens: (char **)cmd_dial  , ntoken: MRP_ARRAY_SIZE(cmd_dial)   },
    { tokens: (char **)cmd_artist, ntoken: MRP_ARRAY_SIZE(cmd_artist) },
    { tokens: (char **)cmd_dial1 , ntoken: MRP_ARRAY_SIZE(cmd_dial1)  },
    { tokens: (char **)cmd_search1, ntoken: MRP_ARRAY_SIZE(cmd_search1)  },
    { tokens: (char **)cmd_search2, ntoken: MRP_ARRAY_SIZE(cmd_search2)  },
    { tokens: (char **)cmd_search3, ntoken: MRP_ARRAY_SIZE(cmd_search3)  },
    { NULL, 0 }
};


static void push_token_cb(mrp_timer_t *t, void *user_data);


static int arm_token_timer(fake_t *fake, double delay)
{
    srs_context_t *srs   = fake->self->srs;
    unsigned int   msecs = (unsigned int)(1000 * delay);

    mrp_del_timer(fake->toktmr);
    fake->toktmr = mrp_add_timer(srs->ml, msecs, push_token_cb, fake);

    if (fake->toktmr != NULL)
        return TRUE;
    else
        return FALSE;
}


static void push_token_cb(mrp_timer_t *t, void *user_data)
{
    static int            cnt  = 0;
    static struct timeval prev = { 0, 0 };
    struct timeval        now;
    uint32_t              diff;

    fake_t               *fake  = (fake_t *)user_data;
    fake_candidate_t     *fcnd  = fake->cand + fake->candidx++;
    srs_srec_token_t      tokens[fcnd->ntoken];
    srs_srec_candidate_t  cand, *cands[2];
    srs_srec_utterance_t  utt;
    int                   flush, i;

    if (!prev.tv_sec) {
        gettimeofday(&prev, NULL);
        diff = 0;
    }
    else {
        gettimeofday(&now, NULL);

        diff  = (now.tv_sec - prev.tv_sec) * 1000;
        if (now.tv_usec < prev.tv_usec)
            diff -= (prev.tv_usec + now.tv_usec) / 1000;
        else
            diff += (now.tv_usec - prev.tv_usec) / 1000;

        prev = now;
    }

    mrp_debug("counter: %d (diff: %u)", cnt++, diff);

    mrp_del_timer(t);
    fake->toktmr = NULL;

    if (fcnd->tokens == NULL) {
        fake->candidx = 0;
        arm_token_timer(fake, 5);
        return;
    }

    for (i = 0; i < fcnd->ntoken; i++) {
        tokens[i].token = fcnd->tokens[i];
        tokens[i].score = 1;
    }

    cand.score  = 1;
    cand.tokens = &tokens[0];
    cand.ntoken = fcnd->ntoken;

    cands[0] = &cand;
    cands[1] = NULL;

    utt.id    = "fake backend utterance";
    utt.score = 1;
    utt.ncand = 1;
    utt.cands = cands;

 rescan:
    utt.length = cand.ntoken * 2;
    for (i = 0; i < (int)cand.ntoken; i++) {
        cand.tokens[i].start = 2 * i;
        cand.tokens[i].end   = 2 * i + 1;
    }

    flush = fake->notify(&utt, fake->notify_data);

    if (flush != SRS_SREC_FLUSH_ALL) {
        mrp_log_info("Trying to flush fake backend buffer till %d.", flush);
        for (i = 0; i < fcnd->ntoken; i++) {
            if (tokens[i].start == (uint32_t)flush) {
                cand.tokens = tokens + i;
                cand.ntoken = fcnd->ntoken - i;
                utt.length -= flush;
                mrp_log_info("Rescan, removed %d fake backend tokens.", i);
                goto rescan;
            }
        }
    }

    arm_token_timer(fake, 5);
}


static int fake_activate(void *user_data)
{
    fake_t *fake = (fake_t *)user_data;

    if (fake->active)
        return TRUE;

    mrp_debug("activating fake backend");

    fake->cand    = &commands[0];
    fake->candidx = 0;

    if (arm_token_timer(fake, 1)) {
        fake->active = TRUE;
        return TRUE;
    }
    else
        return FALSE;
}


static void fake_deactivate(void *user_data)
{
    fake_t *fake = (fake_t *)user_data;

    if (fake->active) {
        mrp_debug("deactivating fake backend");

        mrp_del_timer(fake->toktmr);
        fake->toktmr = NULL;
        fake->active = FALSE;
    }
}


static int fake_flush(uint32_t start, uint32_t end, void *user_data)
{
    fake_t *fake = (fake_t *)user_data;

    MRP_UNUSED(fake);

    mrp_debug("flushing fake backend buffer (%u - %u)", start, end);

    return TRUE;
}


static int fake_rescan(uint32_t start, uint32_t end, void *user_data)
{
    fake_t *fake = (fake_t *)user_data;

    MRP_UNUSED(fake);

    mrp_debug("scheduling fake backend buffer rescan (%u - %u)", start, end);

    return TRUE;
}


static srs_audiobuf_t *fake_sampledup(uint32_t start, uint32_t end,
                                      void *user_data)
{
    fake_t            *fake = (fake_t *)user_data;
    srs_audioformat_t  format;
    uint32_t           rate;
    uint8_t            channels;
    size_t             samples;
    uint32_t           buf[2];

    MRP_UNUSED(fake);

    mrp_debug("duplicating fake backend sample (%u - %u)", start, end);

    format   = SRS_AUDIO_S32LE;
    rate     = 16000;
    channels = 2;
    samples  = 1;
    buf[0]   = start;
    buf[1]   = end;

    return srs_create_audiobuf(format, rate, channels, samples, buf);
}


static int fake_check_decoder(const char *decoder, void *user_data)
{
    fake_t *fake = (fake_t *)user_data;

    MRP_UNUSED(fake);

    mrp_debug("checking availibilty of decoder '%s' for fake backend", decoder);

    return TRUE;
}


static int fake_select_decoder(const char *decoder, void *user_data)
{
    fake_t *fake = (fake_t *)user_data;
    int     max;

    MRP_UNUSED(fake);

    mrp_debug("selecting decoder '%s' for fake backend", decoder);

    mrp_log_info("switching fake backend to decoder '%s'", decoder);

    max = sizeof(fake->decoder) - 1;
    strncpy(fake->decoder, decoder, max);
    fake->decoder[max] = '\0';

    return TRUE;
}


const char *fake_active_decoder(void *user_data)
{
    fake_t *fake = (fake_t *)user_data;

    return &fake->decoder[0];
}


static int create_fake(srs_plugin_t *plugin)
{
    srs_srec_api_t  fake_api = {
    activate:         fake_activate,
    deactivate:       fake_deactivate,
    flush:            fake_flush,
    rescan:           fake_rescan,
    sampledup:        fake_sampledup,
    check_decoder:    fake_check_decoder,
    select_decoder:   fake_select_decoder,
    active_decoder:   fake_active_decoder,
    };

    srs_context_t *srs = plugin->srs;
    fake_t        *fake;


    mrp_debug("creating fake speech recognition backend");

    fake = mrp_allocz(sizeof(*fake));

    if (fake != NULL) {
        fake->self = plugin;
        strcpy(fake->decoder, "default");

        if (srs_register_srec(srs, FAKE_NAME, &fake_api, fake,
                              &fake->notify, &fake->notify_data) == 0) {
            plugin->plugin_data = fake;
            return TRUE;
        }
        else
            mrp_free(fake);
    }

    return FALSE;
}


static int config_fake(srs_plugin_t *plugin, srs_cfg_t *settings)
{
    srs_cfg_t *cfg;
    int        n, i;

    MRP_UNUSED(plugin);

    mrp_debug("configure fake plugin");

    if ((cfg = settings) != NULL) {
        while (cfg->key != NULL) {
            mrp_debug("got config setting: %s = %s", cfg->key,
                      cfg->value);
            cfg++;
        }
    }

    n = srs_config_collect(settings, "fake.", &cfg);
    mrp_debug("Found %d own configuration keys.", n);
    for (i = 0; i < n; i++)
        mrp_debug("    %s = %s", cfg[i].key, cfg[i].value);
    srs_config_free(cfg);

    return TRUE;
}


static int start_fake(srs_plugin_t *plugin)
{
    MRP_UNUSED(plugin);

    mrp_debug("start fake plugin");

    return TRUE;
}


static void stop_fake(srs_plugin_t *plugin)
{
    MRP_UNUSED(plugin);

    mrp_debug("stop fake plugin");

    return;
}


static void destroy_fake(srs_plugin_t *plugin)
{
    srs_context_t *srs = plugin->srs;
    fake_t        *fake = (fake_t *)plugin->plugin_data;

    mrp_debug("destroy fake plugin");

    if (fake != NULL) {
        srs_unregister_srec(srs, FAKE_NAME);
        mrp_free(fake);
    }
}


SRS_DECLARE_PLUGIN(FAKE_NAME, FAKE_DESCRIPTION, FAKE_AUTHORS, FAKE_VERSION,
                   create_fake, config_fake, start_fake, stop_fake,
                   destroy_fake)
