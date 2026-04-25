#ifndef IMU_MANAGER_H
#define IMU_MANAGER_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*imu_event_callback_t)(void);

void imu_init(void);

/* Callbacks */
void imu_set_pickup_callback(imu_event_callback_t cb);
void imu_set_shake_callback(imu_event_callback_t cb);

/* Arranca la task interna de la IMU */
void imu_start_task(void);

#ifdef __cplusplus
}
#endif

#endif