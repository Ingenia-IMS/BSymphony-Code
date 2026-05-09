#pragma once

#include <stdint.h>
#include "../io.h"

/*
 * Pines IR reales para XIAO ESP32-C6 según tu prueba anterior.
 * Cambia solo esto si cambias el cableado.
 */
#define IR_SEL0_GPIO                D8      /* GPIO19 */
#define IR_SEL1_GPIO                D7      /* GPIO17 */
#define IR_RX_GPIO                  D10     /* GPIO18 */
#define IR_TX_GPIO                  D9      /* GPIO20 */

/* Tu prueba IR usaba recepción activa a nivel bajo. */
#define IR_RX_ACTIVE_LEVEL          0

/* Task IR: menor prioridad que la IMU, para no bloquear el resto. */
#define IR_TASK_STACK_BYTES         4096
#define IR_TASK_PRIORITY            3
#define IR_TASK_CORE                tskNO_AFFINITY

#define IR_EVENT_QUEUE_LEN          12

/* Búsqueda: barrido rápido + jitter. */
#define IR_SEARCH_TIMEOUT_US        5000000ULL   /* 5 s */
#define IR_SEARCH_SLOT_BASE_US      3500         /* 3,5 ms */
#define IR_SEARCH_SLOT_JITTER_US    2500         /* +0..2,5 ms */

/* Beacon simple por GPIO directo: ráfagas cortas de IR. */
#define IR_BEACON_BURST_US          300
#define IR_BEACON_GAP_US            450
#define IR_BEACON_BURSTS            3

/* Pequeña escucha antes de emitir, para romper simetrías. */
#define IR_PRE_LISTEN_US            700

/* Confirmación antes de bloquear cara. */
#define IR_CONFIRM_TIMEOUT_US       140000ULL    /* 140 ms */
#define IR_CONFIRM_REQUIRED_HITS    5
#define IR_CONFIRM_ATTEMPT_US       6500

/* Sincronización básica tras bloquear. */
#define IR_SYNC_CLAIM_MIN_US        25000        /* 25 ms */
#define IR_SYNC_CLAIM_RANDOM_US     70000        /* +0..70 ms */
#define IR_SYNC_PERIOD_US           400000ULL    /* leader emite sync cada 400 ms */
#define IR_PRESENCE_PERIOD_US       700000ULL    /* follower emite presencia cada 700 ms */

/* Si no ve nada del otro cubo durante 3 s, se corta enlace. */
#define IR_LOST_TIMEOUT_US          3000000ULL

/* Ventana corta en READY para no cargar CPU. */
#define IR_READY_RX_WINDOW_US       2500

/* Filtro de ruido de RX. */
#define IR_RX_MIN_ACTIVE_US         120
#define IR_RX_POLL_US               25
