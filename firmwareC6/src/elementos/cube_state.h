#pragma once

#include <stdbool.h>

void cube_state_init(void);

bool cube_state_set_element_by_name(const char *element_name);
void cube_state_play_current_sound(void);

const char *cube_state_get_current_name(void);