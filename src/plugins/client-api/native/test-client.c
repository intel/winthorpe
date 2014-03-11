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

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <getopt.h>
#include <stdarg.h>

#include <murphy/common/macros.h>
#include <murphy/common/mm.h>
#include <murphy/common/debug.h>
#include <murphy/common/mainloop.h>
#include <murphy/common/pulse-glue.h>
#include <murphy/common/glib-glue.h>

#include <breedline/breedline-murphy.h>

#include "native-client.h"

static const char *default_commands[] = {
    "hal open the pod bay doors",
    "hal play music",
    "hal stop music",
    "hal exit",
};

typedef struct {
    GMainLoop      *gml;
    pa_mainloop    *pa;
    mrp_mainloop_t *ml;
    brl_t          *brl;
    srs_t          *srs;
    const char     *app_class;
    const char     *app_name;
    int             exit_status;
    int             server_up : 1;
    int             registered : 1;
    const char     *focus;
    char          **commands;
    int             ncommand;
    int             autoregister : 1;
    const char     *autofocus;
    uint32_t        vreq;
    int             glib : 1;
} client_t;


static void connect_notify(srs_t *srs, int status, const char *msg,
                           void *user_data);

static void command_notify(srs_t *srs, int idx, char **tokens, int ntoken,
                           void *user_data);

static void focus_notify(srs_t *srs, srs_voice_focus_t focus, void *user_data);

static void render_notify(srs_t *srs, srs_voice_event_t *e, void *user_data,
                          void *notify_data);


static void execute_user_command(client_t *c, int narg, char **args);

static void set_prompt(client_t *c, const char *prompt)
{
    brl_set_prompt(c->brl, prompt);
}


static void show_prompt(client_t *c)
{
    if (c->brl != NULL)
        brl_show_prompt(c->brl);
}


static void hide_prompt(client_t *c)
{
    if (c->brl != NULL)
        brl_hide_prompt(c->brl);
}


static void print(client_t *c, const char *format, ...)
{
    va_list ap;

    hide_prompt(c);

    va_start(ap, format);
    vfprintf(stdout, format, ap);
    fputc('\n', stdout);
    fflush(stdout);
    va_end(ap);

    show_prompt(c);
}


static char *concat_tokens(char *buf, int size, int ntoken, char **tokens)
{
    char   *p, *t;
    int     l, n;

    p = buf;
    t = "";
    l = size - 1;

    while (ntoken > 0) {
        n  = snprintf(p, l, "%s%s", t, tokens[0]);

        if (n >= l)
            return NULL;

        p += n;
        l -= n;
        t = " ";
        ntoken--;
        tokens++;
    }

    buf[size - 1] = '\0';
    return buf;
}


static void add_command(client_t *c, int ntoken, char **tokens)
{
    char   command[1024];
    size_t osize, nsize;

    if (c->registered) {
        print(c, "You need to unregister first to modify commands.");
        return;
    }

    if (concat_tokens(command, sizeof(command), ntoken, tokens) == NULL) {
        print(c, "Command too long.");
        return;
    }

    osize = sizeof(*c->commands) *  c->ncommand;
    nsize = sizeof(*c->commands) * (c->ncommand + 1);

    if (!mrp_reallocz(c->commands, osize, nsize)) {
        print(c, "Failed to add new command.");
        return;
    }

    c->commands[c->ncommand] = mrp_strdup(command);

    if (c->commands[c->ncommand] != NULL) {
        c->ncommand++;
        print(c, "Command '%s' added to command set.", command);
    }
    else
        print(c, "Failed to register new command.");
}


static void del_command(client_t *c, int ntoken, char **tokens)
{
    char command[1024];
    int  i;

    if (c->registered) {
        print(c, "You need to unregister first to modify commands.");
        return;
    }

    if (concat_tokens(command, sizeof(command), ntoken, tokens) == NULL) {
        print(c, "Command too long.");
        return;
    }

    for (i = 0; i < c->ncommand; i++) {
        if (!strcmp(c->commands[i], command)) {
            if (i < c->ncommand - 1)
                memmove(c->commands + i + 1, c->commands + i,
                        (c->ncommand - 1 - i) * sizeof(*c->commands));

            c->ncommand--;
            mrp_realloc(c->commands, sizeof(*c->commands) * c->ncommand);

            print(c, "Command '%s' deleted.", command);
        }
    }
}


