#include "sound_player.h"

#include <string.h>
#include <stdbool.h>
#include <stdint.h>

#include "driver/gpio.h"

#include "audio_core.h"
#include "i2s_core.h"

// Sonidos disponibles
#include "sounds_h/Tormenta.h"
// #include "sounds_h/Viento.h"

typedef struct {
    const char *name;
    const int16_t *data;
    size_t len;
} sound_def_t;

static const sound_def_t sound_table[] = {
    { "Tormenta", audio_Tormenta, audio_Tormenta_len },
    // { "Viento",   audio_Viento,   audio_Viento_len   },
};

static const size_t sound_table_count = sizeof(sound_table) / sizeof(sound_table[0]);

typedef struct {
    const int16_t *data;
    size_t total;
    size_t offset;
    bool loop;
    bool playing;
} sound_player_state_t;

static sound_player_state_t sound_state = {
    .data = NULL,
    .total = 0,
    .offset = 0,
    .loop = false,
    .playing = false
};

static void amp_enable(bool enable)
{
    gpio_set_level(SPK_ENA_PIN, enable ? 1 : 0);
}

static void sound_player_stop_internal(void)
{
    sound_state.data = NULL;
    sound_state.total = 0;
    sound_state.offset = 0;
    sound_state.loop = false;
    sound_state.playing = false;

    amp_enable(false);
}

static void sound_player_generate(void *state, int16_t *buf, size_t n)
{
    sound_player_state_t *s = (sound_player_state_t *)state;

    for (size_t i = 0; i < n; i++) {
        if (s->data == NULL || s->total == 0 || !s->playing) {
            buf[i] = 0;
            continue;
        }

        if (s->offset >= s->total) {
            if (s->loop) {
                s->offset = 0;
            } else {
                buf[i] = 0;

                // Apagar automáticamente al terminar
                sound_player_stop_internal();

                // Rellenar el resto del buffer con silencio
                for (size_t j = i + 1; j < n; j++) {
                    buf[j] = 0;
                }
                return;
            }
        } else {
            buf[i] = s->data[s->offset++];
        }
    }
}

static audio_generator_t sound_gen = {
    .state = &sound_state,
    .generate = sound_player_generate
};

void sound_player_init(void)
{
    i2s_setup();
    set_audio_generator(&sound_gen);
    start_audio_engine(get_i2s_tx_handle());

    sound_player_stop_internal();
}

bool sound_player_play(const char *sound_name)
{
    if (sound_name == NULL) {
        sound_player_stop_internal();
        return false;
    }

    for (size_t i = 0; i < sound_table_count; i++) {
        if (strcmp(sound_table[i].name, sound_name) == 0) {
            sound_state.data = sound_table[i].data;
            sound_state.total = sound_table[i].len;
            sound_state.offset = 0;
            sound_state.loop = false;
            sound_state.playing = true;

            amp_enable(true);
            return true;
        }
    }

    return false;
}

void sound_player_stop(void)
{
    sound_player_stop_internal();
}

bool sound_player_is_playing(void)
{
    return sound_state.playing;
}