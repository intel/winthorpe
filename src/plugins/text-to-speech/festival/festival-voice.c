/*
 * Copyright (c) 2012, 2013, Intel Corporation
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
#include "srs/daemon/voice.h"

#include "festival-voice.h"
#include "libcarnival.h"
#include "pulse.h"

#define PLUGIN_NAME    "festival-voice"
#define PLUGIN_DESCR   "A festival-based voice synthesizer plugin for SRS."
#define PLUGIN_AUTHORS "Krisztian Litkey <kli@iki.fi>"
#define PLUGIN_VERSION "0.0.1"

#define DEFVOICE       ""
#define AUTOLOAD       "auto"
#define CONFIG_VOICES  "festival.voices"
#define DEFVAL_VOICES  DEFVOICE


static void stream_event_cb(festival_t *f, srs_voice_event_t *event,
                            void *user_data)
{
    MRP_UNUSED(user_data);

    f->voice.notify(event, f->voice.notify_data);
}


static uint32_t festival_render(const char *msg, char **tags, int actor,
                                double rate, double pitch, int notify_events,
                                void *api_data)
{
    festival_t *f = (festival_t *)api_data;
    void       *samples;
    uint32_t    nsample, id;
    int         srate, nchannel;

    MRP_UNUSED(rate);
    MRP_UNUSED(pitch);

    if (0 <= actor && actor < f->nactor)
        carnival_select_voice(f->actors[actor].name);
    else
        return SRS_VOICE_INVALID;

    if (carnival_synthesize(msg, &samples, &srate, &nchannel, &nsample) != 0)
        return SRS_VOICE_INVALID;
    else
        id = pulse_play_stream(f, samples, srate, nchannel, nsample, tags,
                               notify_events, stream_event_cb, NULL);

    if (id == SRS_VOICE_INVALID)
        mrp_free(samples);

    return id;
}


static void festival_cancel(uint32_t id, void *api_data)
{
    festival_t *f = (festival_t *)api_data;

    pulse_stop_stream(f, id, FALSE, FALSE);
}


static int create_festival(srs_plugin_t *plugin)
{
    festival_t *f;

    mrp_debug("creating festival voice plugin");

    f = mrp_allocz(sizeof(*f));

    if (f != NULL) {
        f->self = plugin;
        f->srs  = plugin->srs;

        plugin->plugin_data = f;

        return TRUE;
    }
    else
        return FALSE;
}


static int config_festival(srs_plugin_t *plugin, srs_cfg_t *cfg)
{
    festival_t  *f = (festival_t *)plugin->plugin_data;
    const char  *b, *e;
    char         voice[256], **voices;
    int          nvoice, len, i;

    mrp_debug("configure festival voice plugin");

    if (carnival_init() != 0) {
        mrp_log_error("Failed to initalize festival library.");

        return FALSE;
    }

    f->config.voices = srs_get_string_config(cfg, CONFIG_VOICES, DEFVAL_VOICES);

    if (!strcmp(f->config.voices, AUTOLOAD)) {
        if (carnival_available_voices(&voices, &nvoice) == 0) {
            for (i = 0; i < nvoice; i++) {
                if (carnival_load_voice(voices[i]) == 0)
                    mrp_log_info("Loaded festival voice '%s'.", voices[i]);
                else {
                    mrp_log_info("Failed to load festival voice '%s'.",
                                 voices[i]);
                    return FALSE;
                }
            }
        }
    }
    else if (f->config.voices[0]) {
        b = f->config.voices;
        while (b && *b) {
            e = strchr(b, ',');

            if (e != NULL) {
                len = e - b;

                if (len >= sizeof(voice) - 1) {
                toolong:
                    mrp_log_error("Voice name '%*.*s' too long.", len, len, b);
                    return FALSE;
                }

                strncpy(voice, b, len);
                voice[len] = '\0';
            }
            else {
                if ((len = strlen(b)) >= sizeof(voice) - 1)
                    goto toolong;

                strcpy(voice, b);
            }

            if (carnival_load_voice(voice) == 0)
                mrp_log_info("Loaded festival voice '%s'.", voice);
            else
                mrp_log_error("Failed to load festival voice '%s'.", voice);

            while (e != NULL && (*e == ',' || *e == ' '))
                e++;

            b = e ? e : NULL;
        }
    }

    if (carnival_available_voices(&voices, &nvoice) == 0) {
        mrp_log_info("Available festival voices:");

        for (i = 0; i < nvoice; i++)
            mrp_log_info("    %s", voices[i]);

        carnival_free_strings(voices, nvoice);
    }

    if (carnival_loaded_voices(&voices, &nvoice) == 0) {
        char *lang, *dial, *descr;
        int   female;

        mrp_log_info("Loaded festival voices:");

        for (i = 0; i < nvoice; i++) {
            if (carnival_query_voice(voices[i],
                                     &lang, &female, &dial, &descr) == 0) {
                mrp_log_info("    %s (%smale %s%s%s)", voices[i],
                             female ? "fe" : "",
                             dial ? dial : "", dial ? " " : "", lang);
                mrp_log_info("        %s", descr);

                carnival_free_string(lang);
                carnival_free_string(dial);
                carnival_free_string(descr);
            }
            else
                mrp_log_error("Failed to query festival language '%s'.",
                              voices[i]);
        }

        carnival_free_strings(voices, nvoice);
    }

    return TRUE;
}


static int start_festival(srs_plugin_t *plugin)
{
    static srs_voice_api_t api = {
        .render = festival_render,
        .cancel = festival_cancel
    };

    festival_t         *f = (festival_t *)plugin->plugin_data;
    int                 nactor;
    char              **voices;
    int                 nvoice;
    char               *lang, *dial, *descr;
    int                 female, age, i;

    if (pulse_setup(f) != 0)
        return FALSE;

    if (carnival_loaded_voices(&voices, &nvoice) != 0)
        goto fail;

    if (nvoice == 0)
        return TRUE;

    if ((f->actors = mrp_allocz_array(typeof(*f->actors), nvoice)) == NULL)
        goto fail;

    for (i = 0; i < nvoice; i++) {
        if (carnival_query_voice(voices[i], &lang, &female, &dial, &descr) != 0)
            goto fail;

        f->actors[i].id          = i;
        f->actors[i].name        = mrp_strdup(voices[i]);
        f->actors[i].lang        = lang;
        f->actors[i].dialect     = dial;
        f->actors[i].gender      = SRS_VOICE_GENDER_MALE + !!female;
        f->actors[i].description = descr;

        if (f->actors[i].name == NULL)
            goto fail;

        f->nactor++;
    }

    carnival_free_strings(voices, nvoice);

    if (srs_register_voice(f->self->srs, "festival", &api, f,
                           f->actors, f->nactor,
                           &f->voice.notify, &f->voice.notify_data) == 0)
        return TRUE;

 fail:
    carnival_free_strings(voices, nvoice);

    for (i = 0; i < f->nactor; i++) {
        mrp_free(f->actors[i].name);
        carnival_free_string(f->actors[i].lang);
        carnival_free_string(f->actors[i].dialect);
        carnival_free_string(f->actors[i].description);
    }

    return FALSE;
}


static void stop_festival(srs_plugin_t *plugin)
{
    return;
}


static void destroy_festival(srs_plugin_t *plugin)
{
    festival_t *f = (festival_t *)plugin->plugin_data;
    int         i;

    srs_unregister_voice(f->self->srs, "festival");

    for (i = 0; i < f->nactor; i++) {
        mrp_free(f->actors[i].name);
        carnival_free_string(f->actors[i].lang);
        carnival_free_string(f->actors[i].dialect);
        carnival_free_string(f->actors[i].description);
    }

    pulse_cleanup(f);
    carnival_exit();

    mrp_free(f);
}


SRS_DECLARE_PLUGIN(PLUGIN_NAME, PLUGIN_DESCR, PLUGIN_AUTHORS, PLUGIN_VERSION,
                   create_festival, config_festival,
                   start_festival, stop_festival, destroy_festival)
