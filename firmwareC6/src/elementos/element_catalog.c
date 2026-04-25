#include "elementos/element_catalog.h"

#include <stddef.h>

#include "esp_log.h"

#include "leds/led_manager.h"
#include "sonido/sound_player.h"

static const char *TAG = "ELEMENT";

// -----------------------------------------------------------------------------
// EFECTOS DE LUZ POR ELEMENTO
// -----------------------------------------------------------------------------

static void light_agua(void)
{
    led_manager_set_solid(LED_COLOR_BLUE);
}

static void light_electricidad(void)
{
    led_manager_set_storm();
}

static void light_fuego(void)
{
    led_manager_set_solid(LED_COLOR_RED);
}

static void light_humano(void)
{
    led_manager_set_solid(LED_COLOR_WHITE);
}

static void light_monkey(void)
{
    led_manager_set_solid(LED_COLOR_YELLOW);
}

static void light_naturaleza(void)
{
    led_manager_set_solid(LED_COLOR_GREEN);
}

static void light_oeste(void)
{
    led_manager_set_solid(LED_COLOR_YELLOW);
}

static void light_pajaro(void)
{
    led_manager_set_solid(LED_COLOR_CYAN);
}

// Declaradas ya para cuando metas la otra mitad de sonidos

static void light_piedra(void)
{
    led_manager_set_solid(LED_COLOR_WHITE);
}

static void light_pistola(void)
{
    led_manager_set_solid(LED_COLOR_RED);
}

static void light_reggaeton(void)
{
    led_manager_set_storm();
}

static void light_robot(void)
{
    led_manager_set_solid(LED_COLOR_CYAN);
}

static void light_rock(void)
{
    led_manager_set_solid(LED_COLOR_RED);
}

static void light_tormenta(void)
{
    led_manager_set_storm();
}

static void light_viento(void)
{
    led_manager_set_solid(LED_COLOR_CYAN);
}

// -----------------------------------------------------------------------------
// CATÁLOGO DE ELEMENTOS
// El nombre del elemento debe coincidir con el nombre del sonido.
// Ejemplo: "agua" -> sound_player_play("agua")
// -----------------------------------------------------------------------------

static const element_t element_list[] = {
    { "agua",         light_agua },
    { "electricidad", light_electricidad },
    { "fuego",        light_fuego },
    { "humano",       light_humano },
    { "monkey",       light_monkey },
    { "naturaleza",   light_naturaleza },
    { "oeste",        light_oeste },
    { "pajaro",       light_pajaro },

    // Deja estos comentados hasta meter sus .h en sound_catalog.c
    // { "piedra",    light_piedra },
    // { "pistola",   light_pistola },
    // { "reggaeton", light_reggaeton },
    // { "robot",     light_robot },
    // { "rock",      light_rock },
    // { "tormenta",  light_tormenta },
    // { "viento",    light_viento },
};

static const size_t element_count =
    sizeof(element_list) / sizeof(element_list[0]);

static size_t current_element_index = 0;

// -----------------------------------------------------------------------------
// FUNCIONES PÚBLICAS
// -----------------------------------------------------------------------------

void element_catalog_init(void)
{
    current_element_index = 0;

    element_catalog_apply_light();

    ESP_LOGI(TAG, "Elemento inicial: %s", element_catalog_get_current_name());
}

const element_t *element_catalog_get_current(void)
{
    return &element_list[current_element_index];
}

const element_t *element_catalog_next(void)
{
    current_element_index++;

    if (current_element_index >= element_count) {
        current_element_index = 0;
    }

    ESP_LOGI(TAG, "Nuevo elemento: %s", element_catalog_get_current_name());

    return element_catalog_get_current();
}

void element_catalog_apply_light(void)
{
    const element_t *e = element_catalog_get_current();

    led_manager_set_blink_enabled(false);

    if (e->apply_light != NULL) {
        e->apply_light();
    }
}

void element_catalog_play_sound(void)
{
    const element_t *e = element_catalog_get_current();

    ESP_LOGI(TAG, "Reproduciendo sonido: %s", e->name);

    sound_player_play(e->name);
}

const char *element_catalog_get_current_name(void)
{
    return element_list[current_element_index].name;
}