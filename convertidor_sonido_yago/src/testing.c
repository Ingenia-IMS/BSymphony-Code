#include "testing.h"

void generate_square_wave(gpio_num_t pin, uint32_t freq_hz, uint32_t time_ms)
{
    // Configurar canal LEDC
    ledc_timer_config_t timer_cfg = {
        .speed_mode      = LEDC_LOW_SPEED_MODE,
        .duty_resolution = LEDC_TIMER_10_BIT,
        .timer_num       = LEDC_TIMER_0,
        .freq_hz         = freq_hz,
        .clk_cfg         = LEDC_AUTO_CLK,
    };
    ledc_timer_config(&timer_cfg);

    ledc_channel_config_t channel_cfg = {
        .gpio_num   = pin,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel    = LEDC_CHANNEL_0,
        .timer_sel  = LEDC_TIMER_0,
        .duty       = 512,   // 50% duty cycle (10-bit → 1024 max)
        .hpoint     = 0,
    };
    ledc_channel_config(&channel_cfg);

    // Esperar el tiempo indicado
    vTaskDelay(pdMS_TO_TICKS(time_ms));

    // Detener: duty a 0 y desconectar el pin
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, 0);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
    ledc_stop(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, 0);  // pin queda en nivel bajo
}