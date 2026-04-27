#ifndef CUBE_STATE_H
#define CUBE_STATE_H


void cube_state_init(void);

void cube_state_set_element_by_index(unsigned int index);
void cube_state_next_element(void);

void cube_state_apply_current_light(void);
void cube_state_play_current_sound(void);

const char *cube_state_get_current_name(void);

#endif