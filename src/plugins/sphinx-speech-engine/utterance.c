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

#include "utterance.h"
#include "decoder-set.h"
#include "filter-buffer.h"


static void process_utterance(context_t *);
static void acoustic_processor(context_t *, utterance_t *,
                               candidate_t *, candidate_t **);
static void fsg_processor(context_t *, utterance_t *,
                          candidate_t *, candidate_t **);
static void print_utterance(context_t *, utterance_t *);

static candidate_t *candidate_equal(candidate_t *, candidate_t *);
static double candidate_quality(candidate_t *);
static uint32_t candidate_sort(candidate_t *, candidate_t **);

static bool wdeq(const char *, const char *);


void utterance_start(context_t *ctx)
{
    decoder_set_t *decset;
    decoder_t *dec;
    char utid[256];

    if (ctx && (decset = ctx->decset) && (dec = decset->curdec)) {
        if (!dec->utter) {
            snprintf(utid, sizeof(utid), "%07u-%s", dec->utid++, dec->name);
            ps_start_utt(dec->ps, mrp_strdup(utid));
            dec->utter = true;
        }
    }
}

void utterance_end(context_t *ctx)
{
    decoder_set_t *decset;
    decoder_t *dec;

    if (ctx && (decset = ctx->decset) && (dec = decset->curdec)) {
        if (dec->utter) {
            ps_end_utt(dec->ps);
            dec->utter = false;
            process_utterance(ctx);
        }
    }
}

static void process_utterance(context_t *ctx)
{
    decoder_set_t *decset;
    decoder_t *dec;
    utterance_t utt;
    candidate_t cands[CANDIDATE_MAX + 1];
    candidate_t *sorted[CANDIDATE_MAX + 1];
    int32_t purgelen;

    if (ctx && (decset = ctx->decset) && (dec = decset->curdec)) {

        switch (dec->utproc) {

        case UTTERANCE_PROCESSOR_ACOUSTIC:
            acoustic_processor(ctx, &utt, cands, sorted);
            goto processed;

        case UTTERANCE_PROCESSOR_FSG:
            fsg_processor(ctx, &utt, cands, sorted);
            goto processed;

        processed:
            if (ctx->verbose || 1)
                print_utterance(ctx, &utt);
            purgelen = plugin_utterance_handler(ctx, &utt);
            filter_buffer_purge(ctx, purgelen);
            break;

        default:
            break;
        }
    }
}

static void acoustic_processor(context_t *ctx,
                               utterance_t *utt,
                               candidate_t *cands,
                               candidate_t **sorted)
{
    decoder_set_t *decset;
    decoder_t *dec;
    logmath_t *lmath;
    const char *uttid;
    const char *hyp;
    int32 score;
    double prob;
    ps_nbest_t *nb;
    ps_seg_t *seg;
    ps_lattice_t *dag;
    ps_latnode_iter_t *it;
    ps_latlink_t *lnk;
    ps_latnode_t *nod;
    int32 start, end;
    size_t ncand, nsort;
    candidate_t *cand;
    word_t *wd;
    int32_t length, purgelen;

    if (!ctx || !(decset = ctx->decset) || !(dec = decset->curdec))
        return;

    lmath = ps_get_logmath(dec->ps);
    uttid = "<unknown>";
    hyp = ps_get_hyp(dec->ps, &score, &uttid);
    prob = logmath_exp(lmath, score);
    length = 0;

    if (prob < 0.00000001)
        prob = 0.00000001;

    for (nb  = ps_nbest(dec->ps, 0,-1, NULL,NULL), ncand = 0;
         nb != NULL;
         nb  = ps_nbest_next(nb))
    {
        if (ncand >= CANDIDATE_MAX-1) {
            break;
            ps_nbest_free(nb);
        }

        if ((seg  = ps_nbest_seg(nb, &score))) {
            while (seg && strcmp(ps_seg_word(seg), "<s>"))
                seg = ps_seg_next(seg);
            
            if (!seg)
                continue;
                
            ps_seg_frames(seg, &start, &end);
                
            cand = cands + ncand;
                
            cand->quality = logmath_exp(lmath, score) / prob;
            cand->nword = 0;
                
            length = 0;
                
            while ((seg = ps_seg_next(seg))) {
                if ((hyp = ps_seg_word(seg))) {
                    if (!strcmp(hyp, "</s>") ||
                        cand->nword >= CANDIDATE_WORD_MAX)
                    {
                        ncand++;
                        memset(cand+1, 0, sizeof(candidate_t));
                        ps_seg_frames(seg, &start, &end);
                        ps_seg_free(seg);
                        //printf("hyp=</s> ncand=%d\n", ncand);
                        length = end;
                        break;
                    }
                    else if (!strcmp(hyp, "<sil>")) {
                        ps_seg_frames(seg, &start, &end);
                        //printf("hyp=<sil> skip it\n");
                    }
                    else {
                        wd = cand->words + cand->nword++;
                        wd->word = hyp;
                        ps_seg_frames(seg, &wd->start, &wd->end);
                        //printf("hyp=%s (%d, %d) wd count %d\n",
                        //      wd->word, wd->start,wd->end, cand->nword); 
                    }
                }
            } /* while seg */
            
            if (!seg && cand->nword > 0) {
                ncand++;
                cand->quality *= 0.9; /* some penalty */
                memset(cand+1, 0, sizeof(candidate_t));
            }
            
            if (!length) {
                wd = cand->words + (cand->nword - 1);
                length = wd->end;
            }
        }
    } /* for nb */
    
    utt->id = uttid;
    utt->quality = prob;
    utt->length = length;
    utt->ncand = candidate_sort(cands, sorted);
    utt->cands = sorted;
}

