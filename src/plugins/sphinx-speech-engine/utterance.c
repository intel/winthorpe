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
static void acoustic_processor(context_t *, srs_srec_utterance_t *,
                               srs_srec_candidate_t *,srs_srec_candidate_t **);
static void fsg_processor(context_t *, srs_srec_utterance_t *,
                          srs_srec_candidate_t *, srs_srec_candidate_t **);
static void print_utterance(context_t *, srs_srec_utterance_t *);

static srs_srec_candidate_t *candidate_equal(srs_srec_candidate_t *,
                                             srs_srec_candidate_t *);
static double candidate_score(srs_srec_candidate_t *);
static uint32_t candidate_sort(srs_srec_candidate_t *,srs_srec_candidate_t **);

static bool tkneq(const char *, const char *);
static const char *tknbase(const char *token);


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
    srs_srec_utterance_t utt;
    srs_srec_token_t token_pool[CANDIDATE_MAX * (CANDIDATE_TOKEN_MAX + 1)];
    srs_srec_candidate_t cands[CANDIDATE_MAX + 1];
    srs_srec_candidate_t *sorted[CANDIDATE_MAX + 1];
    int32_t purgelen;
    int i;

    if (ctx && (decset = ctx->decset) && (dec = decset->curdec)) {

        for (i = 0;  i < CANDIDATE_MAX;  i++)
            cands[i].tokens = token_pool + (i * (CANDIDATE_TOKEN_MAX + 1));

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
                               srs_srec_utterance_t *utt,
                               srs_srec_candidate_t *cands,
                               srs_srec_candidate_t **sorted)
{
    filter_buf_t *filtbuf;
    decoder_set_t *decset;
    decoder_t *dec;
    logmath_t *lmath;
    const char *uttid;
    const char *hyp;
    int32 score;
    double prob;
    ps_nbest_t *nb;
    ps_seg_t *seg;
    int32_t frlen;
    int32 start, end;
    size_t ncand;
    srs_srec_candidate_t *cand;
    srs_srec_token_t *tkn;
    int32_t length;

    if (!ctx || !(filtbuf = ctx->filtbuf) ||
        !(decset = ctx->decset) || !(dec = decset->curdec))
        return;

    frlen = filtbuf->frlen;
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
                
            cand->score = logmath_exp(lmath, score) / prob;
            cand->ntoken = 0;
                
            length = 0;
                
            while ((seg = ps_seg_next(seg))) {
                if ((hyp = ps_seg_word(seg))) {
                    if (!strcmp(hyp, "</s>") ||
                        cand->ntoken >= CANDIDATE_TOKEN_MAX)
                    {
                        ncand++;
                        memset(cand+1, 0, sizeof(srs_srec_candidate_t));
                        ps_seg_frames(seg, &start, &end);
                        ps_seg_free(seg);
                        //printf("hyp=</s> ncand=%d\n", ncand);
                        length = end * frlen;
                        break;
                    }
                    else if (!strcmp(hyp, "<sil>")) {
                        ps_seg_frames(seg, &start, &end);
                        //printf("hyp=<sil> skip it\n");
                    }
                    else {
                        tkn = cand->tokens + cand->ntoken++;
                        tkn->token = tknbase(hyp);
                        ps_seg_frames(seg, &start, &end);
                        tkn->start = start * frlen;
                        tkn->end = end * frlen;
                        //printf("hyp=%s (%d, %d) tkn count %d\n",
                        //      tkn->token, tkn->start,tkn->end, cand->ntoken);
                    }
                }
            } /* while seg */
            
            if (!seg && cand->ntoken > 0) {
                ncand++;
                cand->score *= 0.9; /* some penalty */
                memset(cand+1, 0, sizeof(srs_srec_candidate_t));
            }
            
            if (!length) {
                tkn = cand->tokens + (cand->ntoken - 1);
                length = tkn->end;
            }
        }
    } /* for nb */
    
    utt->id = uttid;
    utt->score = prob;
    utt->length = length;
    utt->ncand = candidate_sort(cands, sorted);
    utt->cands = sorted;
}

