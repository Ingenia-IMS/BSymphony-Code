#include "audio_core.h"
#include "audio_generators.h"

void wavetable_generate(void *state, int16_t *buf, size_t n)
{
    array_gen_state_t *s = (array_gen_state_t *)state;
    for (size_t i = 0; i < n; i++) {
        if (s->offset >= s->total) {
            if (s->loop) s->offset = 0;
            else { buf[i] = 0; continue; }
        }
        buf[i] = s->data[s->offset++];
    }
}

void synthesiser_generate(void *state, int16_t *buf, size_t n)
{
    synth_gen_state_t *s = (synth_gen_state_t *)state;
    float phase_inc = 2.0f * M_PI * s->freq_hz / s->sample_rate;
    for (size_t i = 0; i < n; i++) {
        buf[i] = (int16_t)(s->amplitude * 32767.0f * sinf(s->phase));
        s->phase += phase_inc;
        if (s->phase > 2.0f * M_PI) s->phase -= 2.0f * M_PI;
    }
}