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

#include <errno.h>

#include <murphy/common/list.h>

#include "src/daemon/context.h"
#include "src/daemon/recognizer.h"


/*
 * a speech recognition backend
 */

typedef struct {
    srs_context_t     *srs;              /* main context */
    char              *name;             /* recognizer name */
    mrp_list_hook_t    hook;             /* to list of recognizers */
    srs_srec_api_t     api;              /* backend API */
    void              *api_data;         /* opaque backend data */
    srs_srec_result_t *result;           /* result being processed, if any */
} srs_srec_t;


/*
 * a speech recognition disambiguator
 */

typedef struct {
    char             *name;
    mrp_list_hook_t   hook;
    srs_disamb_api_t  api;
    void             *api_data;
} srs_disamb_t;


static srs_srec_t *find_srec(srs_context_t *srs, const char *name);
static int srec_notify_cb(srs_srec_utterance_t *utt, void *notify_data);
static srs_disamb_t *find_disamb(srs_context_t *srs, const char *name);


/*
 * speech recognizer backend handling
 */

int srs_register_srec(srs_context_t *srs, const char *name,
                      srs_srec_api_t *api, void *api_data,
                      srs_srec_notify_t *notify, void **notify_data)
{
    srs_srec_t *srec;

    if (notify == NULL || notify_data == NULL) {
        errno = EINVAL;
        return -1;
    }

    if (find_srec(srs, name) != NULL) {
        mrp_log_error("A recognizer '%s' already registered.", name);

        errno = EEXIST;
        return -1;
    }

    srec = mrp_allocz(sizeof(*srec));

    if (srec != NULL) {
        mrp_list_init(&srec->hook);
        srec->srs      = srs;
        srec->name     = mrp_strdup(name);
        srec->api      = *api;
        srec->api_data = api_data;

        if (srec->name != NULL) {
            mrp_list_append(&srs->recognizers, &srec->hook);

            if (srs->cached_srec == NULL)
                srs->cached_srec = srec;

            if (srs->default_srec == NULL)
                srs->default_srec = srec;

            mrp_log_info("Registered speech recognition engine '%s'.", name);

            *notify      = srec_notify_cb;
            *notify_data = srec;

            return 0;
        }

        mrp_free(srec);
    }

    mrp_log_error("Failed to allocate speech recognition engine '%s'.", name);

    return -1;
}


void srs_unregister_srec(srs_context_t *srs, const char *name)
{
    srs_srec_t *srec = find_srec(srs, name);

    if (srec != NULL) {
        mrp_list_delete(&srec->hook);
        mrp_free(srec->name);
        mrp_free(srec);

        if (srs->cached_srec == srec)
            srs->cached_srec = NULL;

        if (srs->default_srec == srec)
            srs->default_srec = NULL;

        mrp_log_info("Unregistered speech recognition engine '%s'.", name);
    }
}


int srs_activate_srec(srs_context_t *srs, const char *name)
{
    srs_srec_t *srec = find_srec(srs, name);

    if (srec != NULL)
        return srec->api.activate(srec->api_data);
    else
        return FALSE;
}


void srs_deactivate_srec(srs_context_t *srs, const char *name)
{
    srs_srec_t *srec = find_srec(srs, name);

    if (srec != NULL)
        srec->api.deactivate(srec->api_data);
}


int srs_check_decoder(srs_context_t *srs, const char *name,
                      const char *decoder)
{
    srs_srec_t *srec = find_srec(srs, name);

    if (srec != NULL)
        return srec->api.check_decoder(decoder, srec->api_data);
    else
        return FALSE;
}


int srs_set_decoder(srs_context_t *srs, const char *name, const char *decoder)
{
    srs_srec_t *srec = find_srec(srs, name);

    if (srec != NULL)
        return srec->api.select_decoder(decoder, srec->api_data);
    else
        return FALSE;
}


static srs_srec_t *find_srec(srs_context_t *srs, const char *name)
{
    srs_srec_t      *srec = srs->cached_srec;
    mrp_list_hook_t *p, *n;

    if (name == SRS_DEFAULT_RECOGNIZER)
        return srs->default_srec;

    if (srec == NULL || strcmp(srec->name, name) != 0) {
        mrp_list_foreach(&srs->recognizers, p, n) {
            srec = mrp_list_entry(p, typeof(*srec), hook);

            if (!strcmp(srec->name, name)) {
                srs->cached_srec = srec;
                return srec;
            }
        }

        return NULL;
    }
    else
        return srec;
}


static void free_match(srs_srec_match_t *m)
{
    mrp_free(m);
}


