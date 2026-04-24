#include "imu/imu_manager.h"
#include "leds/led_manager.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#include <stdint.h>
#include <stdbool.h>

static const char *TAG = "IMU_TEST";

#define LOOP_MS 20
#define BLINK_DURATION_MS 5000

static uint32_t now_ms(void)
{
    return (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
}

void app_main(void)
{
    uint32_t blink_until_ms = 0;
    bool blink_active = false;

    ESP_LOGI(TAG, "Iniciando prueba simple IMU");

    led_manager_init();
    led_manager_set_blink_enabled(false);
    led_manager_set_off();

    imu_init();

    while (1) {
        imu_update();

        uint32_t now = now_ms();

        if (imu_take_sound_event()) {
            ESP_LOGI(TAG, "EVENTO: CUBO COGIDO / MOVIDO -> reproducir sonido");
        }

        if (imu_take_blink_event()) {
            ESP_LOGI(TAG, "EVENTO: SHAKE VIGOROSO -> activar parpadeo 5s");

            led_manager_set_solid(LED_COLOR_BLUE);
            led_manager_set_blink_enabled(true);

            blink_active = true;
            blink_until_ms = now + BLINK_DURATION_MS;
        }

        if (blink_active && now >= blink_until_ms) {
            ESP_LOGI(TAG, "Fin de parpadeo");

            led_manager_set_blink_enabled(false);
            led_manager_set_off();

            blink_active = false;
        }

        vTaskDelay(pdMS_TO_TICKS(LOOP_MS));
    }
}