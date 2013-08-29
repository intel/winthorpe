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

#include <murphy/common/mm.h>
#include <murphy/common/log.h>

#include "src/daemon/context.h"
#include "src/daemon/plugin.h"
#include "src/daemon/config.h"

#define ARGV_MAX 128
#define ARGV_VG    3

static srs_cfg_t *find_config(srs_cfg_t *settings, const char *key);

static void save_args(char **saved_argv, int argc, char **argv)
{
    int i;

    for (i = 0; i < argc; i++)
        saved_argv[i] = argv[i];

    saved_argv[i] = NULL;
}


static int valgrind(const char *vg_path, int full, int argc, char **argv,
                    int envc, char **envp)
{
    char       *vg_env[envc + 2], **vg_envp;
    char       *vg_argv[ARGV_MAX + ARGV_VG + 1];
    int         vg_argc, offs, i, j;
    const char *bin, *e, *ldp;
    char        lib[PATH_MAX];
    int         len;

    bin = argv[0];
    ldp = getenv("LD_LIBRARY_PATH");

    if ((e = strstr(bin, "/src/srs-daemon")) != NULL) {
        len = e - bin;

        if (snprintf(lib, sizeof(lib), "LD_LIBRARY_PATH=%*.*s/.libs%s%s",
                     len + 5, len + 5, bin,
                     ldp ? ":" : "", ldp ? ldp : "") >= sizeof(lib) - 1) {
            errno = ENAMETOOLONG;
            return -1;
        }

        vg_env[0] = lib;
        vg_envp   = vg_env + 1;
    }
    else
        vg_envp = vg_env;

    vg_argc = i = 0;
    vg_argv[vg_argc++] = (char *)(vg_path ? vg_path : "/usr/bin/valgrind");
    if (full)
        vg_argv[vg_argc++] = "--leak-check=full";
    vg_argv[vg_argc++] = argv[i++];

    for ( ; i < argc; i++ ) {
        if (!strcmp(argv[i], "-V") ||
            !strncmp(argv[i], "-V=", 3) ||
            !strncmp(argv[i], "--valgrind", 10) ||
            !strcmp(argv[i], "-W") ||
            !strncmp(argv[i], "-W=", 3) ||
            !strncmp(argv[i], "--valgrind-full", 15))
            continue;

        vg_argv[vg_argc++] = argv[i];
    }

    vg_argv[vg_argc] = NULL;

    for (i = 0; envp[i] != NULL; i++)
        vg_envp[i] = envp[i];
    vg_envp[i] = NULL;

    return execve(vg_argv[0], vg_argv, vg_env);
}


/*
 * command line processing
 */

static void config_set_defaults(srs_context_t *srs, const char *bin)
{
#define CFG "speech-recognition.conf"
    static char  plgbuf[PATH_MAX], cfgbuf[PATH_MAX];
    char         wd[PATH_MAX];
    const char  *plugin_dir, *config_file, *e;
    int          len, n;

    if ((e = strstr(bin, "/src/srs-daemon")) != NULL) {
    srctree:
        len = e - bin;

        n = snprintf(plgbuf, sizeof(plgbuf), "%*.*s/src/.libs", len, len, bin);
        if (n >= sizeof(plgbuf) - 1)
            plugin_dir = SRS_DEFAULT_PLUGIN_DIR;
        else
            plugin_dir = plgbuf;

        n = snprintf(cfgbuf, sizeof(cfgbuf), "%*.*s/%s", len, len, bin, CFG);
        if (n >= sizeof(cfgbuf) - 1)
            config_file = SRS_DEFAULT_CONFIG_FILE;
        else
            config_file = cfgbuf;
    }
    else {
        if (getcwd(wd, sizeof(wd)) != NULL) {
            if ((e = strrchr(wd, '/')) != NULL && !strcmp(e, "/src")) {
                strncat(wd, "/srs-daemon", sizeof(wd));
                bin = wd;
                goto srctree;
            }
        }
        config_file = SRS_DEFAULT_CONFIG_FILE;
        plugin_dir  = SRS_DEFAULT_PLUGIN_DIR;
    }

    srs->config_file = config_file;
    srs->plugin_dir  = plugin_dir;
    srs->log_mask    = MRP_LOG_MASK_ERROR;
    srs->log_target  = MRP_LOG_TO_STDERR;
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
           "  -W, --valgrind-full[=VALGRIND-PATH] try to run under valgrind\n",
           argv0, cfg, plg);

    if (exit_code < 0)
        return;
    else
        exit(exit_code);
}


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

        if (klen >= sizeof(keybuf) || vlen >= sizeof(valbuf)) {
            mrp_log_error("Configuration setting %*.*s = %*.*s too long.",
                          (int)klen, (int)klen, key,
                          (int)vlen, (int)vlen, val);
            exit(1);
        }

        strncpy(keybuf, key, klen);
        keybuf[klen] = '\0';
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

    while ((p = fgets(line, sizeof(line), fp)) != NULL) {
        while (*p == ' ' || *p == '\t')
            p++;

        if (*p == '#')
            continue;

        if ((end = strchr(p, '\n')) != NULL)
            *end = '\0';

        config_parse_settings(srs, p);
    }

    fclose(fp);
}


