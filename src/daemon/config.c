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
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <stdarg.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>

#define _GNU_SOURCE
#include <getopt.h>

#include "srs/config.h"

#ifdef SYSTEMD_ENABLED
#    include <systemd/sd-daemon.h>
#endif

#include <murphy/common/mm.h>
#include <murphy/common/log.h>

#include "srs/daemon/context.h"
#include "srs/daemon/plugin.h"
#include "srs/daemon/config.h"

#ifndef PATH_MAX
#    define PATH_MAX 1024
#endif
#define MAX_ARGS 64

#define MAX_DEPTH   16
#define MAX_BLOCK   64
#define MAX_PREFIX 128

static void valgrind(const char *vg_path, int argc, char **argv, int vg_offs,
                     int saved_argc, char **saved_argv, char **envp);

static srs_cfg_t *find_config(srs_cfg_t *settings, const char *key);


static int  nblock = 0;
static size_t  prflen = 0;
static char blocks[MAX_DEPTH][MAX_BLOCK];
static char prefix[MAX_PREFIX];


/*
 * command line processing
 */

static void config_set_defaults(srs_context_t *srs, const char *bin)
{
#define CFG "speech-recognition.conf"
    static char cfg_file[PATH_MAX], plugin_dir[PATH_MAX];
    char *e;
    int   l;

    if ((e = strstr(bin, "/src/srs-daemon")) != NULL ||
        (e = strstr(bin, "/src/.libs/lt-srs-daemon")) != NULL) {
        static int     warned = 0;

        if (!warned) {
            mrp_log_mask_t saved = mrp_log_set_mask(MRP_LOG_MASK_WARNING);
            mrp_log_warning("***");
            mrp_log_warning("*** Looks like we are run from the source tree.");
            mrp_log_warning("*** Runtime defaults will be set accordingly...");
            mrp_log_warning("***");
            mrp_log_set_mask(saved);
            warned = 1;
        }

        l = e - bin;
        snprintf(cfg_file, sizeof(cfg_file), "%*.*s/%s", l, l, bin, CFG);
        snprintf(plugin_dir, sizeof(plugin_dir), "%*.*s/src/.libs", l, l, bin);

        srs->config_file = cfg_file;
        srs->plugin_dir  = plugin_dir;
        srs->log_mask    = MRP_LOG_UPTO(MRP_LOG_INFO);
        srs->log_target  = MRP_LOG_TO_STDERR;
        srs->foreground  = TRUE;
    }
    else {
        srs->config_file = SRS_DEFAULT_CONFIG_FILE;
        srs->plugin_dir  = SRS_DEFAULT_PLUGIN_DIR;
        srs->log_mask    = MRP_LOG_MASK_ERROR;
        srs->log_target  = MRP_LOG_TO_STDERR;
    }
}


static void print_usage(const char *argv0, int exit_code, const char *fmt, ...)
{
    va_list        ap;
    srs_context_t  srs;
    const char    *cfg, *plg;

    mrp_clear(&srs);
    config_set_defaults(&srs, argv0);
    cfg = srs.config_file;
    plg = srs.plugin_dir;

    if (fmt && *fmt) {
        va_start(ap, fmt);
        vprintf(fmt, ap);
        va_end(ap);
    }

    printf("usage: %s [options]\n\n"
           "The possible options are:\n"
           "  -c, --config-file=PATH         main configuration file to use\n"
           "      The default configuration file is '%s'.\n"
           "  -P, --plugin-dir=PATH          use DIR to search for plugins\n"
           "      The default plugin directory is '%s'.\n"
           "  -L, --load-plugin=NAME         load the given plugin\n"
           "  -s, --set=SETTINGS.\n"
           "      SETTINGS is of the format key1=var1[,key2=var2...]\n"
           "  -t, --log-target=TARGET        log target to use\n"
           "      TARGET is one of stderr,stdout,syslog, or a logfile path\n"
           "  -l, --log-level=LEVELS         logging level to use\n"
           "      LEVELS is a comma separated list of info, error and warning\n"
           "  -v, --verbose                  increase logging verbosity\n"
           "  -d, --debug                    enable given debug configuration\n"
           "  -D, --list-debug               list known debug sites\n"
           "  -f, --foreground               don't daemonize\n"
           "  -h, --help                     show help on usage\n"
           "  -V, --valgrind[=VALGRIND-PATH] try to run under valgrind\n"
#ifdef SYSTEMD_ENABLED
           "  -S, --sockets=var1[,var2...]   set sockets in by systemd\n"
#endif
,
           argv0, cfg, plg);

    if (exit_code < 0)
        return;
    else
        exit(exit_code);
}


