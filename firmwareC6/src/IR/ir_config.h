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

/*
 * Por tus pruebas RAW, el receptor se considera activo en LOW.
 * Si en algún montaje nuevo no detecta nada, prueba a cambiarlo a 1.
 */
#define IR_RX_ACTIVE_LEVEL              0

/*
 * Task IR.
 * Prioridad baja para convivir con IMU, LEDs y audio.
 */
#define IR_TASK_STACK_BYTES             4096
#define IR_TASK_PRIORITY                2
#define IR_TASK_CORE                    tskNO_AFFINITY

#define IR_EVENT_QUEUE_LEN              12

/*
 * Búsqueda.
 * Cooperativa: usa vTaskDelay(), no bucles largos de microsegundos.
 */
#define IR_SEARCH_TIMEOUT_MS            8000
#define IR_SEARCH_STEP_MS               18
#define IR_SEARCH_TX_PROB_PERCENT       45

/*
 * TX lento y robusto.
 * Como ya verificamos que el hardware ve pulsos lentos, usamos ms.
 */
#define IR_TX_ON_MS                     40
#define IR_TX_OFF_MS                    6

/*
 * RX por muestreo cooperativo.
 */
#define IR_RX_WINDOW_MS                 50
#define IR_RX_SAMPLE_PERIOD_MS          3
#define IR_RX_REQUIRED_ACTIVE_SAMPLES   2

/*
 * Confirmación de cara.
 * En fase 2 alguna vez se quedaba en CANDIDATE y acababa timeout.
 * Aquí damos más margen.
 */
#define IR_CONFIRM_TIMEOUT_MS           2200
#define IR_CONFIRM_REQUIRED_HITS        2
#define IR_CONFIRM_STEP_MS              18
#define IR_CONFIRM_TX_PROB_PERCENT      45

/*
 * Sync simple.
 * Todavía no hay mensajes de elemento.
 */
#define IR_SYNC_TIMEOUT_MS              2000
#define IR_SYNC_STEP_MS                 18
#define IR_SYNC_TX_PROB_PERCENT         55
#define IR_SYNC_REQUIRED_HITS           1

/*
 * READY / presencia.
 * Ambos cubos emiten presencia.
 * Más frecuente que fase 2 para reducir LINK_LOST por microdesalineaciones.
 */
#define IR_READY_STEP_MS                18
#define IR_READY_TX_PERIOD_MIN_MS       150
#define IR_READY_TX_PERIOD_JITTER_MS    120
#define IR_READY_RX_WINDOW_MS           45

/*
 * Requisito: si se separan durante 3 s, se corta enlace.
 */
#define IR_LOST_TIMEOUT_MS              3000
