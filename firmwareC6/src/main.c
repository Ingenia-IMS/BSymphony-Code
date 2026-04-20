#include "imu/imu_manager.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

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
}

void app_main(void)
{
    int blink_activo = 1;
    int blink_estado = 1;
    int blink_contador = 0;

    imu_init();
    ESP_LOGI(TAG, "Sistema iniciado");

    while (1) {
        imu_update();

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
    }
}