#ifdef SYSTEMD_ENABLED
static int set_passed_sockets(srs_context_t *srs, const char *variables)
{
    const char *b, *e;
    char        key[256], val[64];
    int         nfd, i, n;
    size_t      len;

    nfd = sd_listen_fds(0);

    if (nfd <= 0)
        return nfd;

    i = 0;
    b = variables;
    while (b && *b) {
        while (*b == ',' || *b == ' ' || *b == '\t')
            b++;

        if (!*b)
            return 0;

        if (i >= nfd)
            return 0;

        if ((e = strchr(b, ',')) != NULL)
            len = e - b;
        else
            len = strlen(b);

        if (len >= sizeof(key)) {
            errno = EOVERFLOW;
            return -1;
        }

        strncpy(key, b, len);
        key[len] = '\0';

        n = snprintf(val, sizeof(val), "%d", SD_LISTEN_FDS_START + i);

        if (n < 0 || n >= (int)sizeof(val))
            return -1;

        srs_set_config(srs, key, val);

        b = e;
        i++;
    }

    return 0;
}
#endif


static void config_load_plugins(srs_context_t *srs, char *plugins)
{
    char name[PATH_MAX], *p, *n;
    int  l;

    p = plugins;
    while (p && *p) {
        while (*p == ' ')
            p++;

        n = strchr(p, ' ');

        if (n != NULL) {
            l = n - p;

            if (l > (int)sizeof(name) - 1) {
                mrp_log_error("Plugin name '%*.*s' is too long.", l, l, p);
                exit(1);
            }

            strncpy(name, p, l);
            name[l] = '\0';
        }
        else {
            if (snprintf(name, sizeof(name), "%s", p) >= (int)sizeof(name)) {
                mrp_log_error("Plugin name '%s' is too long.", p);
                exit(1);
            }
        }

        if (srs_create_plugin(srs, name) == NULL) {
            mrp_log_error("Failed to load plugin '%s'.", name);
            exit(1);
        }

        p = n ? n : NULL;
    }
}


static void push_block(const char *block, int blen)
{
    if (nblock >= MAX_DEPTH) {
        mrp_log_error("Too deeply nested configuration block: %s.%s",
                      prefix, block);
        exit(1);
    }

    if (blen >= MAX_BLOCK - 1) {
        mrp_log_error("Too long block name '%s'.", block);
        exit(1);
    }

    if (prflen + 1 + blen + 1 >= sizeof(prefix)) {
        mrp_log_error("Too long nested block name '%s.%s'.", prefix, block);
        exit(1);
    }

    strncpy(blocks[nblock], block, blen);
    blocks[nblock][blen] = '\0';
    if (nblock > 0)
        prefix[prflen++] = '.';
    strncpy(prefix + prflen, block, blen);
    prefix[prflen + blen] = '\0';
    nblock++;
    prflen += blen;

    mrp_debug("pushed block '%*.*s', prefix now '%s'", blen, blen, block,
              prefix);
}


static void pop_block(void)
{
    char *block;
    size_t blen;

    if (nblock <= 0) {
        mrp_log_error("Unbalanced block open ({) and close (}).");
        exit(1);
    }

    block = blocks[--nblock];
    blen  = strlen(block);

    if (nblock > 0 && prflen < blen + 1) {
        mrp_log_error("Internal error in nested block book-keeping.");
        exit(1);
    }

    if (nblock > 0)
        prflen -= blen + 1;
    else
        prflen = 0;
    prefix[prflen] = '\0';

    mrp_debug("popped block '%s', prefix now '%s'", block, prefix);
}


