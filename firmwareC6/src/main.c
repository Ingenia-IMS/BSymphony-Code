#include "imu/imu_manager.h"
<<<<<<< HEAD
=======
#include "leds/led_manager.h"

>>>>>>> main
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

<<<<<<< HEAD
static const char *TAG = "APP_MAIN";

static void accion_reproducir_sonido_golpe(void)
{
    ESP_LOGI(TAG, "Evento detectado: GOLPE -> reproducir sonido");
}

static void accion_color_siguiente(void)
{
    ESP_LOGI(TAG, "Evento detectado: SHAKE -> cambiar al siguiente color");
}

static void accion_color_fijo_encendido(void)
{
    ESP_LOGI(TAG, "Evento detectado: PICKUP o LED ON -> color fijo encendido");
}

static void accion_led_apagado(void)
{
    ESP_LOGI(TAG, "LED apagado");
=======
#include <stdint.h>
#include <stdbool.h>

static const char *TAG = "IMU_TEST";

#define LOOP_MS 20
#define BLINK_DURATION_MS 5000

static uint32_t now_ms(void)
{
    return (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
>>>>>>> main
}

void app_main(void)
{
<<<<<<< HEAD
    int blink_activo = 1;
    int blink_estado = 1;
    int blink_contador = 0;

    imu_init();
    ESP_LOGI(TAG, "Sistema iniciado");
=======
    uint32_t blink_until_ms = 0;
    bool blink_active = false;

    ESP_LOGI(TAG, "Iniciando prueba simple IMU");

    led_manager_init();
    led_manager_set_blink_enabled(false);
    led_manager_set_off();

    imu_init();
>>>>>>> main

    while (1) {
        imu_update();

<<<<<<< HEAD
        if (imu_take_hit_event()) {
            accion_reproducir_sonido_golpe();
        }

        if (imu_take_shake_event()) {
            accion_color_siguiente();
        }

        if (imu_take_pickup_event()) {
            blink_activo = 0;
            accion_color_fijo_encendido();
            ESP_LOGI(TAG, "Evento detectado: PICKUP -> desactivar parpadeo");
        }

        if (imu_take_putdown_event()) {
            blink_activo = 1;
            blink_estado = 1;
            ESP_LOGI(TAG, "Evento detectado: PUTDOWN -> activar parpadeo");
        }

        if (blink_activo) {
            blink_contador++;
            if (blink_contador >= 25) {
                blink_contador = 0;
                blink_estado = !blink_estado;

                if (blink_estado) {
                    accion_color_fijo_encendido();
                } else {
                    accion_led_apagado();
                }
            }
        }

        vTaskDelay(pdMS_TO_TICKS(20));
=======
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
>>>>>>> main
    }
}