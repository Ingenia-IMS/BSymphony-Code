#include "sonido/sound_catalog.h"

#include "../sounds_h/agua.h"
#include "../sounds_h/electricidad.h"
#include "../sounds_h/fuego.h"
#include "../sounds_h/humano.h"
#include "../sounds_h/monkey.h"
#include "../sounds_h/naturaleza.h"
#include "../sounds_h/oeste.h"
#include "../sounds_h/pajaro.h"

// Descomenta estos cuando quieras meter la otra mitad
// #include "../sounds_h/piedra.h"
// #include "../sounds_h/pistola.h"
// #include "../sounds_h/reggaeton.h"
// #include "../sounds_h/robot.h"
// #include "../sounds_h/rock.h"
// #include "../sounds_h/tormenta.h"
// #include "../sounds_h/viento.h"

#define SOUND_LIST      \
    X(agua)             \
    X(electricidad)     \
    X(fuego)            \
    X(humano)           \
    X(monkey)           \
    X(naturaleza)       \
    X(oeste)            \
    X(pajaro)
/*
    Descomenta estas líneas cuando metas esos sonidos:
    X(piedra)       \
    X(pistola)      \
    X(reggaeton)    \
    X(robot)        \
    X(rock)         \
    X(tormenta)     \
    X(viento)
*/
#define X(name) { #name, audio_##name, audio_##name##_len, audio_##name##_sample_rate },

const sound_entry_t sound_table[] = {
    SOUND_LIST
};

#undef X

const size_t sound_table_count =
    sizeof(sound_table) / sizeof(sound_table[0]);