#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2s_pdm.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "i2s_core.h"
#include "audio_core.h"
#include "audio_generators.h"
#include "DSCore.h"
#include "Sintetuzo.h"
#include "../sounds/thunder_strike.h"
#include "io.h"

static const char *TAG = __FILE__;

void app_main(void) {

    /////////////////////////////////////////////
	// Audio hardware setup 
    i2s_setup();
    /////////////////////////////////////////////
	// Synth type info 
    printTypeInfo();
    /////////////////////////////////////////////
	// Synth patch setup 
    SintetuzoSetup();
	/////////////////////////////////////////////
	// Synth core setup 
	DSCoreSetup();  

    // Configurar generador de audio con la música
    array_gen_state_t wavetable_state = {
        .data = audio_data,
        .total = audio_data_len,
        .offset = 0,
        .loop = true,
    };  
    audio_generator_t wavetable_gen = {
        .state = &wavetable_state,
        .generate = wavetable_generate,
    };  

    // Síntesis
    synth_gen_state_t synthesiser_state = {
        .freq_hz     = 440.0f,
        .sample_rate = SAMPLE_RATE,
        .phase       = 0.0f,
        .amplitude   = 0.2f,
    };
    audio_generator_t synthesiser_gen = {
        .state    = &synthesiser_state,
        .generate = synthesiser_generate,
    };

    set_audio_generator(&wavetable_gen);

    start_audio_engine(get_i2s_tx_handle());

    while (1) {
        ESP_LOGI(TAG, "Reproducción completada");
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}