#ifndef __SRS_POCKET_SPHINX_FILTER_BUFFER_H__
#define __SRS_POCKET_SPHINX_FILTER_BUFFER_H__

#include "sphinx-plugin.h"

struct filter_buf_s {
    int16_t *buf;
    int32_t max;     /* maximum buffer size of filtered data (in samples) */
    int32_t hwm;     /* high-water mark (in samples) */
    int32_t len;     /* length of data in filter buffer (in samples) */
    int32_t frlen;   /* frame length in samples */
    int32_t silen;   /* minimum samples to declare silence */
    int32_t ts;      /* time stamp (in samples actually) */
    int fdrec;       /* fd for recording the filtered stream */
};

int  filter_buffer_create(context_t *ctx);
void filter_buffer_destroy(context_t *ctx);

void filter_buffer_initialize(context_t *ctx, int32_t bufsiz,
                              int32_t high_water_mark, int32_t silen);

bool filter_buffer_is_empty(context_t *ctx);
void filter_buffer_purge(context_t *ctx, int32_t length);
void filter_buffer_process_data(context_t *ctx);
void filter_buffer_utter(context_t *ctx, bool full_utterance);

int16_t *filter_buffer_dup(context_t *ctx, int32_t start, int32_t end);

#endif /* __SRS_POCKET_SPHINX_FILTER_BUFFER_H__ */

/*
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 *
 */
