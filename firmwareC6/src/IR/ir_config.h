#pragma once

#include <stdint.h>
#include "../io.h"

/*
 * Mapa lógico/físico confirmado:
 * face 0 = arriba
 * face 1 = abajo
 * face 2 = izquierda
 * face 3 = derecha
 */
#define IR_SEL0_GPIO                    D8      // GPIO19
#define IR_SEL1_GPIO                    D7      // GPIO17
#define IR_RX_GPIO                      D10     // GPIO18
#define IR_TX_GPIO                      D9      // GPIO20

#define IR_RX_ACTIVE_LEVEL              0

#define IR_TASK_STACK_BYTES             4096
#define IR_TASK_PRIORITY                2
#define IR_TASK_CORE                    tskNO_AFFINITY

#define IR_EVENT_QUEUE_LEN              16

/* ---------------- FASE 2B: búsqueda / bloqueo / presencia ---------------- */

#define IR_SEARCH_TIMEOUT_MS            3000
#define IR_SEARCH_STEP_MS               18
#define IR_SEARCH_TX_PROB_PERCENT       45

#define IR_TX_ON_MS                     40
#define IR_TX_OFF_MS                    6

#define IR_RX_WINDOW_MS                 50
#define IR_RX_SAMPLE_PERIOD_MS          3
#define IR_RX_REQUIRED_ACTIVE_SAMPLES   2

#define IR_CONFIRM_TIMEOUT_MS           2200
#define IR_CONFIRM_REQUIRED_HITS        2
#define IR_CONFIRM_STEP_MS              18
#define IR_CONFIRM_TX_PROB_PERCENT      45

#define IR_SYNC_TIMEOUT_MS              2000
#define IR_SYNC_STEP_MS                 18
#define IR_SYNC_TX_PROB_PERCENT         55
#define IR_SYNC_REQUIRED_HITS           1

#define IR_READY_STEP_MS                18
#define IR_READY_RX_WINDOW_MS           45
#define IR_LOST_TIMEOUT_MS              3000

/* ---------------- FASE 3: mensaje mínimo de elemento ----------------
 *
 * En READY se envía periódicamente un frame muy simple:
 *
 *  PREAMBLE + element_id(4 bits) + crc(4 bits)
 *
 * Todavía NO transforma el cubo. Solo registra:
 * "Recibido elemento remoto: agua/fuego/..."
 */

#define IR_FRAME_TX_PERIOD_MIN_MS       900
#define IR_FRAME_TX_PERIOD_JITTER_MS    500

#define IR_FRAME_PREAMBLE_ON_MS         90
#define IR_FRAME_PREAMBLE_OFF_MS        35

#define IR_FRAME_BIT0_ON_MS             22
#define IR_FRAME_BIT1_ON_MS             58
#define IR_FRAME_BIT_OFF_MS             24
#define IR_FRAME_BIT_THRESHOLD_MS       40

#define IR_FRAME_WAIT_START_MS          160
#define IR_FRAME_MAX_PULSE_MS           140
#define IR_FRAME_SAMPLE_MS              2

#define IR_FRAME_PREAMBLE_MIN_MS        65
#define IR_FRAME_PREAMBLE_MAX_MS        130