static void config_parse_settings(srs_context_t *srs, char *settings)
{
    char   *key, *val, *next;
    size_t  klen, vlen;
    char    keybuf[128], valbuf[512];

    while (*settings == ' ' || *settings == '\t')
        settings++;

    key = settings;

    if (!strncmp(key, "load ", 5)) {
        config_load_plugins(srs, key + 5);
        return;
    }

    if (*key == '}') {
        key++;

        while (*key == ' ' || *key == '\t')
            key++;

        if (*key != '\0') {
            mrp_log_error("Invalid block closing '%s'.", settings);
            exit(1);
        }

        pop_block();
        return;
    }

    while (key && *key) {
        val  = strchr(key, '=');
        next = strchr(key, ';');

        if (next != NULL && val > next)
            val = NULL;

        if (val != NULL) {
            klen = val - key;
            val++;
            vlen = next ? (size_t)(next - val) : strlen(val);
        }
        else {
            val  = "true";
            vlen = 4;
            klen = next ? (size_t)(next - key) : strlen(key);
        }

        while (klen > 0 && key[klen - 1] == ' ')
            klen--;
        while (vlen > 0 && val[0] == ' ') {
            val++;
            vlen--;
        }
        while (vlen > 0 && val[vlen - 1] == ' ')
            vlen--;

        if (klen + prflen >= sizeof(keybuf) || vlen >= sizeof(valbuf)) {
            mrp_log_error("Configuration setting %*.*s = %*.*s too long.",
                          (int)klen, (int)klen, key,
                          (int)vlen, (int)vlen, val);
            exit(1);
        }

        if (vlen == 1 && val[0] == '{') {
            push_block(key, klen);
            return;
        }

        if (nblock > 0)
            snprintf(keybuf, sizeof(keybuf), "%s.%*.*s", prefix,
                     (int)klen, (int)klen, key);
        else
            snprintf(keybuf, sizeof(keybuf), "%*.*s",
                     (int)klen, (int)klen, key);
        strncpy(valbuf, val, vlen);
        valbuf[vlen] = '\0';

        mrp_debug("setting configuration variable %s=%s", keybuf, valbuf);
        srs_set_config(srs, keybuf, valbuf);

        key = next ? next + 1 : NULL;
    }
}


static void config_parse_file(srs_context_t *srs, char *path)
{
    FILE *fp;
    char  line[1024], *p, *end;

    fp = fopen(path, "r");

    if (fp == NULL) {
        printf("Failed to open configuration file '%s'.\n", path);
        exit(1);
    }

    nblock = 0;
    prflen = 0;

    while ((p = fgets(line, sizeof(line), fp)) != NULL) {
        while (*p == ' ' || *p == '\t')
            p++;

        if (*p == '#')
            continue;

        if ((end = strchr(p, '\n')) != NULL)
            *end = '\0';

        config_parse_settings(srs, p);
    }

    nblock = 0;
    prflen = 0;

    fclose(fp);
}


void config_parse_cmdline(srs_context_t *srs, int argc, char **argv,
                          char **envp)
{
#   define OPTIONS "c:P:L:l:t:B:s:fvd:hS:V"
    struct option options[] = {
        { "config-file"  , required_argument, NULL, 'c' },
        { "plugin-dir"   , required_argument, NULL, 'P' },
        { "load-plugin"  , required_argument, NULL, 'L' },
        { "log-level"    , required_argument, NULL, 'l' },
        { "log-target"   , required_argument, NULL, 't' },
        { "set"          , required_argument, NULL, 's' },
        { "verbose"      , optional_argument, NULL, 'v' },
        { "debug"        , required_argument, NULL, 'd' },
        { "foreground"   , no_argument      , NULL, 'f' },
        { "valgrind"     , optional_argument, NULL, 'V' },
#ifdef SYSTEMD_ENABLED
        { "sockets"      , required_argument, NULL, 'S' },
#endif
        { "help"         , no_argument      , NULL, 'h' },
        { NULL, 0, NULL, 0 }
    };


#   define SAVE_ARG(a) do {                                     \
        if (saved_argc >= MAX_ARGS)                             \
            print_usage(argv[0], EINVAL,                        \
                        "too many command line arguments");     \
        else                                                    \
            saved_argv[saved_argc++] = a;                       \
    } while (0)
