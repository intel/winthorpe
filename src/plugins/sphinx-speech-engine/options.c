#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

#include <murphy/common/mm.h>
#include <murphy/common/log.h>

#include "options.h"

#define DEFAULT_HMM  "/usr/share/pocketsphinx/model/hmm/en_US/hub4wsj_sc_8k"
#define DEFAULT_LM   "/usr/share/pocketsphinx/model/lm/en_US/wsj0vp.5000.DMP"
#define DEFAULT_DICT "/usr/share/pocketsphinx/model/lm/en_US/cmu07a.dic"

static int add_decoder(int, srs_cfg_t *, const char *,
                       size_t *, options_decoder_t **pdecs);
static int print_decoders(size_t, options_decoder_t *, int, char *);


int options_create(context_t *ctx, int ncfg, srs_cfg_t *cfgs)
{
    options_t *opts;
    srs_cfg_t *cfg;
    const char *key;
    const char *value;
    char *e;
    bool verbose;
    int i;
    int sts;
    size_t pfxlen;
    size_t ndec;
    options_decoder_t *decs;
    char buf[65536];

    if (!ctx) {
        errno = EINVAL;
        return -1;
    }

    pfxlen = strlen(SPHINX_PREFIX);

    if (!(opts = mrp_allocz(sizeof(options_t))) ||
        !(decs = mrp_allocz(sizeof(options_decoder_t))))
        return -1;

    ndec = 1;
    decs->name = mrp_strdup("default");
    decs->hmm = mrp_strdup(DEFAULT_HMM);
    decs->lm = mrp_strdup(DEFAULT_LM);
    decs->dict = mrp_strdup(DEFAULT_DICT);
    decs->fsg = NULL;

    opts->srcnam = NULL;
    opts->audio = NULL;
    opts->logfn = mrp_strdup("/dev/null");
    opts->topn = 12;
    opts->rate = 16000;
    opts->silen = 1.0;

    verbose = false;
    sts = 0;

    for (i = 0;  i < ncfg;  i++) {
        cfg = cfgs + i;
        key = cfg->key + pfxlen;
        value = cfg->value;

        if (!strncmp(cfg->key, SPHINX_PREFIX, pfxlen)) {

            switch (key[0]) {

            case 'd':
                if (!strcmp(key, "dict")) {
                    mrp_free((void *)decs->dict);
                    decs->dict = mrp_strdup(value);
                }
                else if (!strcmp(key, "decoder")) {
                    add_decoder(ncfg, cfgs, value, &ndec, &decs);
                }
                break;

            case 'f':
                if (!strcmp(key, "fsg")) {
                    mrp_free((void *)decs->fsg);
                    decs->fsg = mrp_strdup(value);
                }
                break;

            case 'h':
                if (!strcmp(key, "hmm")) {
                    mrp_free((void *)decs->hmm);
                    decs->hmm = mrp_strdup(value);
                }
                break;

            case 'l':
                if (!strcmp(key, "lm")) {
                    mrp_free((void *)decs->lm);
                    decs->lm = mrp_strdup(value);
                }
                break;

            case 'p':
                if (!strcmp(key, "pulsesrc")) {
                    mrp_free((void *)opts->srcnam);
                    opts->srcnam = mrp_strdup(value);
                }
                break;

            case 'r':
                if (!strcmp(key, "record")) {
                    mrp_free((void *)opts->audio);
                    opts->audio = mrp_strdup(value);
                }
                break;

            case 's':
                if (!strcmp(key, "samplerate")) {
                    opts->rate = strtoul(value, &e, 10);
                    if (e[0] || e == value ||
                        opts->rate < 8000 || opts->rate > 4800)
                    {
                        mrp_log_error("invalid value %s for samplerate",value);
                        sts = -1;
                    }
                }
                break;

            case 't':
                if (!strcmp(key, "topn")) {
                    opts->topn = strtoul(value, &e, 10);
                    if (e[0] || e == value ||
                        opts->topn < 1 || opts->topn > 100)
                    {
                        mrp_log_error("invalid value %s for topn", value);
                        sts = -1;
                    }
                }
                break;

            default:
                // cfg->used = FALSE;
                break;

            } /* switch key */
        }
    } /* for cfg */

    opts->ndec = ndec;
    opts->decs = decs;

    if (sts == 0) {
        print_decoders(opts->ndec, opts->decs, sizeof(buf), buf);

        mrp_log_info("topn: %u\n"
                     "   pulseaudio source name: %s\n"
                     "   sample rate: %.1lf KHz\n"
                     "   audio recording file: %s\n"
                     "%s",
                     opts->topn,
                     opts->srcnam ? opts->srcnam : "<default-source>",
                     (double)opts->rate / 1000.0,
                     opts->audio,
                     buf);
    }

    ctx->opts = opts;
    ctx->verbose = verbose;

    return sts;
}

