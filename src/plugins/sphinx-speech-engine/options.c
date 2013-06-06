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


int options_create(context_t *ctx, int ncfg, srs_cfg_t *cfgs)
{
    options_t *opts;
    srs_cfg_t *cfg;
    const char *key;
    const char *value;
    char *e;
    int c;
    bool verbose;
    int i;
    int sts;
    size_t pfxlen;

    if (!ctx) {
        errno = EINVAL;
        return -1;
    }

    pfxlen = strlen(SPHINX_PREFIX);

    if (!(opts = mrp_allocz(sizeof(options_t))))
        return -1;

    opts->hmm = mrp_strdup(DEFAULT_HMM);
    opts->lm = mrp_strdup(DEFAULT_LM);
    opts->dict = mrp_strdup(DEFAULT_DICT);
    opts->fsg = NULL;
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
                    mrp_free((void *)opts->dict);
                    opts->dict = mrp_strdup(value);
                }
                break;

            case 'f':
                if (!strcmp(key, "fsg")) {
                    mrp_free((void *)opts->fsg);
                    opts->fsg = mrp_strdup(value);
                }
                break;

            case 'h':
                if (!strcmp(key, "hmm")) {
                    mrp_free((void *)opts->hmm);
                    opts->hmm = mrp_strdup(value);
                }
                break;

            case 'l':
                if (!strcmp(key, "lm")) {
                    mrp_free((void *)opts->lm);
                    opts->lm = mrp_strdup(value);
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
                cfg->used = FALSE;
                break;

            } /* switch key */
        }
    } /* for cfg */

    if (sts == 0) {
        mrp_log_info("directory for acoustic model: %s\n"
                     "   language model file: %s\n"
                     "   dictionary file: %s\n"
                     "   model: %s%s\n"
                     "   topn: %u\n"
                     "   pulseaudio source name: %s\n"
                     "   sample rate: %.1lf KHz\n"
                     "   audio recording file: %s",
                     opts->hmm, opts->lm, opts->dict,
                     opts->fsg?"fsg - ":"audio", opts->fsg?opts->fsg:"",
                     opts->topn,
                     opts->srcnam ? opts->srcnam : "<default-source>",
                     (double)opts->rate / 1000.0, opts->audio);
    }

    ctx->opts = opts;
    ctx->verbose = verbose;

    return sts;
}

void options_destroy(context_t *ctx)
{
    options_t *opts;

    if (ctx && (opts = ctx->opts)) {
        ctx->opts = NULL;

        mrp_free((void *)opts->hmm);
        mrp_free((void *)opts->lm);
        mrp_free((void *)opts->dict);
        mrp_free((void *)opts->fsg);
        mrp_free((void *)opts->srcnam);
        mrp_free((void *)opts->audio);
        mrp_free((void *)opts->logfn);

        mrp_free(opts);
    }
}


/*
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 *
 */
