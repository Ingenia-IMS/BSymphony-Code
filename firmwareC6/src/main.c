#include <stdbool.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"

#include "esp_log.h"

#include "imu/imu_manager.h"
#include "leds/led_manager.h"
#include "sonido/sound_player.h"

#define SOUND_NAME          "tormenta"
#define BLINK_DURATION_MS   5000

static const char *TAG = "MAIN";

static TimerHandle_t blink_timer = NULL;

static void stop_blink_timer_callback(TimerHandle_t timer)
{
    (void)timer;

    ESP_LOGI(TAG, "Fin de parpadeo -> volver a tormenta fija");

    led_manager_set_blink_enabled(false);
}

static void on_pickup_detected(void)
{
    ESP_LOGI(TAG, "Callback IMU: coger/mover suave -> reproducir sonido");

    // Se mantiene tu comportamiento deseado:
    // si ya hay un sonido, sound_player_play debe cortar el anterior y lanzar este.
    sound_player_play(SOUND_NAME);
}

static void on_strong_shake_detected(void)
{
    ESP_LOGI(TAG, "Callback IMU: agitado vigoroso -> parpadeo tormenta 5s");

    led_manager_set_blink_enabled(true);

    if (blink_timer != NULL) {
        xTimerStop(blink_timer, 0);
        xTimerStart(blink_timer, 0);
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "Inicializando sistema...");

    imu_init();

    if (led_manager_init() != ESP_OK) {
        ESP_LOGE(TAG, "Error inicializando LEDs");
    }

    led_manager_set_master_brightness(80);

    // Estado normal: tormenta fija
    led_manager_set_solid(LED_COLOR_BLUE);
    led_manager_set_blink_enabled(false);

    sound_player_init();

    blink_timer = xTimerCreate(
        "blink_timer",
        pdMS_TO_TICKS(BLINK_DURATION_MS),
        pdFALSE,
        NULL,
        stop_blink_timer_callback
    );

    if (blink_timer == NULL) {
        ESP_LOGE(TAG, "No se pudo crear blink_timer");
    }

    imu_set_pickup_callback(on_pickup_detected);
    imu_set_shake_callback(on_strong_shake_detected);

    imu_start_task();

    ESP_LOGI(TAG, "Sistema listo");
    ESP_LOGI(TAG, "Main sin bucle periódico: IMU, LEDs y audio trabajan en tasks");
}