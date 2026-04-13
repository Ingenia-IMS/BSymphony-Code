/*
 * DSCore.h
 *
 * Created: 30/01/2016 13:17:24
 * Copyright 2016 Yago Torroja
 */ 

#ifndef DSCORE_H_
#define DSCORE_H_

#include "Sintetuzo.h"
#include "Arduino_compat.h"

#define OVERLOAD_PIN        9
#define OVERHEAD_FACTOR  1.20 // lets give 25% of overhead when computing samples

// TODO: Compute this overhead. No idea
#define SAVE_RESTORE_OVERHEAD            10
#define MAX_SAMPLE_FREQ  SAMPLE_RATE
#define SAMPLE_MIN_UNITS                800
#define TIMER_BASE_FREQ  (MAX_SAMPLE_FREQ*SAMPLE_MIN_UNITS)

extern hw_timer_t *glb_timer;
extern float dsFreqFactor;

void       DSCoreSetup();

uint16_t   computeCycles();
uint32_t   configureTimer(uint16_t cycles, void (*timerCb)(void));
void       configureAudioOut(int sample_freq);	

void       computeNextSample();
signal_t   getNextSample();
signal_t * getNextSampleAddress();

#endif /* DSCORE_H_ */