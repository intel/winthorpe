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

#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>

#include <murphy/common/debug.h>
#include <murphy/common/mainloop.h>

#include "srs/daemon/plugin.h"
#include "srs/daemon/voice.h"

#define FESTIVAL "/usr/bin/festival --tts"
#define PAPLAY   "/usr/bin/paplay"

#define SYNTH_NAME    "simple-voice"
#define SYNTH_DESCR   "A trivial voice/sound feedback plugin for SRS."
#define SYNTH_AUTHORS "Krisztian Litkey <kli@iki.fi>"
#define SYNTH_VERSION "0.0.1"

#define SYNTH_TYPE_SOUND  0x80000000
#define SYNTH_TYPE_MSG    0x40000000
#define SYNTH_TYPE_ACTIVE 0x20000000
#define SYNTH_INDEX(id)   ((int)((id) & 0x70000000))

#define MAX_ARGC 32


/*
 * a loaded file
 */

typedef struct {
    char         *path;                  /* path to sound file */
    uint32_t      id;                    /* sound id */
    unsigned int  cache : 1;             /* cache hint */
} sound_t;


/*
 * an active message or sound
 */

typedef struct {
    uint32_t            id;              /* active id */
    pid_t               pid;             /* rendering child */
    srs_voice_notify_t  notify;          /* completion notification callback */
    void               *user_data;       /* opaque callback data */
} active_t;


/*
 * synthesizer state
 */

typedef struct {
    srs_plugin_t      *self;             /* us */
    sound_t           *sounds;           /* loaded sounds */
    int                nsound;           /* number of sounds */
    char             **tts_argv;         /* TTS command/arguments */
    int                tts_argc;         /* TTS argument count */
    char             **play_argv;        /* sound command/arguments */
    int                play_argc;        /* sound argument count */
    active_t           active;           /* active sound/message */
    mrp_sighandler_t  *sigh;             /* SIGCHLD handler */
} synth_t;



static pid_t fork_command(int *wfd, int argc, char **argv)
{
    pid_t   pid;
    int     pfd[2], null, fd;
    char    dummy;

    if (pipe(pfd) < 0)
        return 0;

    switch ((pid = fork())) {
    case -1:
        pid = 0;
        break;

        /* child */
    case 0:
        null = open("/dev/null", O_RDWR);

        if (null == -1)
            exit(1);

        dup2(pfd[0], 0);
        dup2(null, 1);
        dup2(null, 2);

        close(pfd[1]);
        for (fd = 3; fd < 1024; fd++)
            close(fd);

        if (read(0, &dummy, 1) != 1)
            exit(2);

        argv[argc] = NULL;
        execv(argv[0], argv);
        exit(3);

        /* child */
    default:
        close(pfd[0]);
        *wfd = pfd[1];
        break;
    }

    return pid;
}



static uint32_t synth_load(const char *path, int cache, void *api_data)
{
    synth_t *synth = (synth_t *)api_data;
    sound_t *snd;

    if (mrp_reallocz(synth->sounds, synth->nsound, synth->nsound + 1) != NULL) {
        snd = synth->sounds + synth->nsound;

        snd->path  = mrp_strdup(path);
        snd->cache = cache ? 1 : 0;
        snd->id    = SYNTH_TYPE_SOUND | synth->nsound;

        if (snd->path != NULL) {
            synth->nsound++;

            return snd->id;
        }

        mrp_reallocz(synth->sounds, synth->nsound + 1, synth->nsound);
    }

    return SRS_VOICE_INVALID;
}


static uint32_t synth_play_file(const char *path, srs_voice_notify_t notify,
                                void *user_data, void *api_data)
{
#if 1
    synth_t *synth = (synth_t *)api_data;
    uint32_t id;
    pid_t    pid, wfd;

    if (synth->active.pid) {
        errno = EBUSY;

        return SRS_VOICE_INVALID;
    }

    mrp_log_info("Playing sound file '%s'.", path);

    synth->play_argv[synth->play_argc] = (char *)path;

    pid = fork_command(&wfd, synth->play_argc, synth->play_argv + 1);

    if (pid > 0) {
        write(wfd, "G", 1);

        id = 1 | SYNTH_TYPE_ACTIVE;

        synth->active.pid = pid;
        synth->active.notify    = notify;
        synth->active.user_data = user_data;
        synth->active.id        = id;

        close(wfd);
    }
    else
        id = SRS_VOICE_INVALID;

#else
    uint32_t id = 0;
    char cmd[1024];

    snprintf(cmd, sizeof(cmd), "paplay %s", path);
    system(cmd);
#endif

    return id;
}


static uint32_t synth_play(uint32_t id, srs_voice_notify_t notify,
                           void *user_data, void *api_data)
{
    synth_t *synth = (synth_t *)api_data;
    int      idx   = SYNTH_INDEX(id);
    sound_t *snd;

    if (idx < synth->nsound) {
        snd = synth->sounds + idx;

        return synth_play_file(snd->path, notify, user_data, api_data);
    }
    else
        return SRS_VOICE_INVALID;
}


static uint32_t synth_tts(const char *msg, srs_voice_notify_t notify,
                          void *user_data, void *api_data)
{
#if 1
    synth_t *synth = (synth_t *)api_data;
    uint32_t id;
    pid_t    pid, wfd;

    if (synth->active.pid) {
        errno = EBUSY;

        return SRS_VOICE_INVALID;
    }

    mrp_log_info("Synthesizing message '%s'.", msg);

    pid = fork_command(&wfd, synth->tts_argc - 1, synth->tts_argv + 1);

    if (pid > 0) {
        write(wfd, "G", 1);

        id = 1 | SYNTH_TYPE_ACTIVE;

        synth->active.pid = pid;
        synth->active.notify    = notify;
        synth->active.user_data = user_data;
        synth->active.id        = id;

        dprintf(wfd, "%s\n", msg);

        close(wfd);
    }
    else
        id = SRS_VOICE_INVALID;

#else
    uint32_t id = 0;

    char cmd[1024];
    snprintf(cmd, sizeof(cmd), "echo \"%s\" | /usr/bin/festival --tts", msg);
    system(cmd);
#endif

    return id;
}


