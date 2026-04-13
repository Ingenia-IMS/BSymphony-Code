#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "i2s_core.h"
#include "audio_core.h"
#include "audio_generators.h"
#include "sounds_h/Tormenta.h"

void app_main(void)
{
    i2s_setup();

    static array_gen_state_t sound_state = {
        .data = audio_Tormenta,
        .total = audio_Tormenta_len,
        .offset = 0,
        .loop = true
    };

    static audio_generator_t sound_gen = {
        .state = &sound_state,
        .generate = wavetable_generate
    };

    set_audio_generator(&sound_gen);
    start_audio_engine(get_i2s_tx_handle());

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}