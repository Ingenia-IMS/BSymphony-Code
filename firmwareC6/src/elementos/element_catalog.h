#pragma once

typedef void (*element_light_fn_t)(void);

typedef struct {
    const char *name;
    element_light_fn_t apply_light;
} element_t;

const element_t *element_catalog_get_by_name(const char *name);