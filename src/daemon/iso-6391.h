#ifndef __SRS_DAEMON_ISO6391_H__
#define __SRS_DAEMON_ISO6391_H__

/*
 * ISO 639-1 (two letter) language code to name mapping
 */

typedef struct {
    const char *code;                    /* ISO 639-1 code */
    const char *language;                /* associated language */
} srs_iso6391_t;


/*
 * abbreviated dialect to full name mapping
 */

typedef struct {
    const char *code;                    /* abbreviated dialect */
    const char *dialect;                 /* full dialect name */
} srs_dialect_t;

const char *srs_iso6391_language(const char *code);
const char *srs_iso6391_dialect(const char *code);


#endif /* __SRS_DAEMON_ISO6391_H__ */
