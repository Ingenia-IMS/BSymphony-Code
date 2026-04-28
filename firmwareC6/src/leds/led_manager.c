#include "leds/led_manager.h"

#include "driver/rmt_tx.h"
#include "esp_log.h"
#include "esp_err.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <string.h>

#define TAG "LED"

// -----------------------------------------------------------------------------
// CONFIG
// -----------------------------------------------------------------------------

#define LED_GPIO   GPIO_NUM_16
#define LED_COUNT  4

static rmt_channel_handle_t chan = NULL;
static rmt_encoder_handle_t encoder = NULL;

static uint8_t buffer[LED_COUNT * 3];

// blink
static bool blink_enabled = false;
static bool blink_state = true;

// -----------------------------------------------------------------------------
// LOW LEVEL
// -----------------------------------------------------------------------------

static void set_raw(int i, uint8_t r, uint8_t g, uint8_t b)
{
    if (i < 0 || i >= LED_COUNT) return;

    buffer[i * 3 + 0] = g;
    buffer[i * 3 + 1] = r;
    buffer[i * 3 + 2] = b;
}

// -----------------------------------------------------------------------------
// INIT
// -----------------------------------------------------------------------------

void led_manager_init(void)
{
    rmt_tx_channel_config_t cfg = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .gpio_num = LED_GPIO,
        .mem_block_symbols = 64,
        .resolution_hz = 10000000,
        .trans_queue_depth = 4,
    };

    ESP_ERROR_CHECK(rmt_new_tx_channel(&cfg, &chan));

    rmt_bytes_encoder_config_t enc_cfg = {
        .bit0 = {
            .level0 = 1, .duration0 = 4,
            .level1 = 0, .duration1 = 8,
        },
        .bit1 = {
            .level0 = 1, .duration0 = 8,
            .level1 = 0, .duration1 = 4,
        },
        .flags.msb_first = 1,
    };

    ESP_ERROR_CHECK(rmt_new_bytes_encoder(&enc_cfg, &encoder));
    ESP_ERROR_CHECK(rmt_enable(chan));

    ESP_LOGI(TAG, "LED manager listo");
}

// -----------------------------------------------------------------------------
// CORE
// -----------------------------------------------------------------------------

void led_manager_show(void)
{
    if (blink_enabled && !blink_state) {
        memset(buffer, 0, sizeof(buffer));
    }

    rmt_transmit_config_t tx = {
        .loop_count = 0,
    };

    ESP_ERROR_CHECK(rmt_transmit(chan, encoder, buffer, sizeof(buffer), &tx));
    ESP_ERROR_CHECK(rmt_tx_wait_all_done(chan, portMAX_DELAY));

    vTaskDelay(pdMS_TO_TICKS(1));
}

void led_manager_clear(void)
{
    memset(buffer, 0, sizeof(buffer));
}

void led_manager_set_pixel(int i, uint8_t r, uint8_t g, uint8_t b)
{
    set_raw(i, r, g, b);
}

void led_manager_set_solid(uint8_t r, uint8_t g, uint8_t b)
{
    for (int i = 0; i < LED_COUNT; i++) {
        set_raw(i, r, g, b);
    }
}

// -----------------------------------------------------------------------------
// EFECTOS
// -----------------------------------------------------------------------------

void led_manager_set_diagonal_dual(uint8_t r1, uint8_t g1, uint8_t b1,
                                   uint8_t r2, uint8_t g2, uint8_t b2)
{
    // diagonal 1: 0 y 2
    set_raw(0, r1, g1, b1);
    set_raw(2, r1, g1, b1);

    // diagonal 2: 1 y 3
    set_raw(1, r2, g2, b2);
    set_raw(3, r2, g2, b2);
}

void led_manager_set_rainbow(void)
{
    set_raw(0, 255, 0, 0);     // rojo
    set_raw(1, 0, 255, 0);     // verde
    set_raw(2, 0, 0, 255);     // azul
    set_raw(3, 255, 255, 0);   // amarillo
}

void led_manager_set_fire(void)
{
    set_raw(0, 255, 80, 0);
    set_raw(1, 255, 30, 0);
    set_raw(2, 180, 0, 0);
    set_raw(3, 255, 120, 0);
}

void led_manager_set_water(void)
{
    set_raw(0, 0, 50, 255);
    set_raw(1, 0, 100, 255);
    set_raw(2, 0, 150, 255);
    set_raw(3, 0, 80, 200);
}

void led_manager_set_electricity(void)
{
    set_raw(0, 150, 150, 255);
    set_raw(1, 50, 50, 255);
    set_raw(2, 200, 200, 255);
    set_raw(3, 100, 100, 255);
}

void led_manager_set_storm(void)
{
    set_raw(0, 20, 20, 50);
    set_raw(1, 10, 10, 30);
    set_raw(2, 40, 40, 80);
    set_raw(3, 5, 5, 20);
}

// -----------------------------------------------------------------------------
// BLINK
// -----------------------------------------------------------------------------

void led_manager_set_blink_enabled(bool enabled)
{
    blink_enabled = enabled;
}