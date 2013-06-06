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

#include <stdio.h>
#include <errno.h>
#include <getopt.h>
#include <stdarg.h>

#include <murphy/common/macros.h>
#include <murphy/common/mm.h>
#include <murphy/common/debug.h>
#include <murphy/common/mainloop.h>
#include <murphy/common/dbus.h>
#include <murphy/common/pulse-glue.h>

#include <breedline/breedline-murphy.h>

#include "src/daemon/dbus-config.h"


static const char *default_commands[] = {
    "hal open the pod bay doors",
    "hal play music",
    "hal stop music",
    "hal exit",
    "hal dial __push_dict__(digits) *",
    "hal play artist __push_dict__(artists) *"
};


typedef struct {
    pa_mainloop    *pa;
    mrp_mainloop_t *ml;
    mrp_dbus_t     *dbus;
    brl_t          *brl;
    const char     *app_class;
    const char     *app_name;
    const char     *dbus_address;
    int             exit_status;
    int             server_up : 1;
    int             registered : 1;
    const char     *focus;
    char          **commands;
    int             ncommand;
} client_t;


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

    c->dbus_address = "session";
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

        mrp_free(c);
    }
}


static client_t *create_client(const char *argv0)
{
    client_t *c = mrp_allocz(sizeof(*c));

    if (c != NULL) {
        set_client_defaults(c, argv0);

        c->pa = pa_mainloop_new();
        c->ml = mrp_mainloop_pulse_get(pa_mainloop_get_api(c->pa));

        if (c->pa != NULL && c->ml != NULL)
            return c;
        else
            destroy_client(c);
    }

    return NULL;
}


static int focus_notify(mrp_dbus_t *dbus, DBusMessage *msg, void *user_data)
{
    client_t   *c = (client_t *)user_data;
    const char *focus;

    MRP_UNUSED(dbus);

    hide_prompt(c);
    if (dbus_message_get_args(msg, NULL, DBUS_TYPE_STRING, &focus,
                              DBUS_TYPE_INVALID))
        print(c, "Voice focus is now: %s", focus);
    else
        print(c, "Failed to parse voice focus notification.");
    show_prompt(c);

    return TRUE;
}


static int voice_command_notify(mrp_dbus_t *dbus, DBusMessage *msg,
                                void *user_data)
{
    client_t   *c = (client_t *)user_data;
    const char *command;

    MRP_UNUSED(dbus);

    hide_prompt(c);
    if (dbus_message_get_args(msg, NULL, DBUS_TYPE_STRING, &command,
                              DBUS_TYPE_INVALID))
        print(c, "Received voice command: %s", command);
    else
        print(c, "Failed to parse voice command notification.");
    show_prompt(c);

    return TRUE;
}


static void server_name_change(mrp_dbus_t *dbus, const char *name, int running,
                               const char *owner, void *user_data)
{
    client_t *c = (client_t *)user_data;

    MRP_UNUSED(dbus);

    c->server_up = running;

    if (running) {
        set_prompt(c, "server up");
        print(c, "Server (%s) is now up (as %s).", name, owner);
    }
    else {
        set_prompt(c, "server down");
        print(c, "Server (%s) is now down.", name);
        c->registered = FALSE;
    }
}


static void setup_dbus(client_t *c)
{
    const char *name;

    c->dbus = mrp_dbus_get(c->ml, c->dbus_address, NULL);

    if (c->dbus != NULL) {
        name = SRS_SERVICE_NAME;
        if (mrp_dbus_follow_name(c->dbus, name, server_name_change, c) &&
            mrp_dbus_subscribe_signal(c->dbus, focus_notify, c,
                                      NULL, SRS_SERVICE_PATH,
                                      SRS_SERVICE_INTERFACE, SRS_SIGNAL_FOCUS,
                                      NULL) &&
            mrp_dbus_subscribe_signal(c->dbus, voice_command_notify, c,
                                      NULL, SRS_SERVICE_PATH,
                                      SRS_SERVICE_INTERFACE, SRS_SIGNAL_COMMAND,
                                      NULL))
            return;
        else {
            print(c, "Failed to set up server D-BUS name tracking.");
            exit(1);
        }
    }
    else {
        print(c, "Failed to connect to D-BUS (%s).", c->dbus_address);
        exit(1);
    }
}


static void cleanup_dbus(client_t *c)
{
    const char *name;

    if (c != NULL) {
        name = SRS_SERVICE_NAME;
        mrp_dbus_forget_name(c->dbus, name, server_name_change, c);
        mrp_dbus_unsubscribe_signal(c->dbus, focus_notify, c,
                                    NULL, SRS_SERVICE_PATH,
                                    SRS_SERVICE_INTERFACE, SRS_SIGNAL_FOCUS,
                                    NULL);
        mrp_dbus_unsubscribe_signal(c->dbus, voice_command_notify, c,
                                    NULL, SRS_SERVICE_PATH,
                                    SRS_SERVICE_INTERFACE, SRS_SIGNAL_COMMAND,
                                    NULL);
        mrp_dbus_unref(c->dbus);
    }
}