#   define SAVE_OPT(o)       SAVE_ARG(o)
#   define SAVE_OPTARG(o, a) SAVE_ARG(o); SAVE_ARG(a)
    char *saved_argv[MAX_ARGS];
    int   saved_argc;

    int   opt, help;

    config_set_defaults(srs, argv[0]);
    mrp_log_set_mask(srs->log_mask);
    mrp_log_set_target(srs->log_target);

    saved_argc = 0;
    saved_argv[saved_argc++] = argv[0];

    help = FALSE;

    while ((opt = getopt_long(argc, argv, OPTIONS, options, NULL)) != -1) {
        switch (opt) {
        case 'c':
            SAVE_OPTARG("-c", optarg);
            srs->config_file = optarg;
            config_parse_file(srs, optarg);
            break;

        case 'P':
            SAVE_OPTARG("-P", optarg);
            srs->plugin_dir = optarg;
            break;

        case 'L':
            SAVE_OPTARG("-L", optarg);
            if (srs_create_plugin(srs, optarg) == NULL) {
                mrp_log_error("Failed to load plugin '%s'.", optarg);
                exit(1);
            }
            break;

        case 'v':
            SAVE_OPT("-v");
            srs->log_mask <<= 1;
            srs->log_mask  |= 1;
            mrp_log_set_mask(srs->log_mask);
            break;

        case 'l':
            SAVE_OPTARG("-l", optarg);
            srs->log_mask = mrp_log_parse_levels(optarg);
            if (srs->log_mask < 0)
                print_usage(argv[0], EINVAL, "invalid log level '%s'", optarg);
            else
                mrp_log_set_mask(srs->log_mask);
            break;

        case 't':
            SAVE_OPTARG("-t", optarg);
            srs->log_target = optarg;
            break;

        case 's':
            SAVE_OPTARG("-s", optarg);
            nblock = 0;
            prflen = 0;
            config_parse_settings(srs, optarg);
            nblock = 0;
            prflen = 0;
            break;

        case 'd':
            SAVE_OPTARG("-d", optarg);
            srs->log_mask |= MRP_LOG_MASK_DEBUG;
            mrp_debug_set_config(optarg);
            mrp_debug_enable(TRUE);
            break;

        case 'f':
            SAVE_OPT("-f");
            srs->foreground = TRUE;
            break;

        case 'V':
            valgrind(optarg, argc, argv, optind, saved_argc, saved_argv, envp);
            break;

#ifdef SYSTEMD_ENABLED
        case 'S':
            SAVE_OPTARG("-S", optarg);
            set_passed_sockets(srs, optarg);
            break;
#endif

        case 'h':
            SAVE_OPT("-h");
            help++;
            break;

        default:
            print_usage(argv[0], EINVAL, "invalid option '%c'", opt);
        }
    }

    if (help) {
        print_usage(argv[0], -1, "");
        exit(0);
    }

}


/*
 * configuration setting processing
 *
 * Format of a configuration entry is
 *    setting: <key> = <value> | <key> | load <plugin>
 *    entry: setting | setting ; entry
 */

static srs_cfg_t *find_config(srs_cfg_t *settings, const char *key)
{
    if (settings != NULL) {
        while (settings->key != NULL) {
            if (!strcmp(settings->key, key))
                return settings;
            else
                settings++;
        }
    }

    return NULL;
}


const char *srs_config_get_string(srs_cfg_t *settings, const char *key,
                                  const char *defval)
{
    srs_cfg_t *cfg = find_config(settings, key);

    if (cfg != NULL) {
        cfg->used = TRUE;

        return cfg->value;
    }
    else
        return defval;
}


int srs_config_get_bool(srs_cfg_t *settings, const char *key, int defval)
{
    srs_cfg_t *cfg = find_config(settings, key);

    if (cfg != NULL) {
        cfg->used = TRUE;

        if (!strcasecmp(cfg->value, "true"))
            return TRUE;
        else if (!strcasecmp(cfg->value, "false"))
            return FALSE;

        mrp_log_error("Value '%s' for key '%s' is not a boolean.",
                      cfg->value, cfg->key);
        exit(1);
    }

    return defval;
}


int32_t srs_config_get_int32(srs_cfg_t *settings, const char *key,
                             int32_t defval)
{
    srs_cfg_t *cfg = find_config(settings, key);
    int32_t    val;
    char      *end;

    if (cfg != NULL) {
        cfg->used = TRUE;

        val = (int32_t)strtol(cfg->value, &end, 0);

        if (end && !*end)
            return val;
        else {
            mrp_log_error("Value '%s' for key '%s' is not an int32.",
                          cfg->value, cfg->key);
            exit(1);
        }
    }

    return defval;
}


uint32_t srs_config_get_uint32(srs_cfg_t *settings, const char *key,
                               uint32_t defval)
{
    srs_cfg_t *cfg = find_config(settings, key);
    uint32_t   val;
    char      *end;

    if (cfg != NULL) {
        cfg->used = TRUE;

        val = (uint32_t)strtoul(cfg->value, &end, 0);

        if (end && !*end)
            return val;
        else {
            mrp_log_error("Value '%s' for key '%s' is not an uint32.",
                          cfg->value, cfg->key);
            exit(1);
        }
    }

    return defval;
}


