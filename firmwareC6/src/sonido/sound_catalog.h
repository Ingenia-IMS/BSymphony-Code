#pragma once

#include <stddef.h>
#include <stdint.h>

typedef struct {
    const char *name;
    const int16_t *data;
    size_t len;
    uint32_t sample_rate;
} sound_def_t;

extern const sound_def_t sound_table[];
extern const size_t sound_table_count;