#include "elementos/cube_state.h"

#include "esp_log.h"

#include "elementos/element_catalog.h"
#include "leds/led_manager.h"
#include "sonido/sound_player.h"

static const char *TAG = "CUBE_STATE";

static unsigned int current_element_index = 0;

void cube_state_init(void)
{
    current_element_index = 0;

    cube_state_apply_current_light();

    ESP_LOGI(TAG, "Elemento inicial: %s", cube_state_get_current_name());
}

void cube_state_set_element_by_index(unsigned int index)
{
    size_t count = element_catalog_get_count();

    if (count == 0) {
        ESP_LOGE(TAG, "No hay elementos en el catálogo");
        return;
    }

    if (index >= count) {
        ESP_LOGW(TAG, "Índice de elemento inválido: %u", index);
        index = 0;
    }

    current_element_index = index;

    ESP_LOGI(TAG, "Nuevo elemento: %s", cube_state_get_current_name());

    cube_state_apply_current_light();
}

void cube_state_next_element(void)
{
    size_t count = element_catalog_get_count();

    if (count == 0) {
        ESP_LOGE(TAG, "No hay elementos en el catálogo");
        return;
    }

    unsigned int next_index = current_element_index + 1;

    if (next_index >= count) {
        next_index = 0;
    }

    cube_state_set_element_by_index(next_index);
}

void cube_state_apply_current_light(void)
{
    const element_t *element = element_catalog_get_by_index(current_element_index);

    if (element == NULL) {
        ESP_LOGE(TAG, "Elemento actual inválido");
        return;
    }

    led_manager_set_blink_enabled(false);

    if (element->apply_light != NULL) {
        element->apply_light();
    }
}

void cube_state_play_current_sound(void)
{
    const element_t *element = element_catalog_get_by_index(current_element_index);

    if (element == NULL) {
        ESP_LOGE(TAG, "Elemento actual inválido");
        return;
    }

    ESP_LOGI(TAG, "Reproduciendo sonido: %s", element->name);

    sound_player_play(element->name);
}

const char *cube_state_get_current_name(void)
{
    const element_t *element = element_catalog_get_by_index(current_element_index);

    if (element == NULL) {
        return "desconocido";
    }

    return element->name;
}