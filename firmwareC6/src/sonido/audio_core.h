#pragma once

#include "i2s_core.h"

#define BUFFER_SAMPLES  256
#define BUFFER_COUNT    2   // ping-pong

#define start_audio() start_audio_engine(get_i2s_tx_handle())
void start_audio_engine(i2s_chan_handle_t i2s_tx_handle);

// Tipo genérico de generador
typedef struct {
    void *state;                                           // estado interno del generador
    void (*generate)(void *state, int16_t *buf, size_t n); // función de generación
} audio_generator_t;

void set_audio_generator(audio_generator_t *gen);