static void reset_commands(client_t *c)
{
    int i;

    if (c->registered){
        print(c, "You need to unregister first to modify commands.");
        return;
    }

    for (i = 0; i < c->ncommand; i++)
        mrp_free(c->commands[i]);
    mrp_free(c->commands);

    c->commands = NULL;
    c->ncommand = 0;

    print(c, "Commands resetted, no current commands.");
}


static void list_commands(client_t *c)
{
    int i;

    if (c->ncommand > 0) {
        print(c, "Command set:");
        for (i = 0; i < c->ncommand; i++)
            print(c, "  %s", c->commands[i]);
    }
    else
        print(c, "No commands defined.");
}


static void set_client_defaults(client_t *c, const char *argv0)
{
    int i;

    c->app_class    = "player";
    c->app_name     = strrchr(argv0, '/');

    if (c->app_name != NULL)
        c->app_name++;
    else
        c->app_name = argv0;

    c->commands = mrp_allocz(sizeof(default_commands));
    c->ncommand = MRP_ARRAY_SIZE(default_commands);

    for (i = 0; i < c->ncommand; i++) {
        c->commands[i] = mrp_strdup(default_commands[i]);
        if (c->commands[i] == NULL) {
            print(c, "Failed to initialize default command set.");
            exit(1);
        }
    }
}


static void destroy_client(client_t *c)
{
    if (c != NULL) {
        mrp_debug("destroying client");

        if (c->ml != NULL)
            mrp_mainloop_destroy(c->ml);

        if (c->pa != NULL)
            pa_mainloop_free(c->pa);

        if (c->gml != NULL)
            g_main_loop_unref(c->gml);

        mrp_free(c);
    }
}


static client_t *create_client(const char *argv0)
{
    client_t *c = mrp_allocz(sizeof(*c));

    if (c != NULL)
        set_client_defaults(c, argv0);

    return c;
}


static int create_mainloop(client_t *c)
{
    if (!c->glib) {
        c->pa = pa_mainloop_new();
        c->ml = mrp_mainloop_pulse_get(pa_mainloop_get_api(c->pa));
    }
    else {
        c->gml = g_main_loop_new(NULL, FALSE);
        c->ml  = mrp_mainloop_glib_get(c->gml);
    }

    if (c->ml != NULL)
        return TRUE;
    else
        return FALSE;
}


static void run_mainloop(client_t *c)
{
    if (c != NULL && c->pa != NULL)
        pa_mainloop_run(c->pa, &c->exit_status);
    else
        g_main_loop_run(c->gml);
}


static void quit_mainloop(client_t *c, int exit_status)
{
    if (c != NULL) {
        if (c->pa != NULL)
            pa_mainloop_quit(c->pa, exit_status);
        else
            g_main_loop_quit(c->gml);
    }
    else
        exit(exit_status);
}


static void sighandler(mrp_sighandler_t *h, int signum, void *user_data)
{
    client_t *c = (client_t *)user_data;

    MRP_UNUSED(h);

    switch (signum) {
    case SIGINT:
        printf("Received SIGINT, exiting...");
        quit_mainloop(c, 0);
        break;

    case SIGTERM:
        printf("Received SIGTERM, exiting...");
        quit_mainloop(c, 0);
        break;
    }
}


static void setup_signals(client_t *c)
{
    mrp_add_sighandler(c->ml, SIGINT , sighandler, c);
    mrp_add_sighandler(c->ml, SIGTERM, sighandler, c);
}




static int split_input(char *input, int narg, char **args)
{
    int   n;
    char *p;

    n = 0;
    p = input;

    while (*p) {
        while (*p == ' ' || *p == '\t')
            p++;

        args[n++] = p;

        if (n >= narg) {
            errno = EOVERFLOW;
            return -1;
        }

        while (*p && *p != ' ' && *p != '\t')
            p++;

        if (*p)
            *p++ = '\0';
    }

    return n;
}


static void process_input(brl_t *brl, const char *input, void *user_data)
{
    client_t *c   = (client_t *)user_data;
    int       len = input ? strlen(input) + 1 : 0;
    char      buf[len], *args[64];
    int       narg;

    if (len > 1) {
        brl_add_history(brl, input);
        hide_prompt(c);

        strcpy(buf, input);
        narg = split_input(buf, MRP_ARRAY_SIZE(args), args);
        if (narg > 0)
            execute_user_command(c, narg, &args[0]);
        else
            printf("failed to parse input '%s'\n", input);

        show_prompt(c);
    }
}


