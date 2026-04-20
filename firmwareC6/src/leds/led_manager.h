#pragma once

#include "esp_err.h"

typedef enum {
    LED_COLOR_RED,
    LED_COLOR_GREEN,
    LED_COLOR_BLUE,
    LED_COLOR_LIGHT_BLUE,
    LED_COLOR_PINK,
    LED_COLOR_PURPLE,
    LED_COLOR_WARM_WHITE,
    LED_COLOR_OFF
} led_color_t;

esp_err_t led_manager_init(void);

// Colores
esp_err_t led_manager_set_color(led_color_t color);
esp_err_t led_manager_set_color_with_brightness(led_color_t color, uint8_t brightness);

// Efectos
esp_err_t led_manager_set_blink(led_color_t color, uint32_t period_ms);
esp_err_t led_manager_set_alternate(led_color_t c1, led_color_t c2, uint32_t period_ms);