#ifndef __SRS_LIBCARNIVAL_H__
#define __SRS_LIBCARNIVAL_H__

#include <murphy/common/macros.h>

MRP_CDECL_BEGIN

typedef struct  {
    char *voice;
    char *language;
    int   female;
    char *description;
} carnival_voice_t;


/** Initialize festival. */
int carnival_init(void);

/** Cleanup festival. */
void carnival_exit(void);

/** List available voices. */
int carnival_available_voices(char ***voices, int *nvoice);

/** List loaded voices. */
int carnival_loaded_voices(char ***voices, int *nvoice);

/** Free a string allocated by libcarnival. */
void carnival_free_string(char *str);

/** Free an array of strings allocated by libcarnival. */
void carnival_free_strings(char **strings, int nstring);

/** Load a given voice. */
int carnival_load_voice(const char *name);

/** Query a (loaded) voice. */
int carnival_query_voice(const char *name, char **language, int *female,
                         char **dialect, char **description);

/** Select the given voice. */
int carnival_select_voice(const char *name);

/** Synthesize a given message using the currently selected voice. */
int carnival_synthesize(const char *text, void **bufp, int *sratep,
                        int *nchannelp, int *nsamplep);

MRP_CDECL_END




#endif /* __SRS_LIBCARNIVAL_H__ */
