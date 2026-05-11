#include "elementos/element_catalog.h"

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#include "esp_log.h"

#include "leds/led_manager.h"
#include "sonido/sound_player.h"

static const char *TAG = "ELEMENT";

// -----------------------------------------------------------------------------
// EFECTOS DE LUZ POR ELEMENTO
// -----------------------------------------------------------------------------

static void light_agua(void)
{
    led_manager_set_water();
}

static void light_electricidad(void)
{
    led_manager_set_electricity();
}

static void light_fuego(void)
{
    led_manager_set_fire();
}

static void light_humano(void)
{
    led_manager_set_rainbow();
}

static void light_metal(void)
{
    led_manager_set_diagonal_dual(LED_COLOR_WHITE, LED_COLOR_CYAN);
}

static void light_mono(void)
{
    led_manager_set_diagonal_dual(LED_COLOR_DARK_BROWN, LED_COLOR_ORANGE);
}

static void light_naturaleza(void)
{
    led_manager_set_diagonal_dual(LED_COLOR_GREEN, LED_COLOR_CYAN);
}

static void light_oeste(void)
{
    led_manager_set_diagonal_dual(LED_COLOR_ORANGE, LED_COLOR_LIGHT_BROWN);
}

static void light_pajaro(void)
{
    led_manager_set_diagonal_dual(LED_COLOR_BROWN, LED_COLOR_WHITE);
}

static void light_piedra(void)
{
    led_manager_set_solid(LED_COLOR_WHITE);
}

static void light_pistola(void)
{
    led_manager_set_diagonal_dual(LED_COLOR_BLUE, LED_COLOR_WHITE);
}

static void light_reggaeton(void)
{
    led_manager_set_diagonal_dual(LED_COLOR_WARM_WHITE, LED_COLOR_WHITE);
}

static void light_robot(void)
{
    led_manager_set_diagonal_dual(LED_COLOR_WHITE, LED_COLOR_RED);
}

static void light_rock(void)
{
    led_manager_set_diagonal_dual(LED_COLOR_RED, LED_COLOR_DARK_BROWN);
}

static void light_tormenta(void)
{
    led_manager_set_storm();
}

static void light_viento(void)
{
    led_manager_set_diagonal_dual(LED_COLOR_CYAN, LED_COLOR_LIGHT_BLUE);
}

// -----------------------------------------------------------------------------
// CATÁLOGO DE ELEMENTOS
// El nombre del elemento debe coincidir con el nombre del sonido.
// Ejemplo: "agua" -> sound_player_play("agua")
// -----------------------------------------------------------------------------

static const element_t element_list[] = {
    { "agua",           light_agua },
    { "electricidad",   light_electricidad },
    { "fuego",          light_fuego },
    { "humano",         light_humano },
    { "metal",          light_metal },
    { "mono",           light_mono },
    { "naturaleza",     light_naturaleza },
    { "oeste",          light_oeste },
    { "pajaro",         light_pajaro },
    { "piedra",         light_piedra },
    { "pistola",        light_pistola },
    { "reggaeton",      light_reggaeton },
    { "robot",          light_robot },
    { "rock",           light_rock },
    { "tormenta",       light_tormenta },
    { "viento",         light_viento },
};

static const size_t element_count =
    sizeof(element_list) / sizeof(element_list[0]);

// -----------------------------------------------------------------------------
// RECETAS DE COMBINACIÓN
// -----------------------------------------------------------------------------
//
// Regla actual:
// - Si dos cubos se conectan y hay receta, solo cambia el leader.
// - Si no hay receta, no ocurre nada.
// - Ninguna receta debería devolver uno de los dos elementos de entrada.
// -----------------------------------------------------------------------------

typedef struct {
    const char *a;
    const char *b;
    const char *result;
} element_recipe_t;

static const element_recipe_t recipe_list[] = {
    // Recetas base inspiradas en el Processing original, adaptadas al catálogo actual.
    { "fuego",        "agua",          "naturaleza"   },
    { "agua",         "viento",        "tormenta"     },
    { "tormenta",     "viento",        "electricidad" },
    { "electricidad", "naturaleza",    "mono"         },
    { "electricidad", "viento",        "pajaro"       },
    { "piedra",       "fuego",         "metal"        },
    { "fuego",        "metal",         "pistola"      },
    { "mono",         "fuego",         "humano"       },
    { "naturaleza",   "fuego",         "piedra"       },
    { "humano",       "naturaleza",    "reggaeton"    },
    { "humano",       "metal",         "robot"        },
    { "humano",       "pistola",       "oeste"        },

    // Receta corregida.
    { "piedra",       "electricidad",  "rock"         },

    // Recetas añadidas para que los elementos terminales den más juego.
    { "robot",        "reggaeton",     "electricidad" },
    { "robot",        "pajaro",        "viento"       },
    { "oeste",        "fuego",         "pistola"      },
    { "oeste",        "humano",        "metal"        },
    { "pajaro",       "tormenta",      "electricidad" },
    { "reggaeton",    "viento",        "rock"         },
    { "rock",         "electricidad",  "robot"        },
    { "rock",         "fuego",         "metal"        },
    { "robot",        "piedra",        "metal"        },
    { "oeste",        "reggaeton",     "rock"         },
};

static const size_t recipe_count =
    sizeof(recipe_list) / sizeof(recipe_list[0]);

// -----------------------------------------------------------------------------

const element_t *element_catalog_get_by_name(const char *name)
{
    if (name == NULL) {
        return NULL;
    }

    for (size_t i = 0; i < element_count; i++) {
        if (strcmp(element_list[i].name, name) == 0) {
            return &element_list[i];
        }
    }

    return NULL;
}

static bool recipe_matches(const element_recipe_t *recipe,
                           const char *a,
                           const char *b)
{
    if (recipe == NULL || a == NULL || b == NULL) {
        return false;
    }

    return ((strcmp(recipe->a, a) == 0 && strcmp(recipe->b, b) == 0) ||
            (strcmp(recipe->a, b) == 0 && strcmp(recipe->b, a) == 0));
}

const char *element_catalog_combine_names(const char *a, const char *b)
{
    if (a == NULL || b == NULL) {
        return NULL;
    }

    for (size_t i = 0; i < recipe_count; i++) {
        if (recipe_matches(&recipe_list[i], a, b)) {
            return recipe_list[i].result;
        }
    }

    return NULL;
}
