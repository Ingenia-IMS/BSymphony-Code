#include <stdbool.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"

#include "imu/imu_manager.h"
#include "leds/led_manager.h"
#include "sonido/sound_player.h"
#include "elementos/cube_state.h"
#include "IR/ir_raw_test.h"

/*
 * CAMBIA ESTAS DOS LÍNEAS PARA PROGRAMAR CADA CUBO:
 *
 * Cubo A:
 *   TEST_MODE = IR_RAW_MODE_TX_SWEEP
 *
 * Cubo B:
 *   TEST_MODE = IR_RAW_MODE_RX_SWEEP
 */
#define TEST_MODE   IR_RAW_MODE_RX_SWEEP
#define TEST_FACE   IR_RAW_FACE_0

#define MAIN_TASK_DELAY_MS 100

static const char *TAG = "MAIN_IR_RAW";

static led_color_t face_color(ir_raw_face_t face)
{
    switch (face) {
        case IR_RAW_FACE_0: return LED_COLOR_BLUE;
        case IR_RAW_FACE_1: return LED_COLOR_LIGHT_BLUE;
        case IR_RAW_FACE_2: return LED_COLOR_PURPLE;
        case IR_RAW_FACE_3: return LED_COLOR_PINK;
        default: return LED_COLOR_WHITE;
    }
}

static void update_led_from_rx_stats(void)
{
    ir_raw_stats_t st;
    if (!ir_raw_test_get_stats(&st)) {
        return;
    }

    if (!st.any_hit) {
        led_manager_set_solid(LED_COLOR_RED);
        return;
    }

    led_manager_set_solid(face_color(st.best_face));
}

void app_main(void)
{
    ESP_LOGI(TAG, "Prueba RAW IR lenta anti-watchdog");
    ESP_LOGI(TAG, "Modo: %s | Face fija: %d", ir_raw_mode_name(TEST_MODE), TEST_FACE);

    /*
     * Mantengo los init principales, pero no arranco imu_start_task()
     * para aislar totalmente la prueba IR.
     */
    led_manager_init();
    led_manager_set_master_brightness(51);
    led_manager_set_blink_enabled(false);
    led_manager_set_solid(LED_COLOR_WARM_WHITE);

    sound_player_init();
    cube_state_init();
    imu_init();

    ir_raw_test_init();
    ir_raw_test_start(TEST_MODE, TEST_FACE);

    while (1) {
        if (TEST_MODE == IR_RAW_MODE_RX_FIXED || TEST_MODE == IR_RAW_MODE_RX_SWEEP) {
            update_led_from_rx_stats();
        } else {
            if (TEST_MODE == IR_RAW_MODE_TX_FIXED) {
                led_manager_set_solid(face_color(TEST_FACE));
            } else {
                led_manager_set_solid(LED_COLOR_YELLOW);
            }
        }

        vTaskDelay(pdMS_TO_TICKS(MAIN_TASK_DELAY_MS));
    }
}
