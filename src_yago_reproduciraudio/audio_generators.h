#pragma once

#include "audio_core.h"

// Wave table generator: recorre un array de muestras
typedef struct {
    const int16_t *data;
    size_t         total;
    size_t         offset;
    bool           loop;
} array_gen_state_t;
void wavetable_generate(void *state, int16_t *buf, size_t n);

// Synth generator: genera tonos a partir de parámetros (frecuencia, amplitud, etc.)
#include <math.h>
typedef struct {
    float freq_hz;
    float sample_rate;
    float phase;        // estado entre bloques
    float amplitude;    // 0.0 - 1.0
} synth_gen_state_t;
void synthesiser_generate(void *state, int16_t *buf, size_t n);