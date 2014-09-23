/*
 * Copyright (c) 2012-2014, Intel Corporation
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

#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>

#include <murphy/common/debug.h>
#include <murphy/common/log.h>
#include <murphy/common/mm.h>
#include <murphy/common/mainloop.h>

#include <espeak/speak_lib.h>

#include "srs/daemon/plugin.h"
#include "srs/daemon/voice.h"
#include "srs/daemon/iso-6391.h"
#include "srs/daemon/pulse.h"

#include "espeak-voice.h"

#define PLUGIN_NAME    "espeak-voice"
#define PLUGIN_DESCR   "An espeak-based voice synthesizer plugin for SRS."
#define PLUGIN_AUTHORS "Krisztian Litkey <kli@iki.fi>"
#define PLUGIN_VERSION "0.0.1"

#define CONFIG_VOICEDIR "espeak.voicedir"

#define ESPEAK_CONTINUE 0
#define ESPEAK_ABORT    1

typedef struct {
    void *samples;
    int   nsample;
} synth_data_t;


static void stream_event_cb(srs_pulse_t *p, srs_voice_event_t *event,
                            void *user_data)
{
    espeak_t *e = (espeak_t *)user_data;

    MRP_UNUSED(p);

    e->voice.notify(event, e->voice.notify_data);
}


static int espeak_synth_cb(short *samples, int nsample, espeak_EVENT *events)
{
    synth_data_t *data = events->user_data;

    if (samples == NULL)
        return ESPEAK_CONTINUE;

    if (mrp_realloc(data->samples, 2 * (data->nsample + nsample)) == NULL)
        return ESPEAK_ABORT;

    memcpy(data->samples + 2 * data->nsample, samples, 2 * nsample);
    data->nsample += nsample;

    return ESPEAK_CONTINUE;
}


static int espeak_setrate(double drate)
{
    int min, max, step, rate, orig;

    if (0.0 < drate && drate <= 2.0) {
        if (drate == 1.0)
            rate = espeakRATE_NORMAL;
        else if (drate < 1.0) {
            min  = espeakRATE_MINIMUM;
            max  = espeakRATE_NORMAL;
            step = (max - min) / 1.0;
            rate = (int)(min + drate * step);
        }
        else { /*drate > 1.0*/
            min  = espeakRATE_NORMAL;
            max  = espeakRATE_MAXIMUM;
            step = (max - min) / 1.0;
            rate = (int)(min + (drate - 1.0) * step);
        }

        orig = espeak_GetParameter(espeakRATE, 1);
        espeak_SetParameter(espeakRATE, rate, 0);

        return orig;
    }

    return 0;
}


static int espeak_setpitch(double dpitch)
{
    int pitch, orig;

    if (0.0 < dpitch && dpitch <= 2.0) {
        pitch = (int)(50 * dpitch);
        orig = espeak_GetParameter(espeakPITCH, 1);
        espeak_SetParameter(espeakPITCH, pitch, 0);

        return orig;
    }

    return 0;
}


static uint32_t espeak_render(const char *msg, char **tags, int actor,
                              double rate, double pitch, int notify_events,
                              void *api_data)
{
    espeak_t     *e = (espeak_t *)api_data;
    int           size, start, end, type, orate, opitch;
    unsigned int  flags, uid;
    synth_data_t  data;
    uint32_t      id;
    int           r;

    MRP_UNUSED(rate);
    MRP_UNUSED(pitch);

    if (0 <= actor && actor <= e->nactor) {
        if (espeak_SetVoiceByName(e->actors[actor].name) != EE_OK) {
            mrp_log_error("espeak: failed to activate espeak voice #%d ('%s').",
                          actor, e->actors[actor].name);
            return SRS_VOICE_INVALID;
        }
    }
    else {
        mrp_log_error("espeak: invalid espeak voice #%d requested.", actor);
        return SRS_VOICE_INVALID;
    }

    size  = 0;
    type  = POS_CHARACTER;
    start = 0;
    end   = 0;
    flags = espeakCHARS_UTF8;
    uid   = 0;
    data  = (synth_data_t) { NULL, 0 };

    orate  = espeak_setrate(rate);
    opitch = espeak_setpitch(pitch);

    r = espeak_Synth(msg, size, start, type, end, flags, &uid, &data);

    espeak_setrate(orate);
    espeak_setpitch(opitch);

    if (r != EE_OK || data.samples == NULL) {
        mrp_log_error("espeak: failed to synthesize message with espeak.");
        return SRS_VOICE_INVALID;
    }

    id = srs_play_stream(e->srs->pulse, data.samples, e->config.rate, 1,
                         data.nsample, tags, notify_events,
                         stream_event_cb, e);

    if (id == SRS_VOICE_INVALID)
        mrp_free(data.samples);

    return id;
}


static void espeak_cancel(uint32_t id, void *api_data)
{
    espeak_t *e = (espeak_t *)api_data;

    srs_stop_stream(e->srs->pulse, id, FALSE, FALSE);
}


static int create_espeak(srs_plugin_t *plugin)
{
    espeak_t *e;

    mrp_debug("creating espeak voice plugin");

    e = mrp_allocz(sizeof(*e));

    if (e != NULL) {
        e->self = plugin;
        e->srs  = plugin->srs;

        plugin->plugin_data = e;

        return TRUE;
    }
    else
        return FALSE;
}


