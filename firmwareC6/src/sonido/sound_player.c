#include "sound_player.h"

#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#include "driver/gpio.h"

#include "audio_core.h"
#include "i2s_core.h"
#include "sound_catalog.h"

typedef struct {
    const int16_t *data;
    size_t total;
    uint32_t source_sample_rate;
    uint32_t position_q16;
    uint32_t step_q16;
    bool loop;
    bool playing;
} sound_player_state_t;

static sound_player_state_t sound_state = {
    .data = NULL,
    .total = 0,
    .source_sample_rate = 0,
    .position_q16 = 0,
    .step_q16 = 0,
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
    sound_state.source_sample_rate = 0;
    sound_state.position_q16 = 0;
    sound_state.step_q16 = 0;
    sound_state.loop = false;
    sound_state.playing = false;

    amp_enable(false);
}

static uint32_t compute_step_q16(uint32_t source_sample_rate)
{
    if (source_sample_rate == 0) {
        return 0;
    }

    uint64_t step = ((uint64_t)source_sample_rate << 16) / (uint64_t)SAMPLE_RATE;

    if (step == 0) {
        step = 1;
    }

    return (uint32_t)step;
}

static void sound_player_generate(void *state, int16_t *buf, size_t n)
{
    sound_player_state_t *s = (sound_player_state_t *)state;

    for (size_t i = 0; i < n; i++) {
        if (s->data == NULL || s->total == 0 || !s->playing || s->step_q16 == 0) {
            buf[i] = 0;
            continue;
        }

        size_t idx = (size_t)(s->position_q16 >> 16);
        uint32_t frac = s->position_q16 & 0xFFFF;

        if (idx >= s->total) {
            if (s->loop) {
                s->position_q16 = 0;
                idx = 0;
                frac = 0;
            } else {
                buf[i] = 0;
                sound_player_stop_internal();

                for (size_t j = i + 1; j < n; j++) {
                    buf[j] = 0;
                }
                return;
            }
        }

        int16_t s0 = s->data[idx];
        int16_t s1 = (idx + 1 < s->total) ? s->data[idx + 1] : s0;

        int32_t interp =
            ((int32_t)s0 * (int32_t)(65536U - frac) +
             (int32_t)s1 * (int32_t)frac) >> 16;

        buf[i] = (int16_t)interp;
        s->position_q16 += s->step_q16;
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
    if (sound_state.playing) {
        return false;
    }

    if (sound_name == NULL) {
        sound_player_stop_internal();
        return false;
    }

    for (size_t i = 0; i < sound_table_count; i++) {
        if (strcmp(sound_table[i].name, sound_name) == 0) {
            sound_state.data = sound_table[i].data;
            sound_state.total = sound_table[i].len;
            sound_state.source_sample_rate = sound_table[i].sample_rate;
            sound_state.position_q16 = 0;
            sound_state.step_q16 = compute_step_q16(sound_table[i].sample_rate);
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