#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "sound_player.h"
#include "leds/led_manager.h"

void app_main(void)
{
    sound_player_init();
    led_manager_init();

    uint32_t step = 0;

    while (1) {

        sound_player_play("Tormenta");

        switch (step % 5) {
            case 0:
                led_manager_set_color(LED_COLOR_RED);
                break;
            case 1:
                led_manager_set_color(LED_COLOR_BLUE);
                break;
            case 2:
                led_manager_set_color(LED_COLOR_GREEN);
                break;
            case 3:
                led_manager_set_blink(LED_COLOR_LIGHT_BLUE, 500);
                break;
            case 4:
                led_manager_set_alternate(LED_COLOR_PURPLE, LED_COLOR_PINK, 400);
                break;
        }

        step++;

        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}