static void setup_input(client_t *c)
{
    int fd;

    fd     = fileno(stdin);
    c->brl = brl_create_with_murphy(fd, "disconnected", c->ml,
                                    process_input, c);

    if (c->brl != NULL)
        brl_show_prompt(c->brl);
    else {
        fprintf(stderr, "Failed to initialize breedline for console input.");
        exit(1);
    }
}


static void cleanup_input(client_t *c)
{
    if (c != NULL && c->brl != NULL) {
        brl_destroy(c->brl);
        c->brl = NULL;
    }
}


static void print_usage(const char *argv0, int exit_code, const char *fmt, ...)
{
    va_list     ap;
    const char *exe;

    if (fmt && *fmt) {
        va_start(ap, fmt);
        vprintf(fmt, ap);
        va_end(ap);
    }

    exe = strrchr(argv0, '/');

    printf("usage: %s [options]\n\n"
           "The possible options are:\n"
           "  -N, --name=APPNAME             application name to use\n"
           "  -C, --class=APPCLASS           application class to use\n"
           "  -v, --verbose                  increase logging verbosity\n"
           "  -d, --debug                    enable debug messages\n"
           "  -R, --register                 automatically register to server\n"
           "  -F, --focus[=TYPE]             automatically request focus\n"
           "  -h, --help                     show help on usage\n", exe);
    printf("\n");

    if (exit_code < 0)
        return;
    else
        exit(exit_code);
}


static void parse_cmdline(client_t *c, int argc, char **argv)
{
#   define OPTIONS "N:C:d:RFgh"
    struct option options[] = {
        { "name"      , required_argument, NULL, 'N' },
        { "class"     , required_argument, NULL, 'C' },
        { "debug"     , required_argument, NULL, 'd' },
        { "register"  , no_argument      , NULL, 'R' },
        { "focus"     , optional_argument, NULL, 'F' },
        { "glib"      , no_argument      , NULL, 'g' },
        { "help"      , no_argument      , NULL, 'h' },
        { NULL, 0, NULL, 0 }
    };

    int opt;

    while ((opt = getopt_long(argc, argv, OPTIONS, options, NULL)) != -1) {
        switch (opt) {
        case 'N':
            c->app_name = optarg;
            break;

        case 'C':
            c->app_class = optarg;
            break;

        case 'd':
            mrp_debug_set_config(optarg);
            mrp_debug_enable(TRUE);
            break;

        case 'R':
            c->autoregister = TRUE;
            break;

        case 'F':
            c->autofocus = optarg ? optarg : "shared";
            break;

        case 'g':
            c->glib = TRUE;
            break;

        case 'h':
            print_usage(argv[0], -1, "");
            exit(0);
            break;

        default:
            print_usage(argv[0], EINVAL, "invalid option '%c'", opt);
        }
    }
}


static void render_notify(srs_t *srs, srs_voice_event_t *e, void *user_data,
                          void *notify_data)
{
    client_t *c = (client_t *)user_data;

    MRP_UNUSED(srs);
    MRP_UNUSED(notify_data);

    switch (e->type) {
    case SRS_VOICE_EVENT_STARTED:
        print(c, "Rendering of TTS #%u started...", e->id);
        break;

    case SRS_VOICE_EVENT_PROGRESS:
        print(c, "%f %% (%u msec) of TTS #%u rendered...",
              e->data.progress.pcnt, e->data.progress.msec, e->id);
        break;

    case SRS_VOICE_EVENT_COMPLETED:
        print(c, "Rendering of TTS #%u completed.", e->id);
        break;

    case SRS_VOICE_EVENT_TIMEOUT:
        print(c, "Rendering of TTS #%u timed out.", e->id);
        break;

    case SRS_VOICE_EVENT_ABORTED:
        print(c, "Rendering of TTS #%u terminated abnormally.", e->id);
        break;

    default:
        break;
    }
}


static void connect_notify(srs_t *srs, int status, const char *msg,
                           void *user_data)
{
    client_t *c = (client_t *)user_data;

    if (status == 1) {
        set_prompt(c, "connected");
        print(c, "Connection to server established.");
    }
    else {
        set_prompt(c, "disconnected");
        print(c, "Server connection down (error: %d, %s).", status,
              msg ? msg : "<unknown>");
    }
}


static void focus_notify(srs_t *srs, srs_voice_focus_t focus, void *user_data)
{
    client_t *c = (client_t *)user_data;

    print(c, "Client has now %sfocus.", !focus ? "no " :
          focus == SRS_VOICE_FOCUS_SHARED ? "shared " : "exclusive ");
}



