#ifndef ELEMENT_CATALOG_H
#define ELEMENT_CATALOG_H

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*element_light_fn_t)(void);

typedef struct {
    const char *name;
    element_light_fn_t apply_light;
} element_t;

void element_catalog_init(void);

const element_t *element_catalog_get_current(void);
const element_t *element_catalog_next(void);

void element_catalog_apply_light(void);
void element_catalog_play_sound(void);

const char *element_catalog_get_current_name(void);

#ifdef __cplusplus
}
#endif

#endif