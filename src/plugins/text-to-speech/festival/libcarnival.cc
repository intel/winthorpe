#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>

#include <festival.h>
#include <siod.h>
#include <siod_defs.h>

#include <murphy/common/debug.h>
#include <murphy/common/log.h>
#include <murphy/common/mm.h>
#include <murphy/common/list.h>

#include "libcarnival.h"

typedef struct {
    char            *name;               /* unique voice name */
    char            *language;           /* spoken language */
    int              female;             /* whether a female speaker */
    char            *dialect;            /* spoken dialect if any */
    char            *description;        /* verbose voice description */
    mrp_list_hook_t  hook;
} voice_t;

static MRP_LIST_HOOK(ventries);          /* all known voices */
static int           navail;             /* number of available voices */
static int           nloaded;            /* number of loaded voices */


static voice_t *find_voice_entry(const char *name)
{
    voice_t         *v;
    mrp_list_hook_t *p, *n;

    mrp_list_foreach(&ventries, p, n) {
        v = mrp_list_entry(p, voice_t, hook);

        if (!strcmp(v->name, name))
            return v;
    }

    return NULL;
}


static void update_available_voices(void)
{
    voice_t *v;
    LISP     lvoice_list, ll, lv;

    CATCH_ERRORS_QUIET()                 /* if (caught lisp errors) */
        goto errout;

    gc_protect(&ll);

    if ((lvoice_list = siod_get_lval("voice.list", NULL)) == NIL)
        return;

    ll = leval(cons(lvoice_list, NIL), NIL);

    for ( ; ll != NIL; ll = cdr(ll)) {
        lv = car(ll);

        if (!atomp(lv))
            continue;

        v = (voice_t *)mrp_allocz(sizeof(*v));

        if (v == NULL)
            goto errout;

        mrp_list_init(&v->hook);
        v->name = mrp_strdup(get_c_string(lv));

        if (v->name == NULL) {
            mrp_free(v);
            goto errout;
        }

        mrp_list_append(&ventries, &v->hook);
        navail++;
    }

    END_CATCH_ERRORS();

 errout:
    gc_unprotect(&ll);
}


static void update_loaded_voices(void)
{
    voice_t    *v;
    LISP        ll, lentry, lname, ldescr, lp, lk, lv;
    const char *key;

    if ((ll = siod_get_lval("Voice_descriptions", NULL)) == NIL)
        return;

    CATCH_ERRORS_QUIET()
        goto errout;

    for ( ; ll != NIL; ll = cdr(ll)) {
        lentry = car(ll);
        lname  = car(lentry);
        v      = find_voice_entry(get_c_string(lname));

        if (v == NULL) {
            mrp_log_error("Strange... can't find entry for voice '%s'.",
                          get_c_string(lname));
            continue;
        }

        for (ldescr = car(cdr(lentry)); ldescr != NIL; ldescr = cdr(ldescr)) {
            lp = car(ldescr);

            if (!consp(lp)) {
                mrp_log_error("Strange... invalid descriptor item for '%s'.",
                              v->name);
                continue;
            }

            lk = car(lp);
            lv = car(cdr(lp));

            if (!atomp(lk) || !atomp(lv)) {
                mrp_log_error("Strange... invalid desciptor item for '%s'.",
                              v->name);
                continue;
            }

            key = get_c_string(lk);

            mrp_debug("%s:%s = %s", v->name, key, get_c_string(lv));

            if (!strcmp(key, "language"))
                v->language = mrp_strdup(get_c_string(lv));
            else if (!strcmp(key, "dialect"))
                v->dialect = mrp_strdup(get_c_string(lv));
            else if (!strcmp(key, "description"))
                v->description = mrp_strdup(get_c_string(lv));
            else if (!strcmp(key, "gender"))
                v->female = !strcasecmp(get_c_string(lv), "female");
            else
                mrp_log_warning("Ignoring descriptor item '%s' for '%s'.",
                                v->name, get_c_string(lv));
        }

        nloaded++;
    }

    END_CATCH_ERRORS();

 errout:
    return;
}


int carnival_init(void)
{
    festival_initialize(TRUE, FESTIVAL_HEAP_SIZE);

    update_available_voices();
    update_loaded_voices();

    return 0;
}


void carnival_exit(void)
{
    mrp_list_hook_t *p, *n;
    voice_t         *v;

    mrp_list_foreach(&ventries, p, n) {
        v = mrp_list_entry(p, voice_t, hook);

        mrp_list_delete(&v->hook);

        mrp_free(v->name);
        mrp_free(v->language);
        mrp_free(v->dialect);
        mrp_free(v->description);

        mrp_free(v);
    }

    festival_tidy_up();

    navail  = 0;
    nloaded = 0;
}


int carnival_available_voices(char ***voicesp, int *nvoicep)
{
    voice_t          *v;
    mrp_list_hook_t  *p, *n;
    char            **voices;
    int               nvoice, i;

    nvoice = 0;
    voices = mrp_alloc_array(char *, navail);

    if (voices == NULL)
        return -1;

    mrp_list_foreach(&ventries, p, n) {
        v = mrp_list_entry(p, voice_t, hook);

        if (!v->name)
            continue;

        if ((voices[nvoice] = mrp_strdup(v->name)) == NULL)
            goto errout;

        nvoice++;
    }

    *voicesp = voices;
    *nvoicep = nvoice;

    return 0;

 errout:
    for (i = 0; i < nvoice; i++)
        mrp_free(voices[i]);
    mrp_free(voices);

    return -1;
}