static void synth_cancel(uint32_t id, int notify, void *api_data)
{
    MRP_UNUSED(id);
    MRP_UNUSED(notify);
    MRP_UNUSED(api_data);
}


static void sighandler(mrp_sighandler_t *h, int signum, void *user_data)
{
    synth_t            *synth = (synth_t *)user_data;
    pid_t               pid;
    uint32_t            id;
    srs_voice_notify_t  cb;
    void               *data;
    int                 status, sts;

    MRP_UNUSED(h);

    if (signum != SIGCHLD)
        return;

    mrp_log_info("Received SIGCHLD signal.");

    if (synth->active.pid != 0) {
        pid = synth->active.pid;

        if ((sts = waitpid(pid, &status, WNOHANG)) == pid) {
            mrp_log_info("Active child (pid %u) exited with status %d.",
                         synth->active.pid,
                         WIFEXITED(status) ? WEXITSTATUS(status) : -1);

            id   = synth->active.id;
            cb   = synth->active.notify;
            data = synth->active.user_data;

            synth->active.pid       = 0;
            synth->active.notify    = NULL;
            synth->active.user_data = NULL;

            if (cb != NULL)
                cb(id, data);
        }
        else {
            if (sts != 0)
                mrp_log_error("waitpid(%u) failed (%d: %s)",
                              synth->active.pid, errno, strerror(errno));
        }
    }
}


static int create_synth(srs_plugin_t *plugin)
{
    srs_context_t  *srs = plugin->srs;
    mrp_mainloop_t *ml  = srs->ml;
    synth_t        *synth;

    mrp_debug("creating simple voice plugin");

    synth = mrp_allocz(sizeof(*synth));

    if (synth != NULL) {
        synth->sigh = mrp_add_sighandler(ml, SIGCHLD, sighandler, synth);

        if (synth->sigh != NULL) {
            plugin->plugin_data = synth;

            return TRUE;
        }

        mrp_free(synth);
    }

    return FALSE;
}


static int config_synth(srs_plugin_t *plugin, srs_cfg_t *settings)
{
    static char  ttscmd[1024], playcmd[1024];
    static char *ttsargs[MAX_ARGC + 2] , *playargs[MAX_ARGC + 2];

    synth_t    *synth = (synth_t *)plugin->plugin_data;
    const char *tts, *play, *s;
    char       *d, **v, *arg;
    int         l, a;

    mrp_debug("configure simple voice plugin");

    tts  = srs_config_get_string(settings, "voice.say" , FESTIVAL);
    play = srs_config_get_string(settings, "voice.play", PAPLAY);

    mrp_log_info("voice plugin TTS play command: '%s'", tts);
    mrp_log_info("voice plugin sound command: '%s'"   , play);

    s = tts;
    d = ttscmd;
    l = sizeof(ttscmd) - 1;
    a = 0;
    v = ttsargs + 1;

 parse:
    while (*s && l > 0 && a < MAX_ARGC) {
        while (*s && (*s == ' ' || *s == '\t'))
            s++;

        arg = d;
        while (*s && *s != ' ' && *s != '\t' && l > 0) {
            *d++ = *s++;
            l--;
        }

        if (l == 0) {
            mrp_log_error("Invalid command line give, too long.");
            return FALSE;
        }

        *d++ = '\0';
        l--;

        v[a++] = arg;
    }

    if (a >= MAX_ARGC)
        return FALSE;

    if (ttscmd <= d && d <= ttscmd + sizeof(ttscmd)) {
        ttsargs[0] = ttsargs[1];

        synth->tts_argv = ttsargs;
        synth->tts_argc = a + 1;

        s = play;
        d = playcmd;
        l = sizeof(playcmd) - 1;
        a = 0;
        v = playargs + 1;

        goto parse;
    }
    else {
        playargs[0] = playargs[1];

        synth->play_argv    = playargs;
        synth->play_argc    = a + 1;
    }

    return TRUE;
}


static int start_synth(srs_plugin_t *plugin)
{
    static srs_voice_api_t api = {
    load:      synth_load,
    play:      synth_play,
    play_file: synth_play_file,
    say:       synth_tts,
    cancel:    synth_cancel
    };
    synth_t       *synth = (synth_t *)plugin->plugin_data;
    srs_context_t *srs   = plugin->srs;

    mrp_debug("start simple-voice plugin");

    if (synth != NULL) {
        if (srs_register_voice(srs, SYNTH_NAME, &api, synth) == 0)
            return TRUE;
        else
            mrp_free(synth);
    }

    return FALSE;
}


static void stop_synth(srs_plugin_t *plugin)
{
    srs_context_t *srs = plugin->srs;
    synth_t       *synth = (synth_t *)plugin->plugin_data;

    mrp_debug("stop simple-voice plugin");

    srs_unregister_voice(srs, SYNTH_NAME);
    mrp_del_sighandler(synth->sigh);
}


static void destroy_synth(srs_plugin_t *plugin)
{
    synth_t *synth = (synth_t *)plugin->plugin_data;

    mrp_debug("destroy simple-voice plugin");

    mrp_free(synth);
}


SRS_DECLARE_PLUGIN(SYNTH_NAME, SYNTH_DESCR, SYNTH_AUTHORS, SYNTH_VERSION,
                   create_synth, config_synth, start_synth, stop_synth,
                   destroy_synth)
