#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"


typedef enum {
    LED_COLOR_OFF = 0,
    LED_COLOR_RED,
    LED_COLOR_GREEN,
    LED_COLOR_BLUE,
    LED_COLOR_LIGHT_BLUE,
    LED_COLOR_CYAN,
    LED_COLOR_WHITE,
    LED_COLOR_WARM_WHITE,
    LED_COLOR_YELLOW,
    LED_COLOR_ORANGE,
    LED_COLOR_PINK,
    LED_COLOR_PURPLE,
    LED_COLOR_DARK_BLUE,
    LED_COLOR_BROWN,
    LED_COLOR_DARK_BROWN,
    LED_COLOR_LIGHT_BROWN,
} led_color_t;

esp_err_t led_manager_init(void);

/* Ajustes generales */
esp_err_t led_manager_set_master_brightness(uint8_t brightness);
esp_err_t led_manager_set_blink_enabled(bool enabled);
bool led_manager_is_blink_enabled(void);

/* Efectos base */
esp_err_t led_manager_set_off(void);
esp_err_t led_manager_set_solid(led_color_t color);
esp_err_t led_manager_set_diagonal_dual(led_color_t color_a, led_color_t color_b);
esp_err_t led_manager_set_storm(void);
esp_err_t led_manager_set_fire(void);
esp_err_t led_manager_set_water(void);
esp_err_t led_manager_set_rainbow(void);
esp_err_t led_manager_set_electricity(void);

