#pragma once
#include "driver/ledc.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

void generate_square_wave(gpio_num_t pin, uint32_t freq_hz, uint32_t time_ms);