void options_destroy(context_t *ctx)
{
    options_t *opts;
    options_decoder_t*dec;
    size_t i;

    if (ctx && (opts = ctx->opts)) {
        ctx->opts = NULL;

        if (opts->decs) {
            for (i = 0; i < opts->ndec;  i++) {
                dec = opts->decs + i;
                mrp_free((void *)dec->name);
                mrp_free((void *)dec->hmm);
                mrp_free((void *)dec->lm);
                mrp_free((void *)dec->dict);
                mrp_free((void *)dec->fsg);
            }
        }

        mrp_free((void *)opts->srcnam);
        mrp_free((void *)opts->audio);
        mrp_free((void *)opts->logfn);

        mrp_free(opts);
    }
}

static int add_decoder(int ncfg,
                       srs_cfg_t *cfgs,
                       const char *name,
                       size_t *pndec,
                       options_decoder_t **pdecs)
{
    int i;
    srs_cfg_t *cfg;
    const char *key;
    const char *value;
    size_t pfxlen;
    char pfx[1024];
    options_decoder_t *decs, *dec;
    size_t ndec, size;
    const char *hmm = NULL;
    const char *lm = NULL;
    const char *dict = NULL;
    const char *fsg = NULL;

    pfxlen = snprintf(pfx, sizeof(pfx), SPHINX_PREFIX "%s.", name);

    for (i = 0;  i < ncfg;  i++) {
        cfg = cfgs + i;
        key = cfg->key + pfxlen;
        value = cfg->value;

        if (!strncmp(cfg->key, pfx, pfxlen)) {

            switch (key[0]) {

            case 'd':
                if (!strcmp(key, "dict"))
                    dict = value;
                break;

            case 'f':
                if (!strcmp(key, "fsg"))
                    fsg = value;
                break;

            case 'h':
                if (!strcmp(key, "hmm"))
                    hmm = value;
                break;

            case 'l':
                if (!strcmp(key, "lm"))
                    lm = value;
                break;
            }
        }
    }

    ndec = *pndec;
    size = sizeof(options_decoder_t) * (ndec + 1);

    if (lm && dict && (decs = mrp_realloc(*pdecs, size))) {
        dec = decs + ndec++;

        dec->name = mrp_strdup(name);
        dec->hmm  = hmm ? mrp_strdup(hmm) : NULL;
        dec->lm   = mrp_strdup(lm);
        dec->dict = mrp_strdup(dict);
        dec->fsg  = fsg ? mrp_strdup(fsg) : NULL;

        *pndec = ndec;
        *pdecs = decs;

        return 0;
    }

    return -1;
}

static int print_decoders(size_t ndec,
                          options_decoder_t *decs,
                          int len,
                          char *buf)
{
    options_decoder_t *dec;
    char *p, *e;
    size_t i;

    e = (p = buf) + len;

    for (i = 0;  i < ndec && p < e;  i++) {
        dec = decs + i;

        p += snprintf(p, e-p,
                      "   decoder\n"
                      "      name : '%s'\n"
                      "      acoustic model directory: %s\n"
                      "      language model file: %s\n"
                      "      dictionary file: %s\n"
                      "      model: %s%s\n",
                      dec->name, dec->hmm ? dec->hmm : "<default>",
                      dec->lm, dec->dict,
                      dec->fsg ? "fsg - ":"acoustic", dec->fsg ? dec->fsg:"");
    }

    return p - buf;
}


/*
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 *
 */