int carnival_loaded_voices(char ***voicesp, int *nvoicep)
{
    voice_t          *v;
    mrp_list_hook_t  *p, *n;
    char            **voices;
    int               nvoice, i;

    nvoice = 0;
    voices = mrp_alloc_array(char *, navail);

    if (voices == NULL)
        return -1;

    mrp_list_foreach(&ventries, p, n) {
        v = mrp_list_entry(p, voice_t, hook);

        if (!v->language)
            continue;

        if ((voices[nvoice] = mrp_strdup(v->name)) == NULL)
            goto errout;

        nvoice++;
    }

    *voicesp = voices;
    *nvoicep = nvoice;

    return 0;

 errout:
    for (i = 0; i < nvoice; i++)
        mrp_free(voices[i]);
    mrp_free(voices);

    *voicesp = NULL;
    *nvoicep = 0;

    return -1;
}


void carnival_free_string(char *str)
{
    mrp_free(str);
}


void carnival_free_strings(char **strings, int nstring)
{
    int i;

    for (i = 0; i < nstring; i++)
        mrp_free(strings[i]);
    mrp_free(strings);
}


int carnival_load_voice(const char *name)
{
    voice_t *v;
    char     loader[256];
    LISP     lf, lr;
    int      r;

    if ((v = find_voice_entry(name)) != NULL && v->language != NULL)
        return 0;                        /* already loaded, nothing to do */

    if (snprintf(loader, sizeof(loader), "voice_%s", name) >= (int)sizeof(loader)) {
        errno = EOVERFLOW;

        return -1;
    }

    r = 0;
    CATCH_ERRORS_QUIET()                 /* if (caught lisp errors) */
        return -1;

    if ((lf = siod_get_lval(loader, NULL)) == NIL) {
        r = -1;
        goto outerr;
    }

    lr = leval(cons(lf, NIL), NIL);

    if (atomp(lr) && !strcmp(get_c_string(lr), name))
        r = 0;
    else
        r = -1;

    update_loaded_voices();


 outerr:
    END_CATCH_ERRORS();
    return r;
}


int carnival_query_voice(const char *name, char **languagep, int *femalep,
                         char **dialectp, char **descriptionp)
{
    voice_t *v;

    v = find_voice_entry(name);

    if (v == NULL || v->language == NULL) {
        errno = ENOENT;

        if (languagep != NULL)
            *languagep = NULL;
        if (femalep != NULL)
            *femalep = -1;
        if (dialectp != NULL)
            *dialectp = NULL;
        if (descriptionp != NULL)
            *descriptionp = NULL;

        return -1;
    }

    if (languagep != NULL)
        *languagep = mrp_strdup(v->language);
    if (femalep != NULL)
        *femalep = v->female;
    if (dialectp != NULL)
        *dialectp = mrp_strdup(v->dialect);
    if (descriptionp != NULL)
        *descriptionp = mrp_strdup(v->description);

    return 0;
}


int carnival_select_voice(const char *name)
{
    voice_t *v;
    char     sel[256];
    LISP     lf, lr;
    int      r;

    if ((v = find_voice_entry(name)) == NULL || v->language == NULL) {
        errno = ENOENT;

        return -1;                       /* not loaded, cannot select */
    }

    if (snprintf(sel, sizeof(sel), "voice_%s", name) >= (int)sizeof(sel)) {
        errno = EOVERFLOW;

        return -1;
    }

    r = 0;
    CATCH_ERRORS_QUIET()                 /* if (caught lisp errors) */
        return -1;

    if ((lf = siod_get_lval(sel, NULL)) == NIL) {
        r = -1;
        goto outerr;
    }

    lr = leval(cons(lf, NIL), NIL);

    if (atomp(lr) && !strcmp(get_c_string(lr), name))
        r = 0;
    else
        r = -1;

 outerr:
    END_CATCH_ERRORS();
    return r;
}


int carnival_synthesize(const char *text, void **bufp, int *sratep,
                        int *nchannelp, int *nsamplep)
{
    EST_Wave  w;
    short    *buf;
    size_t    size;
    int       nchannel, nsample, i;

    if (sratep == NULL || nchannelp == NULL || nsamplep == NULL) {
        errno = EFAULT;

        return -1;
    }

    if (!festival_text_to_wave(text, w))
        return -1;

    nchannel = w.num_channels();
    nsample  = w.num_samples();
    size     = nchannel * nsample * 2;

    buf = (short *)mrp_allocz(size);

    if (buf == NULL)
        return -1;

    for (i = 0; i < nsample; i++)
        w.copy_sample(i, buf + i * nchannel);

    *sratep    = w.sample_rate();
    *nchannelp = nchannel;
    *nsamplep  = nsample;
    *bufp      = (void *)buf;

    return 0;
}
