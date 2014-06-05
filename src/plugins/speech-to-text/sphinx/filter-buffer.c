#include <sys/types.h>
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>

#include <sphinxbase/err.h>
#include <sphinxbase/ad.h>
#include <sphinxbase/cont_ad.h>

#include <pocketsphinx.h>

#include <murphy/common/mm.h>
#include <murphy/common/log.h>

#include "filter-buffer.h"
#include "input-buffer.h"
#include "options.h"
#include "decoder-set.h"
#include "utterance.h"

#define INJECTED_SILENCE 10     /* injected silence in frames */

static int open_file_for_recording(const char *);


int filter_buffer_create(context_t *ctx)
{
    options_t *opts;
    decoder_set_t *decset;
    decoder_t *dec;
    cmd_ln_t *cfg;
    uint32_t rate;
    int32 fps;
    int32_t frlen;
    filter_buf_t *filtbuf;

    if (!ctx || !(opts = ctx->opts) ||
        !(decset = ctx->decset) || !(dec = decset->decs) ||
        !(cfg = dec->cfg) || !(filtbuf = mrp_allocz(sizeof(filter_buf_t))))
        return -1;

    rate = opts->rate;
    fps =  cmd_ln_int32_r(cfg, "-frate");
    frlen = rate / (double)fps;

    filtbuf->len = 0;
    filtbuf->frlen = frlen;
    filtbuf->fdrec = open_file_for_recording(opts->audio);

    ctx->filtbuf = filtbuf;

    return 0;
}

void filter_buffer_destroy(context_t *ctx)
{
    filter_buf_t *filtbuf;

    if (ctx && (filtbuf = ctx->filtbuf)) {
        ctx->filtbuf = NULL;

        mrp_free(filtbuf->buf);

        free(filtbuf);
    }
}

void filter_buffer_initialize(context_t *ctx,
                              int32_t bufsiz,
                              int32_t highwater_mark,
                              int32_t silen)
{
    options_t *opts;
    filter_buf_t *filtbuf;
    uint32_t rate;
    int32_t frlen;
    int32_t hwm;
    size_t silence;

    if (!ctx || !(opts = ctx->opts) || !(filtbuf = ctx->filtbuf))
        return;

    rate = opts->rate;
    frlen = filtbuf->frlen;
    bufsiz = (bufsiz + (frlen - 1)) / frlen * frlen;
    hwm = (highwater_mark + (frlen - 1)) / frlen * frlen;
    silence = INJECTED_SILENCE * frlen;

    filtbuf->buf = mrp_alloc((bufsiz + silence) * sizeof(int16_t));
    filtbuf->max = bufsiz;
    filtbuf->hwm = hwm;
    filtbuf->silen = silen;

    if (ctx->verbose) {
        mrp_debug("frame length %d samples", filtbuf->frlen);
        mrp_debug("filter buffer size %u samples (%.3lf sec); "
                  "high-water mark %u samples (%.3lf sec)",
                  filtbuf->max, (double)filtbuf->max / (double)rate,
                  filtbuf->hwm, (double)filtbuf->hwm / (double)rate);
        mrp_debug("silence detection window %d samples (%.3lf sec)",
                  filtbuf->silen, (double)filtbuf->silen / (double)rate);
    }
}

bool filter_buffer_is_empty(context_t *ctx)
{
    filter_buf_t *filtbuf;
    bool empty;

    if (ctx && (filtbuf = ctx->filtbuf))
        empty = (filtbuf->len > 0) ? false : true;
    else
        empty = true;

    return empty;
}

void filter_buffer_purge(context_t *ctx, int32_t length)
{
    filter_buf_t *filtbuf;
    size_t size, offset, origlen, sillen;

    if (!ctx || !(filtbuf = ctx->filtbuf))
        return;

    if (length > 0)
        length++;

    if (length < 0 || length > filtbuf->len)
        length = filtbuf->len;

    if (length > 0) {
        if (length == filtbuf->len) {
            filtbuf->len = 0;     /* nothing to preserve */

            if (ctx->verbose)
                mrp_debug("purging buffer. nothing preserved");
        }
        else {
            sillen = INJECTED_SILENCE * filtbuf->frlen;
            origlen = filtbuf->len;
            filtbuf->len = filtbuf->len - length + sillen;

            if (ctx->verbose) {
                mrp_debug("purging buffer. %d samples preserved out of %u",
                              filtbuf->len, origlen);
            }

            offset = length;
            size = (origlen - offset) * sizeof(int16);

            memmove(filtbuf->buf + sillen, filtbuf->buf + offset, size);
            memset(filtbuf->buf, 0, sillen * sizeof(int16_t));
        }
    }
}

