#ifndef __SRS_POCKET_SPHINX_DECODER_H__
#define __SRS_POCKET_SPHINX_DECODER_H__

#include <sphinxbase/cmd_ln.h>
#include <pocketsphinx/pocketsphinx.h>

#include "sphinx-plugin.h"


struct decoder_s {
    const char *name;
    cmd_ln_t *cfg;
    ps_decoder_t *ps;
    const char **fsgs;
    size_t nfsg;
    utterance_processor_t utproc;
    uint32_t utid;
    bool utter;
};

struct decoder_set_s {
    size_t ndec;
    decoder_t *decs;
    decoder_t *curdec;
};


int decoder_set_create(context_t *ctx);
void decoder_set_destroy(context_t *ctx);

int decoder_set_add(context_t *ctx, const char *decoder_name,
                    const char *hmm, const char *lm,
                    const char *dict, const char *fsg,
                    uint32_t topn);
bool decoder_set_contains(context_t *ctx, const char *decoder_name);
int decoder_set_use(context_t *ctx, const char *decoder_name);
const char *decoder_set_name(context_t *ctx);


#endif /* __SRS_POCKET_SPHINX_DECODER_H__ */

/*
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 *
 */
