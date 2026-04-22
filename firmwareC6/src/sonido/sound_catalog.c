#include "sound_catalog.h"

#include "../sounds_h/cute_melody.h"
#include "../sounds_h/fire_forest.h"
#include "../sounds_h/music.h"

#define SOUND_LIST \
    X(cute_melody)    \
    X(fire_forest)      \
    X(music)

#define X(name) { #name, audio_##name, audio_##name##_len, audio_##name##_sample_rate },

const sound_def_t sound_table[] = {
    SOUND_LIST
};

#undef X

const size_t sound_table_count = sizeof(sound_table) / sizeof(sound_table[0]);