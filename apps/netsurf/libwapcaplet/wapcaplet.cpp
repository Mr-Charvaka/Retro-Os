#include "wapcaplet.h"
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

struct lwc_string {
    char *data;
    size_t len;
    uint32_t hash;
    uint32_t refs;
    struct lwc_string *next;
};

#define HASH_TABLE_SIZE 1024
static struct lwc_string *hash_table[HASH_TABLE_SIZE];

/* Jenkins-style hash for string interning */
static uint32_t lwc_string_hash_data(const char *data, size_t len) {
    uint32_t hash = 0x811c9dc5;
    for (size_t i = 0; i < len; i++) {
        hash ^= (uint8_t)data[i];
        hash *= 0x01000193;
    }
    return hash;
}

lwc_error lwc_intern_string(const char *data, size_t len, lwc_string **str) {
    uint32_t hash = lwc_string_hash_data(data, len);
    uint32_t bucket = hash % HASH_TABLE_SIZE;

    /* Search existing */
    struct lwc_string *curr = hash_table[bucket];
    while (curr) {
        if (curr->len == len && memcmp(curr->data, data, len) == 0) {
            curr->refs++;
            *str = curr;
            return LWC_OK;
        }
        curr = curr->next;
    }

    /* Create new */
    struct lwc_string *new_str = (struct lwc_string *)malloc(sizeof(struct lwc_string));
    if (!new_str) return LWC_OOM;

    new_str->data = (char *)malloc(len + 1);
    if (!new_str->data) {
        free(new_str);
        return LWC_OOM;
    }

    memcpy(new_str->data, data, len);
    new_str->data[len] = '\0';
    new_str->len = len;
    new_str->hash = hash;
    new_str->refs = 1;
    new_str->next = hash_table[bucket];
    hash_table[bucket] = new_str;

    *str = new_str;
    return LWC_OK;
}

void lwc_string_ref(lwc_string *str) {
    if (str) str->refs++;
}

void lwc_string_unref(lwc_string *str) {
    if (!str) return;
    str->refs--;
    /* Note: In a real system, we might gargabe-collect when refs == 0 */
}

const char *lwc_string_data(const lwc_string *str) {
    return str ? str->data : NULL;
}

size_t lwc_string_length(const lwc_string *str) {
    return str ? str->len : 0;
}

uint32_t lwc_string_hash(const lwc_string *str) {
    return str ? str->hash : 0;
}