static void run_mainloop(client_t *c)
{
    pa_mainloop_run(c->pa, &c->exit_status);
}


static void quit_mainloop(client_t *c, int exit_status)
{
    if (c != NULL)
        pa_mainloop_quit(c->pa, exit_status);
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
    c->brl = brl_create_with_murphy(fd, "starting", c->ml, process_input, c);

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
           "  -D, --dbus=DBUS                D-BUS to use\n"
           "      DBUS is 'session', 'system', or a DBUS daemon address.\n"
           "  -v, --verbose                  increase logging verbosity\n"
           "  -d, --debug                    enable debug messages\n"
           "  -h, --help                     show help on usage\n", exe);
    printf("\n");

    if (exit_code < 0)
        return;
    else
        exit(exit_code);
}


static void parse_cmdline(client_t *c, int argc, char **argv)
{
#   define OPTIONS "N:C:D:d:h"
    struct option options[] = {
        { "name"      , required_argument, NULL, 'N' },
        { "class"     , required_argument, NULL, 'C' },
        { "dbus"      , required_argument, NULL, 'D' },
        { "debug"     , required_argument, NULL, 'd' },
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

        case 'D':
            c->dbus_address = optarg;
            break;

        case 'd':
            mrp_debug_set_config(optarg);
            mrp_debug_enable(TRUE);
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


static void register_reply(mrp_dbus_t *dbus, DBusMessage *rpl, void *user_data)
{
    client_t *c = (client_t *)user_data;

    MRP_UNUSED(dbus);

    if (dbus_message_get_type(rpl) == DBUS_MESSAGE_TYPE_METHOD_RETURN) {
        set_prompt(c, c->app_name);
        print(c, "Successfully registered to server.");
    }
    else {
        set_prompt(c, "failed");
        print(c, "Failed to register to server.");
    }
}


static void register_client(client_t *c)
{
    const char **cmds   = (const char **)c->commands;
    int          ncmd   = c->ncommand;
    const char  *dest   = SRS_SERVICE_NAME;
    const char  *path   = SRS_SERVICE_PATH;
    const char  *iface  = SRS_SERVICE_INTERFACE;
    const char  *method = SRS_METHOD_REGISTER;

    if (!c->server_up) {
        print(c, "Server is currently down.");

        return;
    }

    if (!mrp_dbus_call(c->dbus, dest, path, iface, method, -1,
                       register_reply, c,
                       DBUS_TYPE_STRING, &c->app_name,
                       DBUS_TYPE_STRING, &c->app_class,
                       DBUS_TYPE_ARRAY , DBUS_TYPE_STRING, &cmds, ncmd,
                       DBUS_TYPE_INVALID))
        print(c, "Failed to send register message to server.");
}


static void unregister_reply(mrp_dbus_t *dbus, DBusMessage *rpl,
                             void *user_data)
{
    client_t *c = (client_t *)user_data;

    MRP_UNUSED(dbus);

    if (dbus_message_get_type(rpl) == DBUS_MESSAGE_TYPE_METHOD_RETURN) {
        set_prompt(c, "unregistered");
        print(c, "Successfully unregistered from server.");
    }
    else
        print(c, "Failed to unregister from server.");
}


static void unregister_client(client_t *c)
{
    const char  *dest   = SRS_SERVICE_NAME;
    const char  *path   = SRS_SERVICE_PATH;
    const char  *iface  = SRS_SERVICE_INTERFACE;
    const char  *method = SRS_METHOD_UNREGISTER;

    if (!c->server_up) {
        print(c, "Server is currently down.");
        return;
    }

    if (!mrp_dbus_call(c->dbus, dest, path, iface, method, -1,
                       unregister_reply, c, DBUS_TYPE_INVALID))
        print(c, "Failed to send unregister message to server.");
}


static void focus_reply(mrp_dbus_t *dbus, DBusMessage *rpl, void *user_data)
{
    client_t *c = (client_t *)user_data;

    MRP_UNUSED(dbus);

    if (dbus_message_get_type(rpl) == DBUS_MESSAGE_TYPE_METHOD_RETURN)
        print(c, "Focus request sent to server.");
    else
        print(c, "Focus request failed on server.");
}


static void request_focus(client_t *c, const char *focus)
{
    const char  *dest   = SRS_SERVICE_NAME;
    const char  *path   = SRS_SERVICE_PATH;
    const char  *iface  = SRS_SERVICE_INTERFACE;
    const char  *method = SRS_METHOD_FOCUS;

    if (!c->server_up) {
        print(c, "Server is currently down.");
        return;
    }

    if (!mrp_dbus_call(c->dbus, dest, path, iface, method, -1,
                       focus_reply, c,
                       DBUS_TYPE_STRING, &focus, DBUS_TYPE_INVALID))
        print(c, "Failed to send focus request to server.");
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
            print(c, "  list commands                - list commands set");
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

    setup_signals(c);
    parse_cmdline(c, argc, &argv[0]);
    setup_dbus(c);
    setup_input(c);
    run_mainloop(c);
    cleanup_input(c);
    cleanup_dbus(c);
    destroy_client(c);

    return 0;
}