static void free_srec_result(srs_srec_result_t *res)
{
    srs_srec_match_t *m;
    mrp_list_hook_t  *p, *n;
    int               i;

    switch (res->type) {
    case SRS_SREC_RESULT_MATCH:
        mrp_list_foreach(&res->result.matches, p, n) {
            m = mrp_list_entry(p, typeof(*m), hook);
            mrp_list_delete(&m->hook);
            free_match(m);
        }
        break;

    case SRS_SREC_RESULT_DICT:
        break;

    case SRS_SREC_RESULT_AMBIGUOUS:
        break;

    default:
        break;
    }

    for (i = 0; i < res->ntoken; i++)
        mrp_free(res->tokens[i]);
    mrp_free(res->tokens);

    mrp_free(res->start);
    mrp_free(res->end);
    srs_unref_audiobuf(res->samplebuf);

    for (i = 0; i < res->ndict; i++)
        mrp_free(res->dicts[i]);
    mrp_free(res->dicts);


    mrp_list_delete(&res->hook);
    mrp_free(res);
}


static int switch_dict(srs_srec_t *srec, const char *dict)
{
    return srec->api.select_decoder(dict, srec->api_data) ? 0 : -1;
}


static int push_dict(srs_srec_t *srec, srs_srec_result_t *res)
{
    const char *active, *next;

    active = srec->api.active_decoder(srec->api_data);
    next   = res->result.dict.dict;

    if (mrp_reallocz(res->dicts, res->ndict, res->ndict + 1) == NULL ||
        (res->dicts[res->ndict] = mrp_strdup(active))        == NULL)
        return -1;

    if (res->dicts[res->ndict] == NULL)
        return -1;

    res->ndict++;

    return switch_dict(srec, next);
}


static int pop_dict(srs_srec_t *srec, srs_srec_result_t *res)
{
    char *prev;

    if (res->ndict <= 0) {
        errno = ENOENT;
        return -1;
    }

    prev = res->dicts[res->ndict - 1];

    if (switch_dict(srec, prev) == 0) {
        mrp_reallocz(res->dicts, res->ndict, res->ndict - 1);
        res->ndict--;
        mrp_free(prev);

        return 0;
    }
    else
        return -1;
}


static void process_match_result(srs_srec_t *srec, srs_srec_result_t *res)
{
    mrp_list_hook_t  *p, *n;
    srs_srec_match_t *match;
    int               i;

    mrp_list_foreach(&res->result.matches, p, n) {
        match = mrp_list_entry(p, typeof(*match), hook);

        for (i = 0; i < res->ntoken; i++) {
            mrp_log_info("  #%d token ('%s'): %u - %u", i,
                         res->tokens[i], res->start[i], res->end[i]);
        }

        client_notify_command(match->client, match->index,
                              res->ntoken, (const char **)res->tokens,
                              res->start, res->end, res->samplebuf);

        while (res->ndict > 0)
            pop_dict(srec, res);
    }
}


static int process_dict_result(srs_srec_t *srec, srs_srec_result_t *res)
{
    if (srec->result != NULL && srec->result != res) {
        mrp_log_error("Conflicting results (%p != %p) for dictionary switch.",
                      res, srec->result);
        return 0;
    }

    switch (res->result.dict.op) {
    case SRS_DICT_OP_POP:
        if (pop_dict(srec, res) < 0) {
            mrp_log_error("Failed to pop dictionary.");
            return 0;
        }
        break;

    case SRS_DICT_OP_PUSH:
        if (push_dict(srec, res) < 0) {
            mrp_log_error("Failed to push dictionary '%s'.",
                          res->result.dict.dict);
            return 0;
        }
        break;

    case SRS_DICT_OP_SWITCH:
        if (switch_dict(srec, res->result.dict.dict) < 0) {
            mrp_log_error("Failed to switch to dictionary '%s'.",
                          res->result.dict.dict);
            return 0;
        }
        break;

    default:
        break;
    }

    srec->result     = res;
    res->sampleoffs += res->result.dict.rescan;

    return res->result.dict.rescan;
}


static void process_ambiguity(srs_srec_t *srec, srs_srec_result_t *res)
{
    MRP_UNUSED(srec);
    MRP_UNUSED(res);

    return;
}


