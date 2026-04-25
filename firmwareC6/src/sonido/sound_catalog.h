#ifndef SOUND_CATALOG_H
#define SOUND_CATALOG_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const char *name;
    const int16_t *data;
    size_t len;
    uint32_t sample_rate;
} sound_entry_t;

extern const sound_entry_t sound_table[];
extern const size_t sound_table_count;

#ifdef __cplusplus
}
#endif

#endif