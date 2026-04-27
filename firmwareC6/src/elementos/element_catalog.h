#ifndef ELEMENT_CATALOG_H
#define ELEMENT_CATALOG_H

#include <stddef.h>

typedef void (*element_light_fn_t)(void);

typedef struct {
    const char *name;
    element_light_fn_t apply_light;
} element_t;

void element_catalog_init(void);

const element_t *element_catalog_get_by_index(size_t index);
size_t element_catalog_get_count(void);

#endif