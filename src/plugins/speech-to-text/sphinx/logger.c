#include <stdio.h>
#include <unistd.h>
#define __USE_GNU                        /* F_SETPIPE_SZ */
#include <fcntl.h>
#include <string.h>
#include <errno.h>

#include <murphy/common/log.h>
#include <murphy/common/mainloop.h>

#include "sphinx-plugin.h"

#define SPHINX_INFO   "INFO: "
#define SPHINX_ERROR  "ERROR: "
#define SPHINX_WARN   "WARNING: "
#define SPHINX_SYSERR "SYSTEM_ERROR: "
#define SPHINX_FATAL  "FATAL_ERROR: "

#define RD 0
#define WR 1

typedef struct {
    int             fd[2];               /* log message pipe */
    FILE           *fp;                  /* log write stream */
    mrp_io_watch_t *w;                   /* log I/O watch */
    char            buf[4096];           /* log line buffer */
    ssize_t         n;
} logger_t;


static ssize_t pull_log(logger_t *logger)
{
    char    *p;
    ssize_t  n, l;

    p = logger->buf + logger->n;
    l = sizeof(logger->buf) - logger->n - 1;

    n = read(logger->fd[RD], p, l);

    if (n <= 0)
        return n;

    p[n] = '\0';
    logger->n += n;

    return logger->n;
}


static void push_log(logger_t *logger)
{
    char    *b, *e, *lb, *le, *file, lvl, name[1024];
    int      line, len, nlen;
    ssize_t  n;

    name[sizeof(name) - 1] = '\0';

    lvl  = 0;
    file = NULL;

    while (logger->n > 0) {
        b = logger->buf;

        if (!strncmp(b, SPHINX_INFO  , len = sizeof(SPHINX_INFO  ) - 1) ||
            !strncmp(b, SPHINX_WARN  , len = sizeof(SPHINX_WARN  ) - 1) ||
            !strncmp(b, SPHINX_ERROR , len = sizeof(SPHINX_ERROR ) - 1) ||
            !strncmp(b, SPHINX_SYSERR, len = sizeof(SPHINX_SYSERR) - 1) ||
            !strncmp(b, SPHINX_FATAL , len = sizeof(SPHINX_FATAL ) - 1)) {
            lvl  = *b;
            b   += len;
            lb  = strchr(b, '(');
        }
        else
            lvl = 0;

        if ((e = strchr(b, '\n')) == NULL) {
            if (logger->n >= sizeof(logger->buf) - 1) {
                mrp_log_warning("Discarding too long sphinx log buffer.");
                logger->n = 0;
            }
            return;
        }

        if (lb != NULL) {
            line = (int)strtoul(lb + 1, &le, 10);

            if (lb != NULL && *le == ')') {
                nlen = lb - b;
                snprintf(name, sizeof(name) - 1, "%*.*s", nlen, nlen, b);
                file = name;
                b = le + 1;
                if (b[0] == ':' && b[1] == ' ')
                    b += 2;
            }
        }
        else {
            if (file == NULL)
                file = "<unknown-file>";
        }

        n = e - b;

        switch (lvl) {
        case 'I':
        default:
            mrp_debug_at("sphinx", line, file, "%*.*s", n, n, b);
            break;
        case 'W':
            mrp_log_msg(MRP_LOG_WARNING, file, line, "sphinx", "%*.*s", n, n, b);
            break;
        case 'E':
        case 'S':
        case 'F':
            mrp_log_msg(MRP_LOG_ERROR, file, line, "sphinx", "%*.*s", n, n, b);
            break;
        }

        b = e + 1;
        n = logger->n - (b - logger->buf);

        if (n <= 0)
            logger->n = 0;
        else {
            memmove(logger->buf, b, n);
            logger->n = n;
            logger->buf[n] = '\0';
        }
    }
}


static void log_cb(mrp_io_watch_t *w, int fd, mrp_io_event_t events,
                   void *user_data)
{
    logger_t *logger = (logger_t *)user_data;

    MRP_UNUSED(fd);

    if (events & MRP_IO_EVENT_IN)
        while (pull_log(logger) > 0)
            push_log(logger);

    if (events & MRP_IO_EVENT_HUP)
        mrp_del_io_watch(w);
}


FILE *logger_create(context_t *ctx)
{
    static logger_t  logger = { { -1, -1 }, NULL, 0 };
    mrp_mainloop_t  *ml     = plugin_get_mainloop(ctx->plugin);
    mrp_io_event_t   events = MRP_IO_EVENT_IN | MRP_IO_EVENT_HUP;

    if (logger.fp != NULL)
        return logger.fp;

    if (pipe(logger.fd) < 0) {
        mrp_log_error("Failed to create sphinx logging pipe (error %d: %s).",
                      errno, strerror(errno));
        goto fail;
    }

    if ((logger.fp = fdopen(logger.fd[WR], "w")) == NULL)
        goto fail;

    setvbuf(logger.fp, NULL, _IOLBF, 0);

    fcntl(logger.fd[WR], F_SETPIPE_SZ, 512 * 1024);
    fcntl(logger.fd[WR], F_SETFL, O_NONBLOCK);
    fcntl(logger.fd[RD], F_SETFL, O_NONBLOCK);

    logger.w = mrp_add_io_watch(ml, logger.fd[RD], events, log_cb, &logger);

    if (logger.w != NULL)
        return logger.fp;

    /* fallthru */

 fail:
    close(logger.fd[0]);
    close(logger.fd[1]);
    if (logger.fp != NULL)
        fclose(logger.fp);

    logger.fd[0] = -1;
    logger.fd[1] = -1;
    logger.fp = NULL;

    return NULL;
}
