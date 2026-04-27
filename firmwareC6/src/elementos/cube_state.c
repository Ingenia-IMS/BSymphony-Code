#include "elementos/cube_state.h"

#include "esp_log.h"

#include "elementos/element_catalog.h"
#include "leds/led_manager.h"
#include "sonido/sound_player.h"

static const char *TAG = "CUBE_STATE";

static const element_t *current_element = NULL;

void cube_state_init(void)
{
    cube_state_set_element_by_name("agua");
}

bool cube_state_set_element_by_name(const char *element_name)
{
    const element_t *new_element = element_catalog_get_by_name(element_name);

    if (new_element == NULL) {
        ESP_LOGW(TAG, "Elemento no encontrado: %s", element_name);
        return false;
    }

    current_element = new_element;

    ESP_LOGI(TAG, "Elemento actual: %s", current_element->name);

    led_manager_set_blink_enabled(false);

    if (current_element->apply_light != NULL) {
        current_element->apply_light();
    }

    return true;
}

void cube_state_play_current_sound(void)
{
    if (current_element == NULL) {
        ESP_LOGW(TAG, "No hay elemento actual");
        return;
    }

    ESP_LOGI(TAG, "Reproduciendo sonido: %s", current_element->name);

    sound_player_play(current_element->name);
}

const char *cube_state_get_current_name(void)
{
    if (current_element == NULL) {
        return "ninguno";
    }

    return current_element->name;
}