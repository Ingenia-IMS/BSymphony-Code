#include "led_manager.h"
#include "led_strip.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/rmt_tx.h"
#include "esp_log.h"

#define LED_GPIO GPIO_NUM_18
#define LED_COUNT 8   // físicos (aunque uses 4)

static const char *TAG = "LED_MANAGER";

static led_strip_handle_t strip;

// ===== Estado interno =====

typedef enum {
    MODE_STATIC,
    MODE_BLINK,
    MODE_ALTERNATE
} led_mode_t;

static led_mode_t current_mode = MODE_STATIC;

static uint8_t r = 0, g = 0, b = 0;
static uint8_t r2 = 0, g2 = 0, b2 = 0;

static uint32_t period_ms = 1000;
static uint32_t last_time = 0;
static bool toggle = false;

// ===== Tabla de colores =====

static void get_color_rgb(led_color_t color, uint8_t *r, uint8_t *g, uint8_t *b)
{
    switch (color) {
        case LED_COLOR_RED:         *r = 255; *g = 0;   *b = 0;   break;
        case LED_COLOR_GREEN:       *r = 0;   *g = 255; *b = 0;   break;
        case LED_COLOR_BLUE:        *r = 0;   *g = 0;   *b = 255; break;
        case LED_COLOR_LIGHT_BLUE:  *r = 0;   *g = 180; *b = 255; break;
        case LED_COLOR_PINK:        *r = 255; *g = 20;  *b = 147; break;
        case LED_COLOR_PURPLE:      *r = 128; *g = 0;   *b = 128; break;
        case LED_COLOR_WARM_WHITE:  *r = 255; *g = 147; *b = 41;  break;
        case LED_COLOR_OFF:
        default:                   *r = 0;   *g = 0;   *b = 0;   break;
    }
}

// ===== Aplicar color =====

static void apply_color(uint8_t rr, uint8_t gg, uint8_t bb)
{
    for (int i = 0; i < LED_COUNT; i++) {
        if (i < 4) {
            led_strip_set_pixel(strip, i, rr, gg, bb);
        } else {
            led_strip_set_pixel(strip, i, 0, 0, 0);
        }
    }
    led_strip_refresh(strip);
}

// ===== TASK =====

static void led_task(void *arg)
{
    while (1) {
        uint32_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;

        switch (current_mode) {

            case MODE_STATIC:
                apply_color(r, g, b);
                break;

            case MODE_BLINK:
                if (now - last_time > period_ms) {
                    toggle = !toggle;
                    last_time = now;
                }
                if (toggle) apply_color(r, g, b);
                else apply_color(0, 0, 0);
                break;

            case MODE_ALTERNATE:
                if (now - last_time > period_ms) {
                    toggle = !toggle;
                    last_time = now;
                }
                if (toggle) apply_color(r, g, b);
                else apply_color(r2, g2, b2);
                break;
        }

        vTaskDelay(pdMS_TO_TICKS(20)); // suave y poco consumo
    }
}

// ===== INIT =====

esp_err_t led_manager_init(void)
{
    led_strip_config_t strip_config = {
        .strip_gpio_num = LED_GPIO,
        .max_leds = LED_COUNT,
        .led_model = LED_MODEL_WS2812,
        .color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB,
    };

    led_strip_rmt_config_t rmt_config = {
        .resolution_hz = 10 * 1000 * 1000,
        .mem_block_symbols = 64,
        .flags.with_dma = false,
    };

    ESP_ERROR_CHECK(led_strip_new_rmt_device(&strip_config, &rmt_config, &strip));

    xTaskCreate(led_task, "led_task", 2048, NULL, 5, NULL);

    ESP_LOGI(TAG, "LED manager initialized");

    return ESP_OK;
}

// ===== API =====

esp_err_t led_manager_set_color(led_color_t color)
{
    get_color_rgb(color, &r, &g, &b);
    current_mode = MODE_STATIC;
    return ESP_OK;
}

esp_err_t led_manager_set_color_with_brightness(led_color_t color, uint8_t brightness)
{
    uint8_t rr, gg, bb;
    get_color_rgb(color, &rr, &gg, &bb);

    r = (rr * brightness) / 255;
    g = (gg * brightness) / 255;
    b = (bb * brightness) / 255;

    current_mode = MODE_STATIC;
    return ESP_OK;
}

esp_err_t led_manager_set_blink(led_color_t color, uint32_t period)
{
    get_color_rgb(color, &r, &g, &b);
    period_ms = period;
    current_mode = MODE_BLINK;
    return ESP_OK;
}

esp_err_t led_manager_set_alternate(led_color_t c1, led_color_t c2, uint32_t period)
{
    get_color_rgb(c1, &r, &g, &b);
    get_color_rgb(c2, &r2, &g2, &b2);
    period_ms = period;
    current_mode = MODE_ALTERNATE;
    return ESP_OK;
}