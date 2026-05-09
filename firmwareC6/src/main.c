#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"

#include "imu/imu_manager.h"
#include "leds/led_manager.h"

static const char *TAG = "TEST_MIN";

void app_main(void)
{
    ESP_LOGI(TAG, "TEST MINIMO: IMU + LEDS, SIN AUDIO");

    ESP_LOGI(TAG, "Init IMU...");
    imu_init();

    ESP_LOGI(TAG, "Init LEDs...");
    led_manager_init();

    led_manager_set_master_brightness(30);
    led_manager_set_solid(LED_COLOR_BLUE);

    ESP_LOGI(TAG, "Start IMU task...");
    imu_start_task();

    ESP_LOGI(TAG, "Sistema arrancado. Mueve el cubo para probar IMU.");

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}