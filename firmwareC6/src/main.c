#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "sonido/sound_player.h"

void app_main(void)
{
    sound_player_init();

    while (1) {
        sound_player_play("music");
        vTaskDelay(pdMS_TO_TICKS(7000));
    }
}