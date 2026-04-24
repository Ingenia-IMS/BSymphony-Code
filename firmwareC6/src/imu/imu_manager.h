#ifndef IMU_MANAGER_H
#define IMU_MANAGER_H

#include <stdbool.h>

void imu_init(void);
void imu_update(void);

bool imu_take_sound_event(void);
bool imu_take_blink_event(void);

#endif