static void fsg_processor(context_t *ctx,
                          srs_srec_utterance_t *utt,
                          srs_srec_candidate_t *cands,
                          srs_srec_candidate_t **sorted)
{
    filter_buf_t *filtbuf;
    decoder_set_t *decset;
    decoder_t *dec;
    logmath_t *lmath;
    const char *uttid;
    int32_t score;
    double prob;
    srs_srec_candidate_t *cand;
    srs_srec_token_t *tkn;
    ps_lattice_t *dag;
    ps_latlink_t *lnk;
    ps_latnode_t *nod;
    const char *token;
    int32_t frlen;
    int32_t start, end;
    int16 fef, lef;

    if (!ctx || !(filtbuf = ctx->filtbuf) ||
        !(decset = ctx->decset) || !(dec = decset->curdec))
        return;

    frlen = filtbuf->frlen;
    lmath = ps_get_logmath(dec->ps);
    ps_get_hyp(dec->ps, &score, &uttid);
    prob = logmath_exp(lmath, score);

    cand = cands;
    cand->score = 1.0;
    cand->ntoken = 0;

    tkn = NULL;

    if ((dag = ps_get_lattice(dec->ps))) {

        if ((lnk = ps_lattice_traverse_edges(dag, NULL, NULL))) {

            ps_latlink_nodes(lnk, &nod);

            if (nod && (token = ps_latnode_word(dag, nod)) && *token != '<') {
                tkn = cand->tokens + cand->ntoken++;
                tkn->token = tknbase(token);
                tkn->start = ps_latnode_times(nod, &fef, &lef) * frlen;
                tkn->end = ((fef + lef) / 2) * frlen;
            }

            goto handle_destination_node;

            while ((lnk = ps_lattice_traverse_next(dag, NULL))) {

              handle_destination_node:
                nod = ps_latlink_nodes(lnk, NULL);

                if (nod && (token = ps_latnode_word(dag,nod)) && *token != '<')
                {
                    start = ps_latnode_times(nod, &fef, &lef) * frlen;
                    end = fef * frlen;

                    if (tkn && start < (int32_t)tkn->end)
                        break;  /* just take one candidate */

                    if (!tkn || !tkneq(token, tkn->token)) {
                        tkn = cand->tokens + cand->ntoken++;
                        tkn->token = tknbase(token);
                        tkn->start = start;
                        tkn->end = end;
                    }
                }
            }
        }
    }

    sorted[0] = cands;
    sorted[1] = NULL;

    utt->id = uttid;
    utt->score = prob < 0.00001 ? 0.00001 : prob;
    utt->length = dag ? ps_lattice_n_frames(dag) * frlen : 0;
    utt->ncand = 1;
    utt->cands = sorted;
}


static void print_utterance(context_t *ctx, srs_srec_utterance_t *utt)
{
    decoder_set_t *decset;
    decoder_t *dec;
    srs_srec_candidate_t *cand;
    srs_srec_token_t *tkn;
    size_t i,j;

    if (ctx && (decset = ctx->decset) && (dec = decset->curdec)) {
        mrp_log_info("*** %15s  (%.4lf) %zd candidates, length %u",
                     utt->id, utt->score, utt->ncand, utt->length);

        for (i = 0; (cand = utt->cands[i]) != NULL;  i++) {
            mrp_log_info("  (%.4lf) ----------------------", cand->score);

            for (j = 0;  j < cand->ntoken;  j++) {
                tkn = cand->tokens + j;
                mrp_log_info("           %d - %d  %s",
                             tkn->start, tkn->end, tkn->token);
            }
        }

        mrp_log_info("           ----------------------");
    }
}

static srs_srec_candidate_t *candidate_equal(srs_srec_candidate_t *a,
                                             srs_srec_candidate_t *b)
{
    srs_srec_token_t *at,*bt;
    size_t i,n;

    if (!a || !b)
        return NULL;

    if ((n = a->ntoken) != b->ntoken)
        return false;

    for (i = 0;  i < n;  i++) {
        at = a->tokens + i;
        bt = b->tokens + i;

        if (!tkneq(at->token, bt->token))
            return NULL;
    }

    return (a->score > b->score) ? a : b;
}

static double candidate_score(srs_srec_candidate_t *cand)
{
    return cand ? cand->score : 0.0;
}


static uint32_t candidate_sort(srs_srec_candidate_t *cands,
                               srs_srec_candidate_t **sorted)
{
    srs_srec_candidate_t *c, **s;
    srs_srec_candidate_t *better_score;
    size_t i,j,n;

    memset(sorted, 0, sizeof(srs_srec_candidate_t *) * (CANDIDATE_MAX + 1));

    for (i = n = 0;  i < CANDIDATE_MAX;  i++) {
        if (!(c = cands + i)->ntoken)
            break;

        for (j = 0;   j <= n;   j++) {
            s = sorted + j;

            if ((better_score = candidate_equal(c, *s))) {
                *s = better_score;
                break;
            }

            if (candidate_score(c) > candidate_score(*s)) {
                if (j < n) {
                    memmove(sorted + j+1, sorted + j,
                            sizeof(srs_srec_candidate_t *) * (n - j));
                }
                *s = c;
                n++;
                break;
            }
        }
    }

    return n;
}

static bool tkneq(const char *tkn1, const char *tkn2)
{
    const char *e1, *e2;
    int l1, l2;

    if (!tkn1 || !tkn2)
        return false;

    if (!strcmp(tkn1, tkn2))
        return true;

    if (*tkn1 == *tkn2) {
        l1 = (e1 = strchr(tkn1, '(')) ? e1 - tkn1 : 0;
        l2 = (e2 = strchr(tkn2, '(')) ? e2 - tkn2 : 0;

        if (l1 || l2) {
            if (l1 == l2 && !strncmp(tkn1, tkn2, l1))
                return true;
            if (l1 && !l2 && !strncmp(tkn1, tkn2, l1))
                return true;
            if (!l1 && l2 && !strncmp(tkn1, tkn2, l2))
                return true;
        }
    }

    return false;
}

static const char *tknbase(const char *token)
{
    static char  pool[16384];
    static char *ptr = pool;
    static char *end = pool + (sizeof(pool) - 1);

    char c, *p, *stripped;
    const char *q;
    int i;

    for (i = 0; i < 2;  i++) {
        for (stripped = p = ptr, q = token;  p < end;  p++) {
            c = *q++;

            if (c == '\0')
                return token;

            if (c == '(') {
                *p++ = '\0';
                ptr = p;
                return stripped;
            }

            *p++ = c;
        }
        ptr = pool;
    }
    return token;
}


/*
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 *
 */
