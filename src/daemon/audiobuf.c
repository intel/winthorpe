#include <murphy/common/mm.h>
#include <murphy/common/refcnt.h>

#include "src/daemon/audiobuf.h"

/*
 * audio buffer handling
 */

srs_audiobuf_t *srs_create_audiobuf(srs_audioformat_t format, uint32_t rate,
                                    uint8_t channels, size_t samples,
                                    void *data)
{
    srs_audiobuf_t *buf;
    size_t          width, size;

    switch (format) {
    case SRS_AUDIO_U8:
    case SRS_AUDIO_ALAW:
    case SRS_AUDIO_ULAW:
        width = 1;
        break;
    case SRS_AUDIO_S16LE:
    case SRS_AUDIO_S16BE:
        width = 2;
        break;
    case SRS_AUDIO_FLOAT32LE:
    case SRS_AUDIO_FLOAT32BE:
        width = sizeof(float);
        break;
    case SRS_AUDIO_S32LE:
    case SRS_AUDIO_S32BE:
    case SRS_AUDIO_S24_32LE:
    case SRS_AUDIO_S24_32BE:
        width = 4;
        break;
    case SRS_AUDIO_S24LE:
    case SRS_AUDIO_S24BE:
        width = 3;

    default:
        return NULL;
    }

    size = channels * samples * width;

    if ((buf = mrp_allocz(sizeof(*buf))) != NULL) {
        if ((buf->data = mrp_datadup(data, size)) != NULL) {
            mrp_refcnt_init(&buf->refcnt);
            buf->format   = format;
            buf->rate     = rate;
            buf->channels = channels;
            buf->samples  = samples;

            return buf;
        }
        else
            mrp_free(buf);
    }

    return NULL;
}


srs_audiobuf_t *srs_ref_audiobuf(srs_audiobuf_t *buf)
{
    return mrp_ref_obj(buf, refcnt);
}


void srs_unref_audiobuf(srs_audiobuf_t *buf)
{
    if (mrp_unref_obj(buf, refcnt)) {
        mrp_free(buf->data);
        mrp_free(buf);
    }
}
