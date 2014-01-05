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
#include <errno.h>
#include <limits.h>
#include <dirent.h>
#include <dlfcn.h>
#include <regex.h>
#include <dirent.h>
#include <sys/types.h>
#include <sys/stat.h>

#include <murphy/common/macros.h>
#include <murphy/common/debug.h>
#include <murphy/common/mainloop.h>

#include "srs/daemon/plugin.h"
#include "srs/daemon/voice.h"

#define PLUGIN_NAME    "festival-loader"
#define PLUGIN_AUTHORS "Krisztian Litkey <kli@iki.fi>"
#define PLUGIN_VERSION "0.0.1"
#define PLUGIN_DESCR                                                         \
    "A plugin to load libFestival.so. This loader works around a bug in "    \
    "festival caused by a symbol conflict with glibc that causes a SIGSEGV " \
    "and a crash during library initialization."

#ifndef PATH_MAX
#    define PATH_MAX 1024
#endif

#define MAX_LIBS 8                       /* max libs to preload */
#define MAX_DIRS 8                       /* max dirs to search */

#define CONFIG_DIRS "SRS_FESTIVAL_DIRS"  /* env. var. for dirs to search */
#define CONFIG_LIBS "SRS_FESTIVAL_LIBS"  /* env. var. for libs to preload */

#define DEFAULT_DIRS "/usr/lib64, /usr/lib, /lib64, /lib"
#define DEFAULT_LIBS "libeststring.so*, libestbase.so*, libestools.so*, " \
    "libFestival.so*"


typedef struct {
    const char *dirs[MAX_DIRS];          /* directories to search */
    int         ndir;                    /* number of directories */
    const char *libs[MAX_LIBS];          /* libraries to preload */
    int         nlib;                    /* number of libraries */
    void       *handles[MAX_LIBS];       /* library DSO handles */
} loader_t;

static loader_t loader;


static int check_config(loader_t *l)
{
    static char buf[PATH_MAX * 16];
    const char *evdir, *evlib, *s;
    char       *b;

    if ((evdir = getenv(CONFIG_DIRS)) == NULL)
        evdir = DEFAULT_DIRS;

    if ((evlib = getenv(CONFIG_LIBS)) == NULL)
        evlib = DEFAULT_LIBS;

    mrp_log_info("Directories to search: %s.", evdir);
    mrp_log_info("Libraries to preaload: %s.", evlib);

    b = buf;

    s = evdir;
    while (*s) {
        while (*s == ' ' || *s == '\t' || *s == ',' || *s == ':')
            s++;

        l->dirs[l->ndir++] = b;

        while (*s && *s != ' ' && *s != '\t' && *s != ',' && *s != ':') {
            *b++ = *s++;

            if (b - buf >= sizeof(buf) - 1) {
                errno = ENOBUFS;
                return -1;
            }
        }

        if (b - buf >= sizeof(buf) - 1) {
            errno = ENOBUFS;
            return -1;
        }

        *b++ = '\0';
        mrp_debug("added preload search dir '%s'...", l->dirs[l->ndir - 1]);
    }


    s = evlib;
    while (*s) {
        while (*s == ' ' || *s == '\t' || *s == ',' || *s == ':')
            s++;

        l->libs[l->nlib++] = b;

        while (*s && *s != ' ' && *s != '\t' && *s != ',' && *s != ':') {
            *b++ = *s++;

            if (b - buf >= sizeof(buf) - 1) {
                errno = ENOBUFS;
                return -1;
            }
        }

        if (b - buf >= sizeof(buf) - 1) {
            errno = ENOBUFS;
            return -1;
        }

        *b++ = '\0';

        mrp_debug("added preload lib '%s'...", l->libs[l->nlib - 1]);
    }

    return 0;
}