int srs_config_collect(srs_cfg_t *settings, const char *prefix,
                       srs_cfg_t **matching)
{
    srs_cfg_t *m = NULL;
    int        n = 0;
    size_t     osize, nsize, l;

    if (settings == NULL)
        goto out;

    l = strlen(prefix);

    while (settings->key != NULL) {
        if (!strncmp(settings->key, prefix, l)) {
            osize = sizeof(*m) *  n;
            nsize = sizeof(*m) * (n + 1);

            if (!mrp_reallocz(m, osize, nsize))
                goto fail;

            m[n].key   = mrp_strdup(settings->key);
            m[n].value = mrp_strdup(settings->value);

            if (m[n].key == NULL || m[n].value == NULL) {
                n++;
                goto fail;
            }

            n++;
        }

        settings++;
    }

 out:
    if (m != NULL) {
        osize = sizeof(*m) *  n;
        nsize = sizeof(*m) * (n + 1);

        if (!mrp_reallocz(m, osize, nsize))
            goto fail;
    }

    *matching = m;

    return n;

 fail:
    while (--n >= 0) {
        mrp_free(m[n].key);
        mrp_free(m[n].value);
        n--;
    }

    mrp_free(m);

    *matching = NULL;
    return -1;
}


void srs_config_free(srs_cfg_t *settings)
{
    srs_cfg_t *s;

    if (settings != NULL) {
        for (s = settings; s->key != NULL; s++) {
            mrp_free(s->key);
            mrp_free(s->value);
        }

        mrp_free(settings);
    }
}


void srs_set_config(srs_context_t *srs, const char *key, const char *value)
{
    srs_cfg_t *var;
    size_t     osize, nsize, diff;

    var = find_config(srs->settings, key);

    if (var == NULL) {
        diff  = srs->nsetting == 0 ? 2 : 1;
        osize = sizeof(*srs->settings) *  srs->nsetting;
        nsize = sizeof(*srs->settings) * (srs->nsetting + diff);

        if (!mrp_reallocz(srs->settings, osize, nsize))
            goto nomem;

        var = srs->settings + srs->nsetting++;
    }
    else {
        mrp_log_warning("Overwriting configuration setting '%s = %s'",
                        var->key, var->value);
        mrp_log_warning("with new setting '%s = %s'", key, value);

        mrp_free(var->key);
        mrp_free(var->value);
        var->key = var->value = NULL;
    }

    var->key   = mrp_strdup(key);
    var->value = mrp_strdup(value);

    if (var->key == NULL || var->value == NULL) {
    nomem:
        mrp_log_error("Failed to allocate configuration variable %s=%s.",
                      key, value);
        exit(1);
    }
}


/*
 * bridging to valgrind
 */

static void valgrind(const char *vg_path, int argc, char **argv, int vg_offs,
                     int saved_argc, char **saved_argv, char **envp)
{
#define VG_ARG(a) vg_argv[vg_argc++] = a
    char *vg_argv[MAX_ARGS + 1];
    int   vg_argc, normal_offs, i;

    vg_argc = 0;

    /* set valgrind binary */
    VG_ARG(vg_path ? (char *)vg_path : "/usr/bin/valgrind");

    /* add valgrind arguments */
    for (i = vg_offs; i < argc; i++)
        VG_ARG(argv[i]);

    /* save offset to normal argument list for fallback */
    normal_offs = vg_argc;

    /* add our binary and our arguments */
    for (i = 0; i < saved_argc; i++)
        vg_argv[vg_argc++] = saved_argv[i];

    /* terminate argument list */
    VG_ARG(NULL);

    /* try executing through valgrind */
    mrp_log_warning("Executing through valgrind (%s)...", vg_argv[0]);
    execve(vg_argv[0], vg_argv, envp);

    /* try falling back to normal execution */
    mrp_log_error("Executing through valgrind failed (error %d: %s), "
                  "retrying without...", errno, strerror(errno));
    execve(vg_argv[normal_offs], vg_argv + normal_offs, envp);

    /* can't do either, so just give up */
    mrp_log_error("Fallback to normal execution failed (error %d: %s).",
                  errno, strerror(errno));
    exit(1);
}
