#pragma once

#include <stdbool.h>

void sound_player_init(void);
bool sound_player_play(const char *sound_name);
void sound_player_stop(void);
bool sound_player_is_playing(void);