void config_parse_cmdline(srs_context_t *srs, int argc, char **argv,
                          char **envp)
{
#   define OPTIONS "c:P:L:l:t:B:s:fvd:DV::W::h"
    struct option options[] = {
        { "config-file"  , required_argument, NULL, 'c' },
        { "plugin-dir"   , required_argument, NULL, 'P' },
        { "load-plugin"  , required_argument, NULL, 'L' },
        { "log-level"    , required_argument, NULL, 'l' },
        { "log-target"   , required_argument, NULL, 't' },
        { "set"          , required_argument, NULL, 's' },
        { "verbose"      , optional_argument, NULL, 'v' },
        { "debug"        , required_argument, NULL, 'd' },
        { "list-debug"   , no_argument      , NULL, 'D' },
        { "foreground"   , no_argument      , NULL, 'f' },
        { "valgrind"     , optional_argument, NULL, 'V' },
        { "valgrind-full", optional_argument, NULL, 'W' },
        { "help"         , no_argument      , NULL, 'h' },
        { NULL, 0, NULL, 0 }
    };

    char *saved_argv[ARGV_MAX];
    int   saved_argc, envc;
    int   opt, help;

    save_args(saved_argv, argc, argv);
    saved_argc = argc;

    config_set_defaults(srs, argv[0]);
    mrp_log_set_mask(srs->log_mask);
    mrp_log_set_target(srs->log_target);

    help = FALSE;

    while ((opt = getopt_long(argc, argv, OPTIONS, options, NULL)) != -1) {
        switch (opt) {
        case 'c':
            srs->config_file = optarg;
            config_parse_file(srs, optarg);
            break;

        case 'P':
            srs->plugin_dir = optarg;
            break;

        case 'L':
            if (srs_create_plugin(srs, optarg) == NULL) {
                mrp_log_error("Failed to load plugin '%s'.", optarg);
                exit(1);
            }
            break;

        case 'v':
            srs->log_mask <<= 1;
            srs->log_mask  |= 1;
            mrp_log_set_mask(srs->log_mask);
            break;

        case 'l':
            srs->log_mask = mrp_log_parse_levels(optarg);
            if (srs->log_mask < 0)
                print_usage(argv[0], EINVAL, "invalid log level '%s'", optarg);
            else
                mrp_log_set_mask(srs->log_mask);
            break;

        case 't':
            srs->log_target = optarg;
            break;

        case 's':
            config_parse_settings(srs, optarg);
            break;

        case 'd':
            srs->log_mask |= MRP_LOG_MASK_DEBUG;
            mrp_debug_set_config(optarg);
            mrp_debug_enable(TRUE);
            break;

        case 'D':
            printf("Known debug sites:\n");
            mrp_debug_dump_sites(stdout, 4);
            exit(0);
            break;

        case 'f':
            srs->foreground = TRUE;
            break;

        case 'V':
        case 'W':
            envc = 0;
            if (envp != NULL)
                while (envp[envc] != NULL)
                    envc++;
            valgrind(optarg, opt == 'W', saved_argc, saved_argv, envc, envp);
            mrp_log_error("Failed to run through valgrind.");
            exit(1);

        case 'h':
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


const char *srs_get_string_config(srs_cfg_t *settings, const char *key,
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


int srs_get_bool_config(srs_cfg_t *settings, const char *key, int defval)
{
    srs_cfg_t *cfg = find_config(settings, key);

    if (cfg != NULL) {
        cfg->used = TRUE;

        if (!strcasecmp(cfg->value, "true"))
            return TRUE;
        else if (!strcasecmp(cfg->value, "false"))
            return FALSE;

        mrp_log_error("Value '%s' for key '%s' is not a boolean.",
                      cfg->key, cfg->value);
        exit(1);
    }

    return defval;
}


int32_t srs_get_int32_config(srs_cfg_t *settings, const char *key,
                             int32_t defval)
{
    srs_cfg_t *cfg = find_config(settings, key);
    int32_t    val;
    char      *end;

    if (cfg != NULL) {
        cfg->used = TRUE;

        val = (int32_t)strtol(cfg->value, &end, 0);

        if (*end && !*end)
            return val;
        else {
            mrp_log_error("Value '%s' for key '%s' is not an int32.",
                          cfg->key, cfg->value);
            exit(1);
        }
    }

    return defval;
}


uint32_t srs_get_uint32_config(srs_cfg_t *settings, const char *key,
                               uint32_t defval)
{
    srs_cfg_t *cfg = find_config(settings, key);
    uint32_t   val;
    char      *end;

    if (cfg != NULL) {
        cfg->used = TRUE;

        val = (uint32_t)strtoul(cfg->value, &end, 0);

        if (*end && !*end)
            return val;
        else {
            mrp_log_error("Value '%s' for key '%s' is not an uint32.",
                          cfg->key, cfg->value);
            exit(1);
        }
    }

    return defval;
}


int srs_collect_config(srs_cfg_t *settings, const char *prefix,
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
    while (n >= 0) {
        mrp_free(m[n].key);
        mrp_free(m[n].value);
    }

    mrp_free(m);

    *matching = NULL;
    return -1;
}


void srs_free_config(srs_cfg_t *settings)
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