static void fsg_processor(context_t *ctx,
                          utterance_t *utt,
                          candidate_t *cands,
                          candidate_t **sorted)
{
    decoder_set_t *decset;
    decoder_t *dec;
    logmath_t *lmath;
    const char *hyp;
    const char *uttid;
    int32_t score;
    double prob;
    candidate_t *cand;
    word_t *wd;
    ps_lattice_t *dag;
    ps_latlink_t *lnk;
    ps_latnode_t *nod;
    const char *word;
    int32_t start;
    int16 fef, lef;
    int32_t purgelen;

    if (!ctx || !(decset = ctx->decset) || !(dec = decset->curdec))
        return;

    lmath = ps_get_logmath(dec->ps);
    hyp = ps_get_hyp(dec->ps, &score, &uttid);
    prob  = logmath_exp(lmath, score);

    cand = cands;
    cand->quality = 1.0;
    cand->nword = 0;

    wd = NULL;

    if ((dag = ps_get_lattice(dec->ps))) {

        if ((lnk = ps_lattice_traverse_edges(dag, NULL, NULL))) {

            ps_latlink_nodes(lnk, &nod);

            if (nod && (word = ps_latnode_word(dag, nod)) && *word != '<') {
                wd = cand->words + cand->nword++;
                wd->word = word;
                wd->start = ps_latnode_times(nod, &fef, &lef);
                wd->end = (fef + lef) / 2;
            }

            goto handle_destination_node;

            while ((lnk  = ps_lattice_traverse_next(dag, NULL))) {

              handle_destination_node:
                nod = ps_latlink_nodes(lnk, NULL);

                if (nod && (word = ps_latnode_word(dag, nod)) && *word != '<'){
                    start = ps_latnode_times(nod, &fef, &lef);

                    if (wd && start < wd->end)
                        break;  /* just take one candidate */

                    if (!wd || !wdeq(word, wd->word)) {
                        wd = cand->words + cand->nword++;
                        wd->word = word;
                        wd->start = start;
                        wd->end = fef;
                    }
                }
            }
        }
    }

    sorted[0] = cands;
    sorted[1] = NULL;

    utt->id = uttid;
    utt->quality = prob < 0.00001 ? 0.00001 : prob;
    utt->length = dag ? ps_lattice_n_frames(dag) : 0;
    utt->ncand = 1;
    utt->cands = sorted;
}


static void print_utterance(context_t *ctx, utterance_t *utt)
{
    decoder_set_t *decset;
    decoder_t *dec;
    candidate_t *cand;
    word_t *wd;
    size_t i,j;

    if (ctx && (decset = ctx->decset) && (dec = decset->curdec)) {
        mrp_log_info("*** %15s  (%.4lf) %u candidates, length %u",
                     utt->id, utt->quality, utt->ncand, utt->length);

        for (i = 0; cand = utt->cands[i];  i++) {
            mrp_log_info("  (%.4lf) ----------------------", cand->quality);

            for (j = 0;  j < cand->nword;  j++) {
                wd = cand->words + j;
                mrp_log_info("           %d - %d  %s\n",
                             wd->start, wd->end, wd->word);
            }
        }

        mrp_log_info("           ----------------------\n");
    }
}

static candidate_t *candidate_equal(candidate_t *a, candidate_t *b)
{
    word_t *aw,*bw;
    size_t i,n;

    if (!a || !b)
        return NULL;

    if ((n = a->nword) != b->nword)
        return false;

    for (i = 0;  i < n;  i++) {
        aw = a->words + i;
        bw = b->words + i;

        if (!wdeq(aw->word, bw->word))
            return NULL;
    }

    return (a->quality > b->quality) ? a : b;
}

static double candidate_quality(candidate_t *cand)
{
    return cand ? cand->quality : 0.0;
}


static uint32_t candidate_sort(candidate_t *cands, candidate_t **sorted)
{
    candidate_t *c, **s;
    candidate_t *better_quality;
    size_t i,j,n;

    memset(sorted, 0, sizeof(candidate_t *) * (CANDIDATE_MAX + 1));

    for (i = n = 0;  i < CANDIDATE_MAX;  i++) {
        if (!(c = cands + i)->nword)
            break;

        for (j = 0;   j <= n;   j++) {
            s = sorted + j;

            if ((better_quality = candidate_equal(c, *s))) {
                *s = better_quality;
                break;
            }

            if (candidate_quality(c) > candidate_quality(*s)) {
                if (j < n) {
                    memmove(sorted + j+1, sorted + j,
                            sizeof(candidate_t *) * (n - j));
                }
                *s = c;
                n++;
                break;
            }
        }
    }

    return n;
}

static bool wdeq(const char *wd1, const char *wd2)
{
    const char *e1, *e2;
    int l1, l2, l;

    if (!wd1 || !wd2)
        return false;

    if (!strcmp(wd1, wd2))
        return true;

    if (*wd1 == *wd2) {
        l1 = (e1 = strchr(wd1, '(')) ? e1 - wd1 : 0;
        l2 = (e2 = strchr(wd2, '(')) ? e2 - wd2 : 0;

        if (l1 || l2) {
            if (l1 == l2 && !strncmp(wd1, wd2, l1))
                return true;
            if (l1 && !l2 && !strncmp(wd1, wd2, l1))
                return true;
            if (!l1 && l2 && !strncmp(wd1, wd2, l2))
                return true;
        }
    }

    return false;
}


/*
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 *
 */
