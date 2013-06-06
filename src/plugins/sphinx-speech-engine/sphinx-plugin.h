#ifndef __SRS_POCKET_SPHINX_PLUGIN_H__
#define __SRS_POCKET_SPHINX_PLUGIN_H__

#include <stdint.h>
#include <stdbool.h>

typedef enum utterance_processor_e  utterance_processor_t;

typedef struct context_s            context_t;
typedef struct plugin_s             plugin_t;
typedef struct options_s            options_t;
typedef struct context_s            context_t;
typedef struct decoder_set_s        decoder_set_t;
typedef struct decoder_s            decoder_t;
typedef struct utternace_s          utterance_t;
typedef struct candidate_s          candidate_t;
typedef struct word_s               word_t;
typedef struct filter_buf_s         filter_buf_t;
typedef struct input_buf_s          input_buf_t;
typedef struct pulse_interface_s    pulse_interface_t;

enum utterance_processor_e {
    UTTERANCE_PROCESSOR_UNKNOWN = 0,
    UTTERANCE_PROCESSOR_ACOUSTIC,
    UTTERANCE_PROCESSOR_FSG,
};

struct context_s {
    plugin_t *plugin;
    options_t *opts;
    decoder_set_t *decset;
    filter_buf_t *filtbuf;
    input_buf_t *inpbuf;
    pulse_interface_t *pulseif;
    bool verbose;
};


int32_t plugin_utterance_handler(context_t *ctx, utterance_t *utt);


#endif /* __SRS_POCKET_SPHINX_PLUGIN_H__ */

/*
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 *
 */
