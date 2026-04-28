#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "driver/gpio.h"
#include "driver/rmt_tx.h"

#include "esp_log.h"
#include "esp_err.h"

#define TAG "LED_TEST"

#define LED_GPIO   GPIO_NUM_16   // D7
#define LED_COUNT  4

// Orden físico:
// 0 = arriba izquierda
// 1 = abajo izquierda
// 2 = abajo derecha
// 3 = arriba derecha

static rmt_channel_handle_t led_chan = NULL;
static rmt_encoder_handle_t led_encoder = NULL;

// Buffer en formato GRB
static uint8_t led_data[LED_COUNT * 3];

static void leds_init(void)
{
    rmt_tx_channel_config_t tx_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .gpio_num = LED_GPIO,
        .mem_block_symbols = 64,
        .resolution_hz = 10000000, // 10 MHz -> 1 tick = 100 ns
        .trans_queue_depth = 4,
    };

    ESP_ERROR_CHECK(rmt_new_tx_channel(&tx_config, &led_chan));

    rmt_bytes_encoder_config_t encoder_config = {
        // 0: high corto, low largo
        .bit0 = {
            .level0 = 1,
            .duration0 = 3,  // 300 ns
            .level1 = 0,
            .duration1 = 9,  // 900 ns
        },
        // 1: high largo, low corto
        .bit1 = {
            .level0 = 1,
            .duration0 = 9,  // 900 ns
            .level1 = 0,
            .duration1 = 3,  // 300 ns
        },
        .flags.msb_first = 1,
    };

    ESP_ERROR_CHECK(rmt_new_bytes_encoder(&encoder_config, &led_encoder));
    ESP_ERROR_CHECK(rmt_enable(led_chan));

    ESP_LOGI(TAG, "LEDs inicializados en GPIO %d", LED_GPIO);
}

static void leds_set_pixel(int index, uint8_t r, uint8_t g, uint8_t b)
{
    if (index < 0 || index >= LED_COUNT) {
        return;
    }

    // Estos LEDs usan normalmente orden GRB, no RGB
    led_data[index * 3 + 0] = g;
    led_data[index * 3 + 1] = r;
    led_data[index * 3 + 2] = b;
}

static void leds_clear(void)
{
    memset(led_data, 0, sizeof(led_data));
}

static void leds_show(void)
{
    rmt_transmit_config_t transmit_config = {
        .loop_count = 0,
    };

    ESP_ERROR_CHECK(rmt_transmit(
        led_chan,
        led_encoder,
        led_data,
        sizeof(led_data),
        &transmit_config
    ));

    ESP_ERROR_CHECK(rmt_tx_wait_all_done(led_chan, portMAX_DELAY));

    // Reset/latch >300 us
    vTaskDelay(pdMS_TO_TICKS(1));
}

void app_main(void)
{
    leds_init();

    while (1) {
        ESP_LOGI(TAG, "Todos rojo");
        leds_clear();
        for (int i = 0; i < LED_COUNT; i++) {
            leds_set_pixel(i, 80, 0, 0);
        }
        leds_show();
        vTaskDelay(pdMS_TO_TICKS(1000));

        ESP_LOGI(TAG, "Todos verde");
        leds_clear();
        for (int i = 0; i < LED_COUNT; i++) {
            leds_set_pixel(i, 0, 80, 0);
        }
        leds_show();
        vTaskDelay(pdMS_TO_TICKS(1000));

        ESP_LOGI(TAG, "Todos azul");
        leds_clear();
        for (int i = 0; i < LED_COUNT; i++) {
            leds_set_pixel(i, 0, 0, 80);
        }
        leds_show();
        vTaskDelay(pdMS_TO_TICKS(1000));

        ESP_LOGI(TAG, "Prueba posiciones");

        // 0 arriba izquierda
        leds_clear();
        leds_set_pixel(0, 80, 0, 0);
        leds_show();
        vTaskDelay(pdMS_TO_TICKS(700));

        // 1 abajo izquierda
        leds_clear();
        leds_set_pixel(1, 0, 80, 0);
        leds_show();
        vTaskDelay(pdMS_TO_TICKS(700));

        // 2 abajo derecha
        leds_clear();
        leds_set_pixel(2, 0, 0, 80);
        leds_show();
        vTaskDelay(pdMS_TO_TICKS(700));

        // 3 arriba derecha
        leds_clear();
        leds_set_pixel(3, 80, 80, 0);
        leds_show();
        vTaskDelay(pdMS_TO_TICKS(700));

        ESP_LOGI(TAG, "Apagado");
        leds_clear();
        leds_show();
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}