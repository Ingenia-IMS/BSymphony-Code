#pragma once

#include <stdint.h>
#include "../io.h"

/*
 * Pines IR reales según tu main de prueba.
 */
#define IR_SEL0_GPIO                D8      // GPIO19
#define IR_SEL1_GPIO                D7      // GPIO17
#define IR_RX_GPIO                  D10     // GPIO18
#define IR_TX_GPIO                  D9      // GPIO20

/* Cambiar a 1 si tu circuito da HIGH cuando recibe IR. */
#define IR_RX_ACTIVE_LEVEL          0

/*
 * Task IR.
 * Tu IMU va a prioridad 4. Dejamos IR a 3 para no pisarla.
 * LED manager va a prioridad 1.
 */
#define IR_TASK_STACK_BYTES         4096
#define IR_TASK_PRIORITY            3
#define IR_TASK_CORE                tskNO_AFFINITY

#define IR_EVENT_QUEUE_LEN          12

/*
 * Búsqueda.
 * Cada cara se prueba durante 3,5-6 ms.
 * Una vuelta completa tarda aprox. 14-24 ms.
 */
#define IR_SEARCH_TIMEOUT_US        5000000ULL
#define IR_SEARCH_SLOT_BASE_US      3500
#define IR_SEARCH_SLOT_JITTER_US    2500

/*
 * Beacon de búsqueda.
 * Pulsos GPIO directos, no portadora.
 */
#define IR_BEACON_BURST_US          300
#define IR_BEACON_GAP_US            450
#define IR_BEACON_BURSTS            3

/*
 * Confirmación antes de bloquear cara.
 * Evita bloquear por ruido o reflejo puntual.
 */
#define IR_CONFIRM_TIMEOUT_US       140000ULL
#define IR_CONFIRM_REQUIRED_HITS    5
#define IR_CONFIRM_ATTEMPT_US       6500

/*
 * Sync tras bloquear.
 * Cada cubo espera un tiempo aleatorio; si oye al otro, follower.
 * Si no oye a nadie antes de su deadline, reclama leader.
 */
#define IR_SYNC_CLAIM_MIN_US        25000
#define IR_SYNC_CLAIM_RANDOM_US     70000
#define IR_SYNC_PERIOD_US           400000ULL
#define IR_PRESENCE_PERIOD_US       700000ULL

/* Si no se ve actividad durante 3 s, se corta el enlace. */
#define IR_LOST_TIMEOUT_US          3000000ULL

/* Ventana de escucha corta en READY para no cargar CPU. */
#define IR_READY_RX_WINDOW_US       2500

/* Detección de actividad RX. */
#define IR_RX_MIN_ACTIVE_US         120
#define IR_RX_POLL_US               25