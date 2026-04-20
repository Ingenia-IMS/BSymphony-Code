#ifndef IMU_MANAGER_H
#define IMU_MANAGER_H

#include <stdbool.h>
#include <stdint.h>

void imu_init(void);
void imu_update(void);

bool imu_take_hit_event(void);
bool imu_take_shake_event(void);
bool imu_take_pickup_event(void);
bool imu_take_putdown_event(void);

bool imu_is_in_hand(void);
bool imu_is_on_table(void);

#endif