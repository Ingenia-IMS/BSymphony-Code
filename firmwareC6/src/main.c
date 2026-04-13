#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_err.h"

#define AMP_STANDBY_GPIO GPIO_NUM_20   // D9 -> !standby
#define AUDIO_GPIO       GPIO_NUM_21   // D3 -> audio out

#define TONE_FREQ_HZ     2000          // pitido de 2 kHz
#define DUTY_RES         LEDC_TIMER_10_BIT
#define DUTY_50_PERCENT  ((1 << 10) / 2)   // 512 con 10 bits

void app_main(void)
{

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}