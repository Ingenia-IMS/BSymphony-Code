#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    IR_RAW_FACE_0 = 0,
    IR_RAW_FACE_1 = 1,
    IR_RAW_FACE_2 = 2,
    IR_RAW_FACE_3 = 3,
} ir_raw_face_t;

typedef enum {
    IR_RAW_MODE_TX_FIXED = 0,
    IR_RAW_MODE_RX_FIXED,
    IR_RAW_MODE_TX_SWEEP,
    IR_RAW_MODE_RX_SWEEP,
} ir_raw_mode_t;

typedef struct {
    uint32_t hits[4];
    uint32_t samples[4];
    ir_raw_face_t best_face;
    uint32_t best_hits;
    bool any_hit;
} ir_raw_stats_t;

void ir_raw_test_init(void);
void ir_raw_test_start(ir_raw_mode_t mode, ir_raw_face_t face);
bool ir_raw_test_get_stats(ir_raw_stats_t *out);
const char *ir_raw_mode_name(ir_raw_mode_t mode);

#ifdef __cplusplus
}
#endif
