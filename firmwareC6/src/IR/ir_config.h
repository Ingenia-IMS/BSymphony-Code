#pragma once

#include <stdint.h>
#include "../io.h"

/*
 * Pines IR reales usados en tus pruebas.
 */
#define IR_SEL0_GPIO                    D8      // GPIO19
#define IR_SEL1_GPIO                    D7      // GPIO17
#define IR_RX_GPIO                      D10     // GPIO18
#define IR_TX_GPIO                      D9      // GPIO20

/* Cambia a 1 si tu RX digital se activa en HIGH. */
#define IR_RX_ACTIVE_LEVEL              0

/*
 * Task IR baja para no molestar a IMU/audio/LED.
 */
#define IR_TASK_STACK_BYTES             4096
#define IR_TASK_PRIORITY                2
#define IR_TASK_CORE                    tskNO_AFFINITY

#define IR_EVENT_QUEUE_LEN              12

/*
 * FASE 1E:
 * Como ya hemos visto CANDIDATE_FACE con dos cubos, pero no FACE_LOCKED,
 * hacemos la confirmación más fácil y damos más tiempo de búsqueda.
 */
#define IR_SEARCH_TIMEOUT_US            8000000ULL   // antes 5 s, ahora 8 s
#define IR_SEARCH_TICK_MS               16
#define IR_SEARCH_RX_WINDOW_US          8000
#define IR_SEARCH_TX_PROB_PERCENT       40

/*
 * Beacon GPIO directo, sin portadora.
 */
#define IR_BEACON_BURST_US              450
#define IR_BEACON_GAP_US                550
#define IR_BEACON_BURSTS                3

/*
 * Confirmación de cara:
 * En 1D apareció CANDIDATE_FACE pero no llegaba a bloquear.
 * Ahora exigimos solo 2 hits totales: el candidato inicial + 1 hit más.
 */
#define IR_CONFIRM_TIMEOUT_US           1800000ULL   // 1,8 s
#define IR_CONFIRM_REQUIRED_HITS        2
#define IR_CONFIRM_TICK_MS              14
#define IR_CONFIRM_RX_WINDOW_US         12000
#define IR_CONFIRM_TX_PROB_PERCENT      45

/*
 * Sync:
 * Algo más generoso para que uno de los dos pueda escuchar antes de hacerse leader.
 */
#define IR_SYNC_TIMEOUT_US              1500000ULL
#define IR_SYNC_CLAIM_MIN_US            80000
#define IR_SYNC_CLAIM_RANDOM_US         220000
#define IR_SYNC_TICK_MS                 16
#define IR_SYNC_RX_WINDOW_US            10000
#define IR_SYNC_TX_PROB_PERCENT         45

/*
 * En READY:
 * leader emite sync periódico.
 * follower emite presencia periódica.
 * Si pasan 3 s sin RX, se pierde enlace.
 */
#define IR_READY_RX_WINDOW_US           8000
#define IR_READY_TICK_MS                20
#define IR_SYNC_PERIOD_US               300000ULL
#define IR_PRESENCE_PERIOD_US           500000ULL
#define IR_LOST_TIMEOUT_US              3000000ULL

/*
 * Filtro RX.
 */
#define IR_RX_MIN_ACTIVE_US             100
#define IR_RX_POLL_US                   25