void filter_buffer_process_data(context_t *ctx)
{
    decoder_set_t *decset;
    decoder_t *dec;
    input_buf_t *inpbuf;
    filter_buf_t *filtbuf;
    cont_ad_t *cont;
    int32_t l, max, len;

    if (!ctx || !(decset = ctx->decset) || !(dec = decset->curdec) ||
        !(inpbuf = ctx->inpbuf) || !(cont = inpbuf->cont) ||
        !(filtbuf = ctx->filtbuf))
        return;

    len = 0;
    max = filtbuf->hwm - filtbuf->len;

    for (;;) {
        if (max <= 0)
            break;

        l = cont_ad_read(cont, filtbuf->buf + filtbuf->len + len, max);

        if (l <= 0)
            break;

        len += l;
        max -= l;
    }

    if (len > 0) {
        filtbuf->len += len;
        filtbuf->ts = cont->read_ts;

        if (ctx->verbose) {
            mrp_debug("got %u samples to filter buffer "
                      "(total size %u samples)", len, filtbuf->len);
        }

        utterance_start(ctx);

        if (filtbuf->len >= filtbuf->hwm)
            filter_buffer_utter(ctx, false);
    }
    else {
        if (dec->utter && (cont->read_ts - filtbuf->ts) > filtbuf->silen) {
            filter_buffer_utter(ctx, true);
            cont_ad_reset(cont);
            utterance_end(ctx);
        }
    }
}

void filter_buffer_utter(context_t *ctx, bool full_utterance)
{
    decoder_set_t *decset;
    decoder_t *dec;
    filter_buf_t *filtbuf;
    int sts, cnt, size;

    if (!ctx || !(decset = ctx->decset) || !(dec = decset->curdec) ||
        !(filtbuf = ctx->filtbuf))
        return;

    mrp_debug("utterance length %d samples", filtbuf->len);

    if (filtbuf->len > 0) {
        if (filtbuf->fdrec >= 0) {
            size = filtbuf->len * sizeof(int16);

            for (;;) {
                cnt = write(filtbuf->fdrec, filtbuf->buf, size);

                if (cnt != size) {
                    if (cnt < 0 && errno == EINTR)
                        continue;

                    mrp_log_error("failed to record samples (fd %d): %s",
                                  filtbuf->fdrec, strerror(errno));
                }

                break;
            }
        }

        sts = ps_process_raw(dec->ps, filtbuf->buf, filtbuf->len,
                             FALSE, full_utterance);
        if (sts < 0)
            mrp_log_error("Failed to process %d samples", filtbuf->len);
    }
}

int16_t *filter_buffer_dup(context_t *ctx,
                           int32_t start,
                           int32_t end,
                           size_t *ret_length)
{
    filter_buf_t *filtbuf;
    int16_t *dup;
    size_t len;


    if (!ctx || !(filtbuf = ctx->filtbuf))
        return NULL;

    if (start < 0 || end < 0 || start >= end || start >= filtbuf->len)
        return NULL;

    len = (end - start) * sizeof(int16_t);

    if (!(dup = mrp_alloc(len)))
        len = 0;
    else
        memcpy(dup, filtbuf->buf + start, len);

    if (ret_length)
        *ret_length = len / sizeof(int16_t);

    return dup;
}



static int open_file_for_recording(const char *path)
{
    int fd;

    if (!path)
        fd = -1;
    else {
        fd = open(path, O_RDWR | O_CREAT | O_TRUNC, 0644);

        if (fd < 0)
            mrp_log_error("can't open file '%s': %s", path, strerror(errno));
        else
            mrp_log_info("succesfully opened file '%s'", path);
    }

    return fd;
}

/*
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 *
 */
