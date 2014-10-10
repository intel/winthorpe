/*
 * Copyright (c) 2014, Intel Corporation
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *   * Redistributions of source code must retain the above copyright notice,
 *     this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *   * Neither the name of Intel Corporation nor the names of its contributors
 *     may be used to endorse or promote products derived from this software
 *     without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <errno.h>
#include <stdarg.h>

#include <murphy/common/macros.h>
#include <murphy/common/debug.h>
#include <murphy/common/log.h>
#include <murphy/common/json.h>


typedef struct {
    mrp_json_t *objects[64];
    int         depth;
    mrp_json_t *top;
} json_stack_t;


static inline mrp_json_t *push_container(json_stack_t *s, const char *name,
                                         mrp_json_t *c)
{
    if (s->depth >= (int)MRP_ARRAY_SIZE(s->objects)) {
        errno = ENOSPC;
        return NULL;
    }

    s->objects[s->depth++] = c;

    if (s->depth != 1) {
        if (name != NULL)
            mrp_json_add(s->top, name, c);
        else
            mrp_json_array_append(s->top, c);
    }

    s->top = c;

    mrp_debug("push(#%d): %p", s->depth, s->top);

    return c;
}


static inline mrp_json_t *pop_container(json_stack_t *s, const char *type)
{
    if (s->depth < 1) {
        errno = ENOENT;
        return NULL;
    }

    switch (mrp_json_get_type(s->top)) {
    case MRP_JSON_OBJECT:
        if (*type != '}') {
            errno = EINVAL;
            return NULL;
        }
        break;
    case MRP_JSON_ARRAY:
        if (*type != ']') {
            errno = EINVAL;
            return NULL;
        }
        break;
    default:
        break;
    }

    s->depth--;

    if (s->depth < 1)
        s->top = NULL;
    else
        s->top = s->objects[s->depth - 1];

    mrp_debug("pop(#%d): %p", s->depth, s->top);

    return s->top;
}


mrp_json_t *mrp_json_build(const char *type, ...)
{
    json_stack_t   stack = { .depth = 0, .top = NULL };
    mrp_json_t    *o, *c;
    const char    *name;
    unsigned int   t;
    void         **ptr, *obj;
    va_list        ap;

    /* push outermost container object */
    switch (*type) {
    case '{':
        o = c = push_container(&stack, NULL, mrp_json_create(MRP_JSON_OBJECT));
        break;
    case '[':
        o = c = push_container(&stack, NULL, mrp_json_create(MRP_JSON_ARRAY));
        break;
    default:
        errno = EINVAL;
        return NULL;
    }

    /* fill the object */
    va_start(ap, type);
    while ((name = va_arg(ap, const char *)) != NULL) {
        t = (unsigned int)(ptrdiff_t)name;

        /* take care of popping containers if necessary */
        if ((t > 0x1000 && *name == ']') || t == ']') {
            if (mrp_json_get_type(c) == MRP_JSON_ARRAY) {
                c = pop_container(&stack, "]");
                continue;
            }
            else {
                errno = EILSEQ;
                goto fail;
            }
        }
        else if ((t > 0x1000 && *name == '}') || t == '}') {
            if (mrp_json_get_type(c) == MRP_JSON_OBJECT) {
                c = pop_container(&stack, "}");
                continue;
            }
            else {
                errno = EILSEQ;
                goto fail;
            }
        }

        if (c == NULL)
            goto fail;

        ptr = NULL;
        obj = NULL;

        switch (mrp_json_get_type(c)) {
        case MRP_JSON_ARRAY:
            if (t > 0x1000) {
                if (name[0] == '&') {
                    if ((name[1] == '{' || name[1] == '[') && name[2] == '\0') {
                        ptr = va_arg(ap, void **);
                        t   = name[1];
                        goto add_item;
                    }
                }

                if ((name[0] == '{' || name[0] == '[') && name[1] == '\0') {
                    t = name[0];
                    goto add_item;
                }

                errno = EINVAL;
                goto fail;
            }

        add_item:
            switch (t) {
            case '{':
            case MRP_JSON_OBJECT:
                c = push_container(&stack, NULL,
                                   mrp_json_create(MRP_JSON_OBJECT));
                if (ptr != NULL)
                    *ptr = c;
                break;
            case '[':
            case MRP_JSON_ARRAY:
                c = push_container(&stack, NULL,
                                   mrp_json_create(MRP_JSON_ARRAY));
                if (ptr != NULL)
                    *ptr = c;
                break;
            case MRP_JSON_STRING:
                mrp_json_array_append_string(c, va_arg(ap, const char *));
                break;
            case MRP_JSON_BOOLEAN:
                mrp_json_array_append_boolean(c, va_arg(ap, int));
                break;
            case MRP_JSON_INTEGER:
                mrp_json_array_append_integer(c, va_arg(ap, int));
                break;
            case MRP_JSON_DOUBLE:
                mrp_json_array_append_double(c, va_arg(ap, double));
                break;
            default:
                errno = EINVAL;
                goto fail;
            }
            break;

        case MRP_JSON_OBJECT:
            type = va_arg(ap, const char *);
            t    = (unsigned int)(ptrdiff_t)type;

            if (t > 0x1000) {
                if (type[0] == '&') {
                    if ((type[1] == '{' || type[1] == '[') && type[2] == '\0') {
                        ptr = va_arg(ap, void **);
                        t   = type[1];
                        goto add_member;
                    }
                }

                if ((type[0] == '{' || type[0] == '[') && type[1] == '\0') {
                    t = type[0];
                    goto add_member;
                }

                errno = EINVAL;
                goto fail;
            }

        add_member:
            switch (t) {
            case '{':
            case MRP_JSON_OBJECT:
                c = push_container(&stack, name,
                                   mrp_json_create(MRP_JSON_OBJECT));
                if (ptr != NULL)
                    *ptr = c;
                break;
            case '[':
            case MRP_JSON_ARRAY:
                c = push_container(&stack, name,
                                   mrp_json_create(MRP_JSON_ARRAY));
                if (ptr != NULL)
                    *ptr = c;
                break;
            case MRP_JSON_STRING:
                mrp_json_add_string(c, name, va_arg(ap, const char *));
                break;
            case MRP_JSON_BOOLEAN:
                mrp_json_add_boolean(c, name, va_arg(ap, int));
                break;
            case MRP_JSON_INTEGER:
                mrp_json_add_integer(c, name, va_arg(ap, int));
                break;
            case MRP_JSON_DOUBLE:
                mrp_json_add_double(c, name, va_arg(ap, double));
                break;
            default:
                errno = EINVAL;
                goto fail;
            }

            break;

        default:
            goto fail;
        }
    }

    return o;

 fail:
    mrp_json_unref(o);
    return NULL;
}
