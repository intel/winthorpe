#ifndef __SRS_POCKET_SPHINX_UTTERANCE_H__
#define __SRS_POCKET_SPHINX_UTTERANCE_H__

#include "sphinx-plugin.h"

#define CANDIDATE_TOKEN_MAX  50
#define CANDIDATE_MAX        1000


#if 0
struct word_s {
    const char *word;
    int32_t start;
    int32_t end;
};

struct candidate_s {
    double  quality;
    size_t  nword;
    word_t  words[CANDIDATE_WORD_MAX + 1];
};

struct utternace_s {
    const char *id;
    double quality;
    uint32_t length;
    size_t ncand;
    candidate_t **cands;
};
#endif

void utterance_start(context_t *ctx);
void utterance_end(context_t *ctx);


#endif /* __SRS_POCKET_SPHINX_UTTERANCE_H__ */

/*
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 *
 */