static char *find_matching(char *buf, size_t size, const char *dir,
                           const char *lib)
{
    char           pattern[PATH_MAX], path[PATH_MAX], *p;
    const char    *l;
    int            len, prfx, match, ok;
    DIR           *dp;
    struct dirent *de;
    regex_t        re;
    regmatch_t     rm;

    if (strchr(lib, '*') == NULL && strchr(lib, '?') == NULL) {
        if (snprintf(buf, size, "%s/%s", dir, lib) >= size)
            return NULL;
        if (access(buf, R_OK) == 0)
            return buf;
        else
            return NULL;
    }

    if ((dp = opendir(dir)) == NULL)
        return NULL;

    p    = pattern;
    l    = lib;
    len  = size;
    prfx = -1;
    while (len > 1 && *l) {
        switch (*l) {
        case '?':
        case '*':
            if (len <= 3)
                goto fail;

            if (prfx < 0)
                prfx = (p - pattern);

            *p++ = '.';
            *p++ = *l++;
            len -= 2;
            break;

        case '.':
            if (len <= 3)
                goto fail;

            if (prfx < 0)
                prfx = (p - pattern);

            *p++ = '\\';
            *p++ = *l++;
            len -= 2;
            break;

        default:
            *p++ = *l++;
            len--;
        }
    }

    *p = '\0';

    mrp_debug("regex pattern to match: '%s'", pattern);

    if (regcomp(&re, pattern, REG_NOSUB) != 0)
        goto fail;

    while ((de = readdir(dp)) != NULL) {
        if (de->d_type == DT_REG) {
        check_match:
            if (prfx > 0 && strncmp(de->d_name, pattern, prfx) != 0)
                continue;

            if (regexec(&re, de->d_name, 1, &rm, 0) == 0) {
                ok = snprintf(buf, size, "%s/%s", dir, de->d_name) < size;

                closedir(dp);
                regfree(&re);

                return ok ? buf : NULL;
            }
        }
        else if (de->d_type == DT_LNK) {
            struct stat st;

            snprintf(path, sizeof(path), "%s/%s", dir, de->d_name);

            if (stat(path, &st) == 0 && S_ISREG(st.st_mode))
                goto check_match;
        }
    }

    closedir(dp);
    regfree(&re);

    return NULL;

 fail:
    if (dp != NULL)
        closedir(dp);
    return NULL;
}


static const char *load_libs(loader_t *l)
{
    char  path[PATH_MAX], *err;
    void *h;
    int   i, j;

    for (i = 0; i < l->nlib; i++) {
        for (j = 0; j < l->ndir; j++) {
            mrp_log_info("Looking for %s in %s...", l->libs[i], l->dirs[j]);

            if (find_matching(path, sizeof(path), l->dirs[j], l->libs[i])) {
                h = dlopen(path, RTLD_NOW | RTLD_GLOBAL | RTLD_DEEPBIND);

                if (h != NULL) {
                    mrp_log_info("Preloaded %s.", path);
                    l->handles[i] = h;
                    break;
                }
                else {
                    err = dlerror();
                    mrp_log_warning("Failed to load %s (error: %s).",
                                    l->libs[i], err ? err : "unknown");
                }
            }
        }

        if (l->handles[i] == NULL) {
            mrp_log_error("Failed to preload %s.", l->libs[i]);
            return l->libs[i];
        }
    }

    return NULL;
}


static void unload_libs(loader_t *l)
{
    int i;

    for (i = 0; i < l->nlib; i++)
        if (l->handles[i] != NULL) {
            dlclose(l->handles[i]);
            l->handles[i] = NULL;
        }
}


static int create_loader(srs_plugin_t *plugin)
{
    const char *failed;

    MRP_UNUSED(plugin);

    mrp_clear(&loader);

    if (check_config(&loader) != 0) {
        mrp_log_error("Failed to get configuration (%d: %s).", errno,
                      strerror(errno));
        return FALSE;
    }

    if ((failed = load_libs(&loader)) != NULL) {
        mrp_log_error("Failed to preload library '%s'.", failed);
        return FALSE;
    }
    else
        return TRUE;
}


static int config_loader(srs_plugin_t *plugin, srs_cfg_t *settings)
{
    MRP_UNUSED(plugin);
    MRP_UNUSED(settings);

    return TRUE;
}


static int start_loader(srs_plugin_t *plugin)
{
    return TRUE;
}


static void stop_loader(srs_plugin_t *plugin)
{
    MRP_UNUSED(plugin);
}


static void destroy_loader(srs_plugin_t *plugin)
{
    MRP_UNUSED(plugin);

    unload_libs(&loader);
}


SRS_DECLARE_PLUGIN(PLUGIN_NAME, PLUGIN_DESCR, PLUGIN_AUTHORS, PLUGIN_VERSION,
                   create_loader, config_loader, start_loader, stop_loader,
                   destroy_loader);
