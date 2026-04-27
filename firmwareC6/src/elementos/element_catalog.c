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

static void light_metal(void)
{
    led_manager_set_solid(LED_COLOR_CYAN);
}

static void light_mono(void)
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
    { "metal",       light_metal },  
    { "mono",       light_mono },
    { "naturaleza",   light_naturaleza },
    { "oeste",        light_oeste },
    { "pajaro",       light_pajaro },
    { "piedra",    light_piedra },
    { "pistola",   light_pistola },
    { "reggaeton", light_reggaeton },
    { "robot",     light_robot },
    { "rock",      light_rock },
    { "tormenta",  light_tormenta },
    { "viento",    light_viento },
};

static const size_t element_count =
    sizeof(element_list) / sizeof(element_list[0]);


// -----------------------------------------------------------------------------
// FUNCIONES PÚBLICAS
// -----------------------------------------------------------------------------

void element_catalog_init(void)
{
    ESP_LOGI(TAG, "Catálogo inicializado. Elementos: %u", (unsigned)element_count);
}

const element_t *element_catalog_get_by_index(size_t index)
{
    if (index >= element_count) {
        return NULL;
    }

    return &element_list[index];
}

size_t element_catalog_get_count(void)
{
    return element_count;
}