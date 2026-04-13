/*
 * DSCore.h
 *
 * Created: 30/01/2016 13:17:24
 * Copyright 2016 Yago Torroja
 */

#include <freertos/FreeRTOS.h>
#include <freertos/stream_buffer.h>
#include "Arduino_compat.h"
#include "i2s_core.h"
#include "audio_core.h"
#include "DSCore.h"

static const char* TAG = __FILE__;

hw_timer_t *glb_timer = NULL;

float dsFreqFactor = 0;

#define SAMPLES_PER_FRAME  BUFFER_SAMPLES

void DSCoreSetup() {
  ESP_LOGI(TAG, "Initializing Synthesiser");

  uint32_t real_timer_freq = configureTimer(0, NULL);  // No interrupts to measure sample computing time
  uint16_t cycles = computeCycles();
  ESP_LOGI(TAG, "Meassured cycles per synth sample: %d", cycles);

  cycles *= OVERHEAD_FACTOR;
  ESP_LOGI(TAG, "Final cycles per sample with overhead: %d", cycles);

  uint16_t sampling_freq = (uint16_t)(((float)real_timer_freq / cycles) < SAMPLE_RATE
                                      ? ((float)real_timer_freq / cycles)
                                      : SAMPLE_RATE);
  ESP_LOGI(TAG, "Setting sampling frequency to (Hz): %d", sampling_freq);

  dsFreqFactor = ((float)0x10000 /* uint16_t range */) / sampling_freq;
  ESP_LOGI(TAG, "Setting frequency factor to: %.3f", dsFreqFactor);


  int cycles_per_sample = (int)((float)real_timer_freq/sampling_freq + 0.5);
  int cycles_per_frame  = (int)((float)real_timer_freq * SAMPLES_PER_FRAME /sampling_freq + 0.5)-5;

  // configureTimer(cycles_per_frame, NULL);

  ESP_LOGI(TAG, "Setting samples per frame to: %d", SAMPLES_PER_FRAME);
  ESP_LOGI(TAG, "Setting cycles per frame to: %d", cycles_per_frame);

  ESP_LOGI(TAG, "Percentage of uP time for synthesis: %.2f", (float)cycles/cycles_per_sample*100);

}

uint16_t computeCycles() {
  uint32_t TT = 0;
  ESP_LOGI(TAG, "Meassuring Synth Sample Computing Time...");
  for (int i = 0; i < 1000; i++) {
    timerWrite(glb_timer, 0);
    computeNextSample();
    TT += timerRead(glb_timer) + SAVE_RESTORE_OVERHEAD;
  }
  timerStop(glb_timer);  // Stop counting, so profiling functions will know they have to report results
  computeNextSample();
  uint16_t cycles = TT / 1000;
  return cycles;
}

uint32_t configureTimer(uint16_t cycles, void (*timerCb)(void)) {
  #define DIVIDER 2
  if (glb_timer == NULL) {
    glb_timer = timerBegin(getApbFrequency() / 2);
  } else {
    timerStart(glb_timer);
  }
  if (glb_timer == NULL) {
    ESP_LOGE(TAG, "Error configuring timer");
  } else {
    ESP_LOGI(TAG, "Timer configured to (HZ): %d", getApbFrequency()/DIVIDER);
  }
  if (timerCb && cycles) {  
    timerAttachInterrupt(glb_timer, timerCb);
    timerAlarm(glb_timer, cycles, true, 0);
    ESP_LOGI(TAG, "Callback configured to execute every (timer cycles): %d", cycles);
  }
  return timerGetFrequency(glb_timer);
}