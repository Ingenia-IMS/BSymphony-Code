#pragma once

typedef void (*element_light_fn_t)(void);

typedef struct {
    const char *name;
    element_light_fn_t apply_light;
} element_t;

const element_t *element_catalog_get_by_name(const char *name);

/*
 * Consulta simétrica clásica:
 * fuego + agua == agua + fuego
 *
 * Devuelve el resultado si existe combinación.
 * Devuelve NULL si no hay combinación.
 *
 * Esta función NO decide qué cubo cambia.
 */
const char *element_catalog_combine_names(const char *a, const char *b);

/*
 * Consulta orientada al cubo local:
 *
 * Devuelve el nuevo elemento SOLO SI el elemento local es el que debe cambiar
 * para esa receta.
 *
 * Ejemplo:
 *   receta: fuego + agua -> naturaleza, cambia fuego
 *
 *   element_catalog_get_local_change_result("fuego", "agua")
 *       devuelve "naturaleza"
 *
 *   element_catalog_get_local_change_result("agua", "fuego")
 *       devuelve NULL
 *
 * Así evitamos depender de leader/follower para transformar.
 */
const char *element_catalog_get_local_change_result(const char *local,
                                                    const char *remote);
