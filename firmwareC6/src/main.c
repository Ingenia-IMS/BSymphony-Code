#include <stdio.h>
#include <stdbool.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "driver/gpio.h"
#include "esp_log.h"

#include "leds/led_manager.h"

#define IR_SEL0_GPIO    GPIO_NUM_19   // D8
#define IR_SEL1_GPIO    GPIO_NUM_17   // D7
#define IR_RX_GPIO      GPIO_NUM_18   // D10
#define IR_TX_GPIO      GPIO_NUM_20   // D9

#define IR_FACE         1

// Cambiar a 1 si tu circuito da HIGH cuando recibe IR
#define IR_RX_ACTIVE_LEVEL 0

#define LED_BRIGHTNESS_20_PERCENT 51

static const char *TAG = "IR_TEST";

static void ir_select_face_1(void)
{
    gpio_set_level(IR_SEL0_GPIO, 1); // SEL0 = 1
    gpio_set_level(IR_SEL1_GPIO, 0); // SEL1 = 0
}

static void ir_gpio_init(void)
{
    gpio_config_t out_conf = {
        .pin_bit_mask =
            (1ULL << IR_SEL0_GPIO) |
            (1ULL << IR_SEL1_GPIO) |
            (1ULL << IR_TX_GPIO),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&out_conf);

    gpio_config_t in_conf = {
        .pin_bit_mask = (1ULL << IR_RX_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&in_conf);

    gpio_set_level(IR_TX_GPIO, 0);
    ir_select_face_1();
}

static void ir_tx_blink_task(void *arg)
{
    bool tx_on = false;

    while (1) {
        tx_on = !tx_on;
        gpio_set_level(IR_TX_GPIO, tx_on);

        // 2 Hz: 250 ms ON + 250 ms OFF
        vTaskDelay(pdMS_TO_TICKS(250));
    }
}

static void ir_rx_led_task(void *arg)
{
    bool last_detected = false;

    led_manager_set_master_brightness(LED_BRIGHTNESS_20_PERCENT);
    led_manager_set_off();

    while (1) {
        int rx_level = gpio_get_level(IR_RX_GPIO);
        bool detected = (rx_level == IR_RX_ACTIVE_LEVEL);

        if (detected != last_detected) {
            if (detected) {
                led_manager_set_solid(LED_COLOR_RED);
                ESP_LOGI(TAG, "IR detectado en cara %d", IR_FACE);
            } else {
                led_manager_set_off();
                ESP_LOGI(TAG, "IR no detectado");
            }

            last_detected = detected;
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "Test IR simple en cara %d", IR_FACE);

    ir_gpio_init();

    ESP_ERROR_CHECK(led_manager_init());
    ESP_ERROR_CHECK(led_manager_set_master_brightness(LED_BRIGHTNESS_20_PERCENT));
    ESP_ERROR_CHECK(led_manager_set_off());

    xTaskCreate(ir_tx_blink_task, "ir_tx_blink", 2048, NULL, 5, NULL);
    xTaskCreate(ir_rx_led_task, "ir_rx_led", 2048, NULL, 5, NULL);
}