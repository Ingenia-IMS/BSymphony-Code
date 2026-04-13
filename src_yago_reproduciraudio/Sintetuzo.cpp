/*
 * Sintetuzo.cpp
 *
 * Created: 30/01/2016 13:17:24
 * Copyright 2016 Yago Torroja
 */ 

#include "Sintetuzo.h"
#include "LightCoder.hpp"
#include "DSCore.h"
#include "Phasor.hpp"
#include "Oscilator.hpp"
#include "Signal.hpp"
#include "Voice.hpp"
#include "Noise.hpp"
#include "Mixer.hpp"
#include "Filter.hpp"
#include "Adsr.hpp"
#include "Ar.hpp"
#include "Lfo.hpp"
#include "Vca.hpp"
#include "MIDI.hpp"

static const char* TAG = __FILE__;

LightCoder lightCoder(BEACON_LED, 1000);

MIDI	midi;

Cable	midiDac1;
Cable	midiDac2;
Cable	midiDac3;
Cable	midiDac4;

Cable	midiCC_A1;
Cable	midiCC_D1;
Cable	midiCC_S1;
Cable	midiCC_R1;

Cable	midiCC_L1;

Cable	midiCC_FF;
Cable	midiCC_FQ;

Cable	midiCC_A2;
Cable	midiCC_R2;

Cable	midiCC_A3;
Cable	midiCC_R3;

Cable	midiCC_M1;
Cable	midiCC_M2;
Cable	midiCC_M3;

Noise	noise;

Voice	v_1;
Voice	v_2;
Voice	v_3;
Voice	v_4;

Ar		noiseEnv;
Ar		kick;

Vca 	noiseVca;
Vca 	preFilterVca;

Lfo     lfo;

Filter  filter;

Mixer<4>	kbdMixer;
Mixer<3>	finalMixer;

#define FINAL_OUT finalMixer

signal_t getNextSample() {
	return FINAL_OUT.get();
}

signal_t * getNextSampleAddress() {
	return FINAL_OUT.getAddress();
}

#ifdef PROFILE
	#define profile(A, B) A.doProfile(A, (char *)B)
#else 
    #define profile(A, B) A.next()
#endif

void computeNextSample() {

	
	profile(v_1, "Voice");
	profile(v_2, "Voice");
	profile(v_3, "Voice");
	profile(v_4, "Voice");

	profile(kbdMixer, "Mixer4");
    
	profile(preFilterVca, "FilterVCA");
	profile(filter, "Filter");
	
	profile(lfo, "Lfo"); 
    
	profile(noise,  "Noise");
	profile(noiseEnv, "NoiseEnv");
	profile(noiseVca, "NoiseVca");

	profile(kick, "KickEnv");

	profile(finalMixer, "Mixer3");
    
 	midi.next();
}

void SintetuzoSetup() {

	/////////////////////////////////////////////
	// Midi voices and controls configuration 
	
	// Voices
	midi.addVoice(midiDac1, 0);
	midi.addVoice(midiDac2, 0);
	midi.addVoice(midiDac3, 0);
	midi.addVoice(midiDac4, 0);
	
	// Voices bindings
	v_1.attachCv(midiDac1);
	v_2.attachCv(midiDac2);
	v_3.attachCv(midiDac3);
	v_4.attachCv(midiDac4);

	v_1.setType(SAW);
	v_2.setType(SAW);
	v_3.setType(SAW);
	v_4.setType(SAW);
	
	// Gates and trigger bindings
	v_1.attachGate(midi.getVoiceGate(0));
	v_2.attachGate(midi.getVoiceGate(1));
	v_3.attachGate(midi.getVoiceGate(2));
	v_4.attachGate(midi.getVoiceGate(3));


	// Controllers
	midi.addController(midiCC_A1, 0, 0x40, &Signal::expInvSetting);
	midi.addController(midiCC_D1, 0, 0x41, &Signal::expInvSetting);
	midi.addController(midiCC_S1, 0, 0x42, &Signal::msbSetting);
	midi.addController(midiCC_R1, 0, 0x43, &Signal::expInvSetting);

	midi.addController(midiCC_L1, 0, 0x44, &Signal::lsbSetting);

	midi.addController(midiCC_FF, 0, 0x45, &Signal::msbSetting);
	midi.addController(midiCC_FQ, 0, 0x46, &Signal::msbInvSetting);
	
	midi.addController(midiCC_A2, 1, 0x40, &Signal::expInvSetting);
	midi.addController(midiCC_R2, 1, 0x43, &Signal::expInvSetting);

	midi.addController(midiCC_A3, 2, 0x40, &Signal::expInvSetting);
	midi.addController(midiCC_R3, 2, 0x43, &Signal::expInvSetting);

	midi.addController(midiCC_M1, 0, 0x07, &Signal::msbSetting);
	midi.addController(midiCC_M2, 1, 0x07, &Signal::msbSetting);
	midi.addController(midiCC_M3, 2, 0x07, &Signal::msbSetting);
	
	// Controllers bindings
	lfo.attachCv(midiCC_L1);
	lfo.setPrescaler(256);
	lfo.setType(TRI);
	
	v_1.osc.attachPwmCv(lfo);

	noiseEnv.attachAttack(midiCC_A2);
	noiseEnv.attachRelease(midiCC_R2);

	kick.attachAttack(midiCC_A3);
	kick.attachRelease(midiCC_R3);
		
	finalMixer.attachVolume(0, midiCC_M1);
	finalMixer.attachVolume(1, midiCC_M2);
	finalMixer.attachVolume(2, midiCC_M3);
			
 	filter.attachFreqCv(midiCC_FF);
 	filter.attachResoCv(midiCC_FQ);

	v_1.env.attachAttack(midiCC_A1);
	v_1.env.attachDecay(midiCC_D1);
	v_1.env.attachSustain(midiCC_S1);
	v_1.env.attachRelease(midiCC_R1);

	v_2.env.attachAttack(midiCC_A1);
	v_2.env.attachDecay(midiCC_D1);
	v_2.env.attachSustain(midiCC_S1);
	v_2.env.attachRelease(midiCC_R1);

	v_3.env.attachAttack(midiCC_A1);
	v_3.env.attachDecay(midiCC_D1);
	v_3.env.attachSustain(midiCC_S1);
	v_3.env.attachRelease(midiCC_R1);

	v_4.env.attachAttack(midiCC_A1);
	v_4.env.attachDecay(midiCC_D1);
	v_4.env.attachSustain(midiCC_S1);
	v_4.env.attachRelease(midiCC_R1);

	/////////////////////////////////////////////
	// Routing and settings

	kbdMixer.attachInput(0, v_1);
	kbdMixer.attachInput(1, v_2);
	kbdMixer.attachInput(2, v_3);
	kbdMixer.attachInput(3, v_4);

	kbdMixer.setVolume(0, p_toSignal(100));
	kbdMixer.setVolume(1, p_toSignal(100));
	kbdMixer.setVolume(2, p_toSignal(100));
	kbdMixer.setVolume(3, p_toSignal(100));

	v_1.env.setValues(p_toSignal(90), p_toSignal(90), p_toSignal(50), p_toSignal(50));
	v_2.env.setValues(p_toSignal(90), p_toSignal(90), p_toSignal(50), p_toSignal(50));
	v_3.env.setValues(p_toSignal(90), p_toSignal(90), p_toSignal(50), p_toSignal(50));
	v_4.env.setValues(p_toSignal(90), p_toSignal(90), p_toSignal(50), p_toSignal(50));

	noiseEnv.attachGate(midi.getChannelGate(1));
	noiseEnv.setValues(p_toSignal(90), p_toSignal(30));
	
	kick.attachGate(midi.getChannelGate(2));
	kick.setValues(p_toSignal(100), p_toSignal(30));

	noiseVca.attachInput(noise);
	noiseVca.attachCv(noiseEnv);

	preFilterVca.attachInput(kbdMixer);
	preFilterVca.setCv(p_toSignal(90));
	filter.attachInput(preFilterVca);
		
	finalMixer.attachInput(0, filter);
	finalMixer.attachInput(1, noiseVca);
	finalMixer.attachInput(2, kick);

	finalMixer.setVolume(0, p_toSignal(95));
	finalMixer.setVolume(1, p_toSignal(95));
	finalMixer.setVolume(2, p_toSignal(95));

	noiseVca.setCv(p_toSignal(95));
	noiseVca.attachCv(noiseEnv);

}

void SintetuzoLoop() {
	midi.update();
}

void printTypeInfo() {
	#define TYPE_NAME(A)  #A
	ESP_LOGI(TAG, "Base type info (supporting base type)-------");
	ESP_LOGI(TAG, "Base type bits:    %d", numBits(signal_t));
	ESP_LOGI(TAG, "Max base value:    0x%08x", TYPE_MAX_VALUE);
	ESP_LOGI(TAG, "Min base value:    0x%08x", TYPE_MIN_VALUE);
	ESP_LOGI(TAG, "Signal type info (signal Normal value is +1/-1) -------");
	ESP_LOGI(TAG, "Data width:        %d", DATA_WIDTH);
	ESP_LOGI(TAG, "Fract bits:        %d", FRACT_BITS-1);
	ESP_LOGI(TAG, "Headroom bits:     %d", HEADROOM_BITS);
	ESP_LOGI(TAG, "Normalizing shift: %d", SCALING_SHIFT);
	ESP_LOGI(TAG, "Multiplying shift: %d", MULT_SHIFT);
	ESP_LOGI(TAG, "Negative bit mask: 0x%08x", SIGN_BIT_MASK);
	ESP_LOGI(TAG, "Max Normal value:  0x%08x", SIGNAL_MAX_VALUE);
	ESP_LOGI(TAG, "Min Normal value:  0x%08x", SIGNAL_MIN_VALUE);
	ESP_LOGI(TAG, "Lsb bit mask:      0x%08x", midiToLsb(0x7F));
	ESP_LOGI(TAG, "Msb bit mask:      0x%08x", midiToMsb(0x7F));
	ESP_LOGI(TAG, "100%% value:        0x%08x", p_toSignal(100));
	ESP_LOGI(TAG, "Max headrom:       %d%%", (2<<(DATA_WIDTH - 1 - FRACT_BITS))*100);
	ESP_LOGI(TAG, "I2S bit mask:      0x%08x", 0xFFFF << I2S_SHIFT);
	ESP_LOGI(TAG, "Sigma_D bit mask:  0x%08x", 0xFF << SIGMA_D_SHIFT);
}
