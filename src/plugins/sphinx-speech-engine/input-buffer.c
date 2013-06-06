#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

#include <sphinxbase/err.h>

#include <pocketsphinx.h>

#include <murphy/common/mm.h>
#include <murphy/common/log.h>

#include "input-buffer.h"
#include "options.h"
#include "decoder-set.h"
#include "filter-buffer.h"

static int32 ad_buffer_read(ad_rec_t *ud, int16 *buf, int32 reqlen);


int input_buffer_create(context_t *ctx)
{
    options_t *opts;
    input_buf_t *inpbuf;

    if (!ctx || !(opts = ctx->opts)) {
        errno = EINVAL;
        return -1;
    }

    if (!(inpbuf = mrp_allocz(sizeof(input_buf_t))))
        return -1;

    inpbuf->ad.sps = opts->rate;
    inpbuf->ad.bps = 1 * sizeof(int16_t); /* for MONO + PA_SAMPLE_S16LE */

    if (!(inpbuf->cont = cont_ad_init((ad_rec_t *)inpbuf, ad_buffer_read))) {
        mrp_log_error("cont_ad_init() failed");
        goto failed;
    }

    inpbuf->ctx = ctx;

    ctx->inpbuf = inpbuf;

    return 0;

 failed:
    mrp_free(inpbuf);
    return -1;
}

void input_buffer_destroy(context_t *ctx)
{
    input_buf_t *inpbuf;

    if (ctx && (inpbuf = ctx->inpbuf)) {
        ctx->inpbuf = NULL;

        cont_ad_close(inpbuf->cont);
        mrp_free(inpbuf->buf);

        mrp_free(inpbuf);
    }
}

int input_buffer_initialize(context_t *ctx, size_t size, size_t minreq)
{
    options_t *opts;
    input_buf_t *inpbuf;

    if (!ctx || !(opts = ctx->opts) || !(inpbuf = ctx->inpbuf) ||
        !(inpbuf->buf = mrp_alloc(size)))
        return -1;

    inpbuf->max = size;
    inpbuf->len = 0;
    inpbuf->minreq = minreq;

    if (ctx->verbose) {
        mrp_debug("input buffer length: %u byte (%.3lf sec), "
                  "min. request %u byte (%.3lf sec)",
                  size, (double)(size/sizeof(int16)) / (double)opts->rate,
                  minreq, (double)(minreq/sizeof(int16)) / (double)opts->rate);
    }

    return 0;
}


void input_buffer_process_data(context_t *ctx, const void *buf, size_t len)
{
    decoder_set_t *decset;
    decoder_t *dec;
    input_buf_t *inpbuf;
    filter_buf_t *filtbuf;
    cont_ad_t *cont;
    int32 l, max, rem;
    uint32_t minreq;
    size_t maxlen;
    size_t totlen;
    size_t extra;
    size_t move;

    if (!ctx || !(decset = ctx->decset) || !(dec = decset->curdec) ||
        !(inpbuf = ctx->inpbuf) || !(cont = inpbuf->cont) ||
        !(filtbuf = ctx->filtbuf))
        return;

    if (inpbuf->calibrated)
        minreq = inpbuf->minreq;
    else
        minreq = cont_ad_calib_size(cont) * sizeof(int16);

    maxlen = inpbuf->max;

    if ((totlen = len + inpbuf->len) > maxlen) {
        extra = totlen - maxlen;

        mrp_log_error("input buffer overflow (%u bytes). "
                      "throwing away extra bytes", extra);

        if (extra > maxlen) {
            buf += (len - (len % maxlen));
            len = maxlen;
        }       
        else {
            inpbuf->len -= extra;
            memmove(inpbuf->buf, inpbuf->buf + extra, inpbuf->len);
        }
    }

    memcpy(inpbuf->buf + inpbuf->len, buf, len);

    if ((inpbuf->len += len) < minreq)
        return;

    if (ctx->verbose)
        mrp_debug("processing %u byte input data", inpbuf->len);

    if (!inpbuf->calibrated) {
        if (cont_ad_calib(cont) < 0) {
            mrp_log_error("failed to calibrate");
            inpbuf->len = 0;    /* try again ... */
            return;
        }

        inpbuf->calibrated = true;
        filtbuf->ts = cont->read_ts;

        if (ctx->verbose || 1)
            mrp_log_info("Successfully calibrated @ %u", filtbuf->ts);

        filter_buffer_purge(ctx, -1);
    }

    filter_buffer_process_data(ctx);
}


static int32 ad_buffer_read(ad_rec_t *ud, int16 *buf, int32 reqlen)
{
    input_buf_t *inpbuf = (input_buf_t *)ud;
    context_t *ctx = inpbuf->ctx;
    size_t len = reqlen * sizeof(int16);

    if (!inpbuf->max || !inpbuf->buf)
        len = 0;
    else {
        if (len > inpbuf->len)
            len = inpbuf->len;

        if (len > 0) {
            memcpy(buf, inpbuf->buf, len);
            inpbuf->len -= len;

            if (inpbuf->len)
                memmove(inpbuf->buf, inpbuf->buf + len, inpbuf->len);
        }
    }
    
    if ((len % sizeof(int16)))
        mrp_log_error("%s(): odd buffer size %u", __FUNCTION__, len);

    return (int32)(len / sizeof(int16));
}



/*
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 *
 */