static void command_notify(srs_t *srs, int idx, char **tokens, int ntoken,
                           void *user_data)
{
    client_t *c = (client_t *)user_data;
    char      cmd[1024], *p;
    size_t    size;
    int       i;

    print(c, "Got command #%d: ", idx);
    for (i = 0; i < ntoken; i++)
        print(c, "    token #%d: %s", i, tokens[i]);
}


static void register_client(client_t *c)
{
    static int   mainloop_set = 0;

    if (!mainloop_set) {
        if (c->gml != NULL)
            srs_set_gmainloop(c->gml);
        else
            srs_set_mainloop(c->ml);
        mainloop_set = 1;
    }

    c->srs = srs_create(c->app_name, c->app_class, c->commands, c->ncommand,
                        connect_notify, focus_notify, command_notify, c);

    if (c->srs == NULL) {
        print(c, "Failed to create SRS client.");
        return;
    }

    if (srs_connect(c->srs, NULL, 0) < 0)
        print(c, "Failed to connect SRS client.");
}


static void unregister_client(client_t *c)
{
    srs_disconnect(c->srs);
}


static void query_voices_reply(srs_t *srs, srs_voice_actor_t *actors,
                               int nactor, void *user_data, void *notify_data)
{
    client_t          *c = (client_t *)user_data;
    srs_voice_actor_t *a;
    int                i;

    MRP_UNUSED(notify_data);

    print(c, "Server has %d available matching voices.", nactor);

    for (i = 0, a = actors; i < nactor; i++, a++) {
        print(c, "Actor %s:", a->name, a->id);
        print(c, "     language: %s", a->lang);
        print(c, "      dialect: %s", a->dialect);
        print(c, "       gender: %s",
              a->gender == SRS_VOICE_GENDER_MALE ? "male" : "female");
        print(c, "          age: %u", a->age);
        print(c, "  description: %s", a->description);
    }
}


static void query_voices(client_t *c, const char *language)
{
    if (language == NULL)
        language = "";

    if (srs_query_voices(c->srs, language, query_voices_reply, NULL) < 0)
        print(c, "Voice query failed.");
}


static void request_focus(client_t *c, const char *focusstr)
{
    srs_voice_focus_t focus;

    if (!strcmp(focusstr, "none"))
        focus = SRS_VOICE_FOCUS_NONE;
    else if (!strcmp(focusstr, "shared"))
        focus = SRS_VOICE_FOCUS_SHARED;
    else if (!strcmp(focusstr, "exclusive"))
        focus = SRS_VOICE_FOCUS_EXCLUSIVE;
    else {
        print(c, "Unknown focus type '%s'", focusstr);
        return;
    }

    srs_request_focus(c->srs, focus);
}


static void tts_progress_cb(srs_t *srs, srs_voice_event_t *event,
                            void *user_data, void *notify_data)
{
    client_t *c = (client_t *)user_data;

    MRP_UNUSED(srs);
    MRP_UNUSED(notify_data);

    print(c, "Got voice rendering event 0x%x.", event->type);
}


static void request_tts(client_t *c, int ntoken, char **tokens)
{
    const char *sep     = "";
    const char *voice   = "english";
    int         timeout = SRS_VOICE_QUEUE;
    int         events  = FALSE;
    char        msg[1024], *t, *e, *p;
    int         i, o;
    size_t      l;
    ssize_t     n;

    p = msg;
    l = sizeof(msg);
    for (i = 0; i < ntoken; i++) {
        t = tokens[i];
        if (*t == '-') {
            if (!strncmp(t + 1, "timeout:", o=8)) {
                timeout = strtol(t + 1 + o, &e, 10);
                if (*e != '\0') {
                    print(c, "Invalid timeout: %s.", t + 1 + o);
                    return;
                }
            }
            else if (!strncmp(t + 1, "events", o=6)) {
                events = TRUE;
            }
            else if (!strncmp(t + 1, "voice:", o=6)) {
                voice = t + 1 + o;
            }
        }
        else {
            n = snprintf(p, l, "%s%s", sep, t);
            if (n >= l) {
                print(c, "TTS message too long.");
                return;
            }

            p += n;
            l -= n;
            sep = " ";
        }
    }

    print(c, "Requesting TTS for message: '%s'.", msg);

    c->vreq = srs_render_voice(c->srs, msg, voice, 0, 0, timeout,
                               events ? SRS_VOICE_MASK_ALL:SRS_VOICE_MASK_NONE,
                               render_notify, NULL);
}


