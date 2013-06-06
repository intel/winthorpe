#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

#include <sphinxbase/err.h>
#include <sphinxbase/ad.h>
#include <sphinxbase/cont_ad.h>

#include <pocketsphinx.h>

#include <murphy/common/mm.h>
#include <murphy/common/log.h>

#include "decoder-set.h"
#include "options.h"
#include "utterance.h"




int decoder_set_create(context_t *ctx)
{
    options_t *opts;
    decoder_set_t *decset;
    int sts;

    if (!ctx || !(opts = ctx->opts)) {
        errno = EINVAL;
        return -1;
    }

    if (!(decset = mrp_allocz(sizeof(decoder_set_t))))
        return -1;

    ctx->decset = decset;

    sts = decoder_set_add(ctx, "default", opts->hmm, opts->lm,
                          opts->dict, opts->fsg, opts->topn);
    if (sts  < 0) {
        mrp_log_error("failed to create default decoder");
        errno = EIO;
        return -1;
    }

    decset->curdec = decset->decs;

    return 0;
}

void decoder_set_destroy(context_t *ctx)
{
    decoder_set_t *decset;
    decoder_t *dec;
    size_t i,j;

    if (ctx && (decset = ctx->decset)) {
        ctx->decset = NULL;

        for (i = 0;  i < decset->ndec;  i++) {
            dec = decset->decs + i;

            mrp_free((void *)dec->name);

            if (dec->ps)
                ps_free(dec->ps);

            for (j = 0; j < dec->nfsg; j++)
                mrp_free((void *)dec->fsgs[j]);
            mrp_free((void *)dec->fsgs);
        }

        mrp_free(decset->decs);

        mrp_free(decset);
    }
}

int decoder_set_add(context_t *ctx, const char *decoder_name,
                    const char *hmm, const char *lm,
                    const char *dict, const char *fsg,
                    uint32_t topn)
{
#define FSG_NAMES_MAX 255

    static const arg_t arg_defs[] = {
        POCKETSPHINX_OPTIONS,
        CMDLN_EMPTY_OPTION
    };

    options_t *opts;
    decoder_set_t *decset;
    decoder_t *dec;
    const char *dupnam;
    cmd_ln_t *cfg;
    ps_decoder_t *ps;
    size_t new_size;
    fsg_set_t *set;
    fsg_set_iter_t *sit;
    fsg_model_t *model;
    const char *modnam;
    const char *fsgs[FSG_NAMES_MAX + 1];
    size_t nfsg;
    size_t curidx;

    if (!ctx || !(opts = ctx->opts) || !(decset = ctx->decset)) {
        errno = EINVAL;
        return -1;
    }

    if (!lm || !dict) {
        errno = ENOENT;
        return -1;
    }

    if (!hmm)
        hmm = opts->hmm;

    curidx = decset->curdec - decset->decs;
    new_size = sizeof(decoder_t) * (decset->ndec + 2);

    if (!(decset->decs = realloc(decset->decs, new_size)) ||
        !(dupnam = mrp_strdup(decoder_name)))
    {
        return -1;
    }


    memset(decset->decs + sizeof(decoder_t) * decset->ndec, 0,
           sizeof(decoder_t) * 2);

    dec = decset->decs + decset->ndec;
    decset->curdec = decset->decs + curidx;

    if (!(cfg = cmd_ln_init(NULL, arg_defs, 0, NULL))) {
        mrp_log_error("failed to create cmd line struct");
        return -1;
    }

    cmd_ln_set_str_r(cfg, "-hmm", hmm);
    cmd_ln_set_str_r(cfg, "-lm", lm);
    cmd_ln_set_str_r(cfg, "-dict", dict);
    cmd_ln_set_int_r(cfg, "-topn", topn);
    cmd_ln_set_float_r(cfg, "-samprate", (double)opts->rate);
    cmd_ln_set_boolean_r(cfg, "-verbose", ctx->verbose);

    if (opts->logfn)
        cmd_ln_set_str_r(cfg, "-logfn", opts->logfn);

    if (fsg)
        cmd_ln_set_str_r(cfg, "-fsg", opts->fsg);
    
    if (!(ps = ps_init(cfg)))
        return -1;

    if (!fsg)
        nfsg = 0;
    else {
        if (!(set = ps_get_fsgset(ps))) {
            mrp_log_error("can't find fsg models");
            ps_free(ps);
            return -1;
        }

        mrp_log_info("found fsg models:");

        for (nfsg = 0, sit = fsg_set_iter(set);
             nfsg < FSG_NAMES_MAX && sit;
             nfsg++, sit = fsg_set_iter_next(sit))
        {
            model = fsg_set_iter_fsg(sit);
            modnam = fsg_model_name(model);
            fsgs[nfsg] = mrp_strdup(modnam ? modnam : "<anonymous>");
            
            mrp_log_info("   %s", fsgs[nfsg]);
        }

        if (!nfsg) {
            mrp_log_info("    <none>");
            ps_free(ps);
            return -1;
        }
    }

    dec->name = dupnam;
    dec->cfg  = cfg;
    dec->ps   = ps;
    dec->fsgs = mrp_allocz(sizeof(const char *) * (nfsg + 1));
    dec->nfsg = nfsg;
    dec->utproc = nfsg ? UTTERANCE_PROCESSOR_FSG:UTTERANCE_PROCESSOR_ACOUSTIC;
    dec->utid = 1;

    if (!dec->fsgs) {
        mrp_log_error("No memory");
        return -1;
    }

    if (nfsg > 0)
        memcpy((void *)dec->fsgs, (void *)fsgs, sizeof(const char *) * nfsg);

    decset->ndec++;

    return 0;

#undef FSG_NAMES_MAX
}

int decoder_set_use(context_t *ctx, const char *decoder_name)
{
    decoder_set_t *decset;
    decoder_t *d;

    if (!ctx || !(decset = ctx->decset))
        return -1;

    if (!decoder_name) {
        decset->curdec = decset->decs;
        return -1;
    }

    for (d = decset->decs;  d->name;  d++) {
        if (!strcmp(decoder_name, d->name)) {
            if (ctx->verbose)
                mrp_debug("switching to decoder '%s'", decoder_name);

            decset->curdec = d;

            return 0;
        }
    }

    mrp_log_error("unable to set decoder '%s': can't find it", decoder_name);

    return -1;
}

const char *decoder_set_name(context_t *ctx)
{
    decoder_set_t *decset;
    decoder_t *dec;

    if (!ctx || !(decset = ctx->decset) || !(dec = decset->curdec))
        return "<unknown>";

    return dec->name;
}


/*
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 *
 */
