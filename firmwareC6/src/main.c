#include <stdbool.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"

#include "imu/imu_manager.h"
#include "leds/led_manager.h"
#include "sonido/sound_player.h"

#define MAIN_LOOP_MS        20
#define BLINK_DURATION_MS   5000
#define SOUND_NAME          "tormenta"

static const char *TAG = "MAIN";

static uint32_t now_ms(void)
{
    return (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
}

void app_main(void)
{
    ESP_LOGI(TAG, "Inicializando sistema...");

    imu_init();

    if (led_manager_init() != ESP_OK) {
        ESP_LOGE(TAG, "Error inicializando LEDs");
    }

    led_manager_set_master_brightness(80);

    // Estado normal: efecto tormenta fijo, sin parpadeo
    led_manager_set_storm();
    led_manager_set_blink_enabled(false);

    sound_player_init();

    bool blink_active = false;
    uint32_t blink_until_ms = 0;

    ESP_LOGI(TAG, "Sistema listo");
    ESP_LOGI(TAG, "LED en modo tormenta fijo");

    while (1) {
        imu_update();

        uint32_t now = now_ms();

        // -------------------------------------------------
        // Agitación vigorosa -> sonido
        // -------------------------------------------------
        if (imu_take_sound_event()) {
            ESP_LOGI(TAG, "IMU detecta AGITACION VIGOROSA -> reproducir sonido: %s", SOUND_NAME);
            sound_player_play(SOUND_NAME);
        }

        // -------------------------------------------------
        // Coger / mover cubo -> parpadeo tormenta 5 s
        // -------------------------------------------------
        if (imu_take_blink_event()) {
            ESP_LOGI(TAG, "IMU detecta COGER/MOVER CUBO -> activar parpadeo tormenta 5s");

            led_manager_set_storm();
            led_manager_set_blink_enabled(true);

            blink_active = true;
            blink_until_ms = now + BLINK_DURATION_MS;
        }

        // -------------------------------------------------
        // Fin del parpadeo -> vuelve a tormenta fija
        // -------------------------------------------------
        if (blink_active && now >= blink_until_ms) {
            ESP_LOGI(TAG, "Fin de parpadeo -> volver a tormenta fija");

            led_manager_set_blink_enabled(false);
            led_manager_set_storm();

            blink_active = false;
        }

        vTaskDelay(pdMS_TO_TICKS(MAIN_LOOP_MS));
    }
}