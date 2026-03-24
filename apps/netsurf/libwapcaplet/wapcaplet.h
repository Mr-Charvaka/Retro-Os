#ifndef LIWAPCAPLET_H
#define LIWAPCAPLET_H

#include <stddef.h>
#include <stdint.h>

/* Forward declare the lwc_string type */
typedef struct lwc_string lwc_string;

/* Error codes */
typedef enum {
    LWC_OK = 0,
    LWC_OOM = 1,
    LWC_NOTFOUND = 2
} lwc_error;

/* Interning functions */
lwc_error lwc_intern_string(const char *data, size_t len, lwc_string **str);
lwc_error lwc_intern_substring(lwc_string *parent, size_t offset, size_t len, lwc_string **str);

/* String operations */
void lwc_string_ref(lwc_string *str);
void lwc_string_unref(lwc_string *str);

const char *lwc_string_data(const lwc_string *str);
size_t lwc_string_length(const lwc_string *str);
uint32_t lwc_string_hash(const lwc_string *str);

/* Comparison (pointer comparison is the strongest part of LibWapcaplet) */
#define lwc_string_isequal(s1, s2, res) (*(res) = ((s1) == (s2)))

#endif /* LIWAPCAPLET_H */