static int config_espeak(srs_plugin_t *plugin, srs_cfg_t *cfg)
{
    espeak_t      *e = (espeak_t *)plugin->plugin_data;
    const char    *path;
    int            out, blen, rate;

    mrp_debug("configure espeak voice plugin");

    e->config.voicedir = srs_config_get_string(cfg, CONFIG_VOICEDIR, NULL);

    out  = AUDIO_OUTPUT_SYNCHRONOUS;
    path = e->config.voicedir;
    blen = 1000;

    rate = espeak_Initialize(out, blen, path, 0);

    if (rate <= 0) {
        mrp_log_error("espeak: failed to initialize espeak.");
        return FALSE;
    }

    mrp_log_info("espeak: chose %d Hz for sample rate.", rate);

    e->config.rate = rate;

    espeak_SetSynthCallback(espeak_synth_cb);
    /*espeak_SetParameter(espeakRATE, espeakRATE_NORMAL, 0);
      espeak_SetParameter(espeakPITCH, 50, 0);*/

    return TRUE;
}


static const char *espeak_parse_dialect(const char *lang, const char **dialect)
{
    const char *dial, *l, *c, *d;
    char        code_buf[8];
    size_t      n;

    c = lang;
    d = strchr(lang, '-');

    if (d == NULL || d - c > 3)
        d = NULL;
    else {
        n = d - c;
        strncpy(code_buf, c, n);
        code_buf[n] = '\0';
        c = code_buf;
        d = d + 1;
    }

    mrp_debug("parsed '%s' into code '%s', dialect '%s'", lang, c, d ? d : "-");

    l = srs_iso6391_language(c);

    if (l == NULL)
        dial = NULL;
    else {
        lang = l;

        if (d != NULL && strcmp(c, d))
            dial = srs_iso6391_dialect(d);
        else
            dial = NULL;
    }

    *dialect = dial;
    return lang;
}


static inline int espeak_gender(int gender)
{
    switch (gender) {
    case 1:  return SRS_VOICE_GENDER_MALE;
    case 2:  return SRS_VOICE_GENDER_FEMALE;
    default: return SRS_VOICE_GENDER_MALE;
    }
}


static inline char *espeak_description(espeak_VOICE *v)
{
    static char descr[256];

    snprintf(descr, sizeof(descr), "espeak %s voice (%s).", v->languages,
             v->identifier ? v->identifier : "-");

    return descr;
}


static int start_espeak(srs_plugin_t *plugin)
{
    static srs_voice_api_t api = {
        .render = espeak_render,
        .cancel = espeak_cancel
    };

    espeak_t      *e = (espeak_t *)plugin->plugin_data;
    espeak_VOICE **voices, *v;
    int            nvoice, i;
    const char    *lang, *language, *dialect;

    if (e->srs->pulse == NULL)
        return FALSE;

    voices = (espeak_VOICE **)espeak_ListVoices(NULL);

    if (voices == NULL) {
        mrp_log_error("espeak: could not find any voices.");
        return FALSE;
    }

    for (nvoice = 0; voices[nvoice] != NULL; nvoice++)
        ;

    if ((e->actors = mrp_allocz_array(typeof(*e->actors), nvoice)) == NULL)
        goto fail;

    mrp_log_info("espeak: found %d available voices.", nvoice);

    for (i = 0; i < nvoice; i++) {
        v = voices[i];

        mrp_log_info("    %s (%s)", v->name, v->identifier);

        for (lang = v->languages + 1; *lang; lang += strlen(lang)) {
            mrp_log_info("      %s (priority %d)", lang, lang[-1]);

            language = espeak_parse_dialect(lang, &dialect);
            e->actors[i].id          = i;
            e->actors[i].name        = mrp_strdup(v->name);
            e->actors[i].lang        = mrp_strdup(language);
            e->actors[i].dialect     = mrp_strdup(dialect);
            e->actors[i].gender      = espeak_gender(v->gender);
            e->actors[i].description = mrp_strdup(espeak_description(v));

            if (e->actors[i].name == NULL || e->actors[i].lang == NULL)
                goto fail;

            e->nactor++;
        }
    }

    if (srs_register_voice(e->self->srs, "espeak", &api, e,
                           e->actors, e->nactor,
                           &e->voice.notify, &e->voice.notify_data) == 0)
        return TRUE;

 fail:
    for (i = 0; i < e->nactor; i++) {
        mrp_free(e->actors[i].name);
        mrp_free(e->actors[i].lang);
        mrp_free(e->actors[i].description);
    }

    return FALSE;
}


static void stop_espeak(srs_plugin_t *plugin)
{
    MRP_UNUSED(plugin);

    return;
}


static void destroy_espeak(srs_plugin_t *plugin)
{
    espeak_t *e = (espeak_t *)plugin->plugin_data;
    int       i;

    srs_unregister_voice(e->self->srs, "espeak");
    espeak_Terminate();

    for (i = 0; i < e->nactor; i++) {
        mrp_free(e->actors[i].name);
        mrp_free(e->actors[i].lang);
        mrp_free(e->actors[i].description);
    }

    mrp_free(e);
}


SRS_DECLARE_PLUGIN(PLUGIN_NAME, PLUGIN_DESCR, PLUGIN_AUTHORS, PLUGIN_VERSION,
                   create_espeak, config_espeak, start_espeak, stop_espeak,
                   destroy_espeak)
