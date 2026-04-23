#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "sound_player.h"
#include "leds/led_manager.h"

void app_main(void)
{
    sound_player_init();
    led_manager_init();

    led_manager_set_master_brightness(180);

    uint32_t step = 0;

    while (1) {
        switch (step % 6) {
            case 0:
                led_manager_set_solid(LED_COLOR_BLUE);
                led_manager_set_blink_enabled(false);
                break;

            case 1:
                led_manager_set_diagonal_dual(LED_COLOR_BLUE, LED_COLOR_GREEN);
                led_manager_set_blink_enabled(false);
                break;

            case 2:
                led_manager_set_storm();
                sound_player_play("Tormenta");
                led_manager_set_blink_enabled(false);
                break;

            case 3:
                led_manager_set_fire();
                led_manager_set_blink_enabled(true);
                break;

            case 4:
                led_manager_set_water();
                led_manager_set_blink_enabled(false);
                break;

            case 5:
                led_manager_set_rainbow();
                led_manager_set_blink_enabled(true);
                break;
        }

        step++;
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}