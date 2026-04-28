#pragma once

#include <stdint.h>
#include <stdbool.h>

typedef struct {
    uint8_t r;
    uint8_t g;
    uint8_t b;
} led_color_t;

#define LED_COLOR_RED          ((led_color_t){255, 0, 0})
#define LED_COLOR_GREEN        ((led_color_t){0, 255, 0})
#define LED_COLOR_BLUE         ((led_color_t){0, 0, 255})
#define LED_COLOR_WHITE        ((led_color_t){255, 255, 255})
#define LED_COLOR_WARM_WHITE   ((led_color_t){255, 180, 90})
#define LED_COLOR_CYAN         ((led_color_t){0, 180, 255})
#define LED_COLOR_LIGHT_BLUE   ((led_color_t){80, 160, 255})
#define LED_COLOR_ORANGE       ((led_color_t){255, 80, 0})
#define LED_COLOR_BROWN        ((led_color_t){120, 55, 15})
#define LED_COLOR_LIGHT_BROWN  ((led_color_t){180, 90, 35})
#define LED_COLOR_DARK_BROWN   ((led_color_t){70, 30, 10})

void led_manager_init(void);
void led_manager_show(void);
void led_manager_clear(void);

void led_manager_set_pixel(int i, led_color_t color);
void led_manager_set_solid(led_color_t color);
void led_manager_set_diagonal_dual(led_color_t color_a, led_color_t color_b);

void led_manager_set_rainbow(void);
void led_manager_set_fire(void);
void led_manager_set_water(void);
void led_manager_set_electricity(void);
void led_manager_set_storm(void);

void led_manager_set_blink_enabled(bool enabled);