static void cancel_tts(client_t *c, int ntoken, char **tokens)
{
    int       i;
    uint32_t  vreq;
    char     *end;

    if (ntoken == 0) {
        if (c->vreq)
            srs_cancel_voice(c->srs, c->vreq);
        else
            print(c, "No outstanding TTS request.");
    }
    else {
        for (i = 0; i < ntoken; i++) {
            vreq = strtoul(tokens[i], &end, 10);

            if (end && !*end) {
                print(c, "Cancelling TTS request %u.", vreq);
                srs_cancel_voice(c->srs, vreq);
            }
            else
                print(c, "TTS request id '%s' is invalid.", tokens[i]);
        }
    }
}


static void execute_user_command(client_t *c, int narg, char **args)
{
    const char *cmd;

    cmd = args[0];
    narg--;
    args++;

    switch (narg) {
    case 0:
        if      (!strcmp(cmd, "register"))   register_client(c);
        else if (!strcmp(cmd, "unregister")) unregister_client(c);
        else if (!strcmp(cmd, "exit"))       quit_mainloop(c, 0);
        else if (!strcmp(cmd, "quit"))       quit_mainloop(c, 0);
        else if (!strcmp(cmd, "help")) {
            print(c, "Available commands:");
            print(c, "  register                     - register to server");
            print(c, "  unregister                   - unregister from server");
            print(c, "  focus none|shared|exclusive  - request voice focus");
            print(c, "  add command <command>        - add new command");
            print(c, "  del command <command>        - delete a command");
            print(c, "  render tts '<msg>' \\        - request TTS of <msg>");
            print(c, "    [-voice:<voice>] \\");
            print(c, "    [-timeout:<timeout>]\\");
            print(c, "    [-events]");
            print(c, "  cancel tts '<id>'            - cancel given TTS "
                  "request");
            print(c, "  list commands                - list commands set");
            print(c, "  list voices                  - list available voices");
            print(c, "  help                         - show this help");
            print(c, "  exit                         - exit from client");
        }
        else
            print(c, "Unknown command '%s'.", cmd);
        break;

    case 1:
        if (!strcmp(cmd, "focus")) {
            if (strcmp(args[0], "none") &&
                strcmp(args[0], "shared") &&
                strcmp(args[0], "exclusive")) {
                print(c, "Invalid focus '%s', valid foci are: "
                      "none, shared, and exclusive.", args[0]);
            }
            else
                request_focus(c, args[0]);
        }
        else if (!strcmp(cmd, "reset") && !strcmp(args[0], "commands"))
            reset_commands(c);
        else if (!strcmp(cmd, "list" ) && !strcmp(args[0], "commands"))
            list_commands(c);
        else if (!strcmp(cmd, "list" ) && !strcmp(args[0], "voices"))
            query_voices(c, NULL);
        else if (!strcmp(cmd, "cancel" ) && !strcmp(args[0], "tts"))
            cancel_tts(c, 0, NULL);
        else
            print(c, "Invalid command.");
        break;

    case 2:
        if (!strcmp(cmd, "list" ) && !strcmp(args[0], "voices"))
            query_voices(c, args[1]);
        else if (!strcmp(cmd, "cancel"))
            cancel_tts(c, narg-1, args+1);
        else
            print(c, "Invalid command.");
        break;

    default:
        if (!strcmp(args[0], "command")) {
            if (!strcmp(cmd, "add" ))
                add_command(c, narg-1, args+1);
            else if (!strcmp(cmd, "del" ) || !strcmp(cmd, "delete"))
                del_command(c, narg-1, args+1);
            else
                print(c, "Invalid command.");
        }
        else if (!strcmp(args[0], "tts")) {
            if (!strcmp(cmd, "render"))
                request_tts(c, narg-1, args+1);
            else
                print(c, "Invalid TTS command.");
        }
        else
            print(c, "Invalid command.");
        break;
    }
}


int main(int argc, char *argv[])
{
    client_t *c;

    c = create_client(argv[0]);

    if (c == NULL) {
        fprintf(stderr, "Failed to create client.");
        exit(1);
    }

    parse_cmdline(c, argc, &argv[0]);
    create_mainloop(c);
    setup_signals(c);
    setup_input(c);

    if (c->glib)
        print(c, "Using GMainLoop...");
    else
        print(c, "Using pa_manloop...");

    run_mainloop(c);
    cleanup_input(c);
    destroy_client(c);

    return 0;
}
