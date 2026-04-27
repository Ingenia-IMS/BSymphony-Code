#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"

#include "esp_log.h"

#include "imu/imu_manager.h"
#include "leds/led_manager.h"
#include "sonido/sound_player.h"
#include "elementos/element_catalog.h"
#include "elementos/cube_state.h"

#define CHANGE_BLINK_DURATION_MS 1000

static const char *TAG = "MAIN";

static TimerHandle_t change_element_timer = NULL;

static void change_element_timer_callback(TimerHandle_t timer)
{
    (void)timer;

    ESP_LOGI(TAG, "Fin de parpadeo -> cambiar elemento");

    led_manager_set_blink_enabled(false);

    cube_state_next_element();
    cube_state_play_current_sound();
}

static void on_pickup_detected(void)
{
    ESP_LOGI(
        TAG,
        "Callback IMU: coger/mover suave -> sonido del elemento actual: %s",
        cube_state_get_current_name()
    );

    cube_state_play_current_sound();
}

static void on_strong_shake_detected(void)
{
    ESP_LOGI(TAG, "Callback IMU: agitado vigoroso -> parpadeo y cambio de elemento");

    cube_state_apply_current_light();
    led_manager_set_blink_enabled(true);

    if (change_element_timer != NULL) {
        xTimerStop(change_element_timer, 0);
        xTimerStart(change_element_timer, 0);
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

    sound_player_init();

    element_catalog_init();
    cube_state_init();

    change_element_timer = xTimerCreate(
        "change_element_timer",
        pdMS_TO_TICKS(CHANGE_BLINK_DURATION_MS),
        pdFALSE,
        NULL,
        change_element_timer_callback
    );

    if (change_element_timer == NULL) {
        ESP_LOGE(TAG, "No se pudo crear change_element_timer");
    }

    imu_set_pickup_callback(on_pickup_detected);
    imu_set_shake_callback(on_strong_shake_detected);

    imu_start_task();

    ESP_LOGI(TAG, "Sistema listo");
    ESP_LOGI(TAG, "Elemento actual: %s", cube_state_get_current_name());
}