#include <stddef.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"

#include "imu/imu_manager.h"
#include "leds/led_manager.h"
#include "sonido/sound_player.h"
#include "elementos/element_catalog.h"
#include "elementos/cube_state.h"

#define MAIN_TASK_DELAY_MS 20

static const char *TAG = "MAIN";

static const char *element_sequence[] = {
    "agua",      
    "electricidad",
    "fuego",     
    "humano",    
    "metal",     
    "mono",      
    "naturaleza",
    "oeste",     
    "pajaro",    
    "piedra",    
    "pistola",   
    "reggaeton", 
    "robot",     
    "rock",      
    "tormenta",  
    "viento",    
};

static const size_t element_sequence_count =
    sizeof(element_sequence) / sizeof(element_sequence[0]);

static size_t current_sequence_index = 0;

static void change_to_sequence_element(size_t index)
{
    if (element_sequence_count == 0) {
        ESP_LOGE(TAG, "La secuencia de elementos está vacía");
        return;
    }

    if (index >= element_sequence_count) {
        index = 0;
    }

    const char *element_name = element_sequence[index];

    ESP_LOGI(TAG, "Cambiando a elemento de la secuencia: %s", element_name);

    if (cube_state_set_element_by_name(element_name)) {
        cube_state_play_current_sound();
        current_sequence_index = index;
    } else {
        ESP_LOGW(TAG, "El elemento '%s' no existe en el catálogo", element_name);
    }
}

static void change_to_next_sequence_element(void)
{
    size_t next_index = current_sequence_index + 1;

    if (next_index >= element_sequence_count) {
        next_index = 0;
    }

    change_to_sequence_element(next_index);
}

static void on_pickup_detected(void)
{
    ESP_LOGI(
        TAG,
        "Mover suave -> sonido del elemento actual: %s",
        cube_state_get_current_name()
    );

    cube_state_play_current_sound();
}

static void on_strong_shake_detected(void)
{
    ESP_LOGI(TAG, "Agitado vigoroso -> siguiente elemento de la secuencia");

    change_to_next_sequence_element();
}

void app_main(void)
{
    ESP_LOGI(TAG, "Inicializando sistema...");

    imu_init();
    led_manager_init();
    sound_player_init();
    cube_state_init();

    imu_set_pickup_callback(on_pickup_detected);
    imu_set_shake_callback(on_strong_shake_detected);

    imu_start_task();

    change_to_sequence_element(0);

    ESP_LOGI(TAG, "Elemento actual: %s", cube_state_get_current_name());

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(MAIN_TASK_DELAY_MS));
    }
}