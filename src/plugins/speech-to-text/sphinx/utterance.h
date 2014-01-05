#ifndef __SRS_POCKET_SPHINX_UTTERANCE_H__
#define __SRS_POCKET_SPHINX_UTTERANCE_H__

#include "sphinx-plugin.h"

#define CANDIDATE_TOKEN_MAX  50
#define CANDIDATE_MAX        5


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
