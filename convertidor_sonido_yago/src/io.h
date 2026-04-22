#pragma once
#include "driver/gpio.h"

#define ESP32_C6

#ifdef ESP32_C3
#define D0 GPIO_NUM_2
#define D1 GPIO_NUM_3
#define D2 GPIO_NUM_4
#define D3 GPIO_NUM_5
#define D4 GPIO_NUM_6
#define D5 GPIO_NUM_7
#define D6 GPIO_NUM_21
#define D7 GPIO_NUM_22
#define D8 GPIO_NUM_8
#define D9 GPIO_NUM_9
#define D10 GPIO_NUM_10
#endif

#ifdef ESP32_C6
#define D0 GPIO_NUM_0
#define D1 GPIO_NUM_1
#define D2 GPIO_NUM_2
#define D3 GPIO_NUM_21
#define D4 GPIO_NUM_22
#define D5 GPIO_NUM_23
#define D6 GPIO_NUM_16
#define D7 GPIO_NUM_17
#define D8 GPIO_NUM_19
#define D9 GPIO_NUM_20
#define D10 GPIO_NUM_18
#endif
