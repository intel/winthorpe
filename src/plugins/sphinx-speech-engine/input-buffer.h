#ifndef __SRS_POCKET_SPHINX_INPUT_BUFFER_H__
#define __SRS_POCKET_SPHINX_INPUT_BUFFER_H__

#include <sphinxbase/ad.h>
#include <sphinxbase/cont_ad.h>

#include "sphinx-plugin.h"

struct input_buf_s {
    ad_rec_t ad;
    cont_ad_t *cont;
    void *buf;
    size_t max;
    size_t minreq;
    size_t len;
    bool calibrated;
    context_t *ctx;
};


int  input_buffer_create(context_t *ctx);
void input_buffer_destroy(context_t *ctx);

int  input_buffer_initialize(context_t *ctx, size_t size, size_t minreq);

void input_buffer_purge(context_t *ctx);

void input_buffer_process_data(context_t *ctx, const void *buf, size_t len);


#endif /* __SRS_POCKET_SPHINX_INPUT_BUFFER_H__ */

/*
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 *
 */