static int srec_notify_cb(srs_srec_utterance_t *utt, void *notify_data)
{
    srs_srec_t           *srec = (srs_srec_t *)notify_data;
    srs_disamb_t         *dis;
    srs_srec_candidate_t *c;
    srs_srec_result_t    *res;
    srs_srec_token_t     *t;
    int                   flush, i, j;
    uint32_t              start, end;

    mrp_log_info("Got %zd recognition candidates in from %s backend:",
                 utt->ncand, srec->name);

    for (i = 0; i < (int)utt->ncand; i++) {
        c = utt->cands[i];
        mrp_log_info("Candidate #%d:", i);
        for (j = 0, t = c->tokens; j < (int)c->ntoken; j++, t++) {
            mrp_log_info("    token #%d: '%s' (%u - %u)", j, t->token,
                         t->start, t->end);
        }
    }

    flush = SRS_SREC_FLUSH_ALL;
    dis   = find_disamb(srec->srs, SRS_DEFAULT_DISAMBIGUATOR);

    if (dis != NULL) {
        if (srec->result == NULL) {
            res = srec->result = mrp_allocz(sizeof(*srec->result));

            if (res == NULL)
                return SRS_SREC_FLUSH_ALL;

            mrp_list_init(&res->hook);
            mrp_list_init(&res->result.matches);

            c     = utt->cands[0];
            start = 0;
            end   = utt->length;

            res->samplebuf = srec->api.sampledup(start, end, srec->api_data);
        }

        res = srec->result;

        if (dis->api.disambiguate(utt, &res, dis->api_data) == 0 && res) {
            mrp_log_info("Disambiguation succeeded.");

            switch (res->type) {
            case SRS_SREC_RESULT_MATCH:
                process_match_result(srec, res);
                free_srec_result(res);
                srec->result = NULL;
                flush = SRS_SREC_FLUSH_ALL;
                break;

            case SRS_SREC_RESULT_DICT:
                flush = process_dict_result(srec, res);
                break;

            case SRS_SREC_RESULT_AMBIGUOUS:
                process_ambiguity(srec, res);
                free_srec_result(res);
                flush = SRS_SREC_FLUSH_ALL;
                break;

            default:
                flush = SRS_SREC_FLUSH_ALL;
                break;
            }
        }
    }

    return flush;
}


/*
 * disambiguator handling
 */

int srs_register_disambiguator(srs_context_t *srs, const char *name,
                               srs_disamb_api_t *api, void *api_data)
{
    srs_disamb_t *dis;

    if (find_disamb(srs, name) != NULL) {
        mrp_log_error("A disambiguator '%s' already exists.", name);

        errno = EEXIST;
        return -1;
    }

    dis = mrp_allocz(sizeof(*dis));

    if (dis != NULL) {
        mrp_list_init(&dis->hook);
        dis->name     = mrp_strdup(name);
        dis->api      = *api;
        dis->api_data = api_data;

        if (dis->name != NULL) {
            mrp_list_append(&srs->disambiguators, &dis->hook);

            if (srs->default_disamb == NULL)
                srs->default_disamb = dis;

            mrp_log_info("Registered disambiguator '%s'.", name);

            return 0;
        }

        mrp_free(dis);
    }

    mrp_log_error("Failed to allocate disambiguator '%s'.", name);

    return -1;
}


void srs_unregister_disambiguator(srs_context_t *srs, const char *name)
{
    srs_disamb_t *dis = find_disamb(srs, name);

    if (dis != NULL) {
        mrp_list_delete(&dis->hook);
        mrp_free(dis->name);
        mrp_free(dis);

        if (srs->default_disamb == dis)
            srs->default_disamb = NULL;

        mrp_log_info("Unregistered disambiguator '%s'.", name);
    }
}


static srs_disamb_t *find_disamb(srs_context_t *srs, const char *name)
{
    srs_disamb_t    *dis;
    mrp_list_hook_t *p, *n;

    if (name == SRS_DEFAULT_DISAMBIGUATOR)
        return srs->default_disamb;

    mrp_list_foreach(&srs->disambiguators, p, n) {
        dis = mrp_list_entry(p, typeof(*dis), hook);

            if (!strcmp(dis->name, name))
                return dis;
    }

    return NULL;
}


int srs_srec_add_client(srs_context_t *srs, srs_client_t *client)
{
    srs_disamb_t *dis = find_disamb(srs, SRS_DEFAULT_DISAMBIGUATOR);

    if (dis != NULL)
        return dis->api.add_client(client, dis->api_data);
    else
        return -1;
}


void srs_srec_del_client(srs_context_t *srs, srs_client_t *client)
{
   srs_disamb_t *dis = find_disamb(srs, SRS_DEFAULT_DISAMBIGUATOR);

   if (dis != NULL)
       dis->api.del_client(client, dis->api_data);
}
