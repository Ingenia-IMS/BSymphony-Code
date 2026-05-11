#pragma once

typedef void (*element_light_fn_t)(void);

typedef struct {
    const char *name;
    element_light_fn_t apply_light;
} element_t;

const element_t *element_catalog_get_by_name(const char *name);

/*
 * Devuelve el resultado de combinar dos elementos.
 *
 * - Las combinaciones son simétricas:
 *      fuego + agua == agua + fuego
 *
 * - Si no hay combinación definida, devuelve NULL.
 *
 * - Esta función NO cambia el estado del cubo.
 *   Solo consulta la tabla de recetas.
 */
const char *element_catalog_combine_names(const char *a, const char *b);
