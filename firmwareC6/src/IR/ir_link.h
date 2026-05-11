#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    IR_FACE_NONE = -1,
    IR_FACE_0 = 0,   // arriba
    IR_FACE_1 = 1,   // abajo
    IR_FACE_2 = 2,   // izquierda
    IR_FACE_3 = 3,   // derecha
} ir_face_t;

typedef enum {
    IR_LINK_IDLE = 0,
    IR_LINK_SEARCHING,
    IR_LINK_CONFIRMING_FACE,
    IR_LINK_LOCKED,
    IR_LINK_SYNCING,
    IR_LINK_READY,
} ir_link_state_t;

typedef enum {
    IR_ROLE_UNKNOWN = 0,
    IR_ROLE_LEADER,
    IR_ROLE_FOLLOWER,
} ir_role_t;

typedef enum {
    IR_EVENT_SEARCH_STARTED = 1,
    IR_EVENT_CANDIDATE_FACE,
    IR_EVENT_FACE_LOCKED,
    IR_EVENT_SYNCED,
    IR_EVENT_SEARCH_TIMEOUT,
    IR_EVENT_LINK_LOST,
    IR_EVENT_STOPPED,
    IR_EVENT_REMOTE_ELEMENT_RX,
} ir_event_type_t;

typedef struct {
    ir_event_type_t type;
    ir_link_state_t state;
    ir_face_t face;
    ir_role_t role;
    uint64_t sync_time_us;

    /*
     * Válido en IR_EVENT_REMOTE_ELEMENT_RX.
     */
    uint8_t remote_element_id;
    char remote_element_name[24];
} ir_event_t;

typedef struct {
    ir_link_state_t state;
    ir_face_t locked_face;
    ir_face_t candidate_face;
    ir_role_t role;
    uint64_t sync_time_us;
    uint64_t last_rx_us;

    uint8_t local_element_id;
    uint8_t remote_element_id;
    char local_element_name[24];
    char remote_element_name[24];
} ir_status_t;

void ir_link_init(void);
void ir_link_start_search(void);
void ir_link_stop(void);

/*
 * Actualiza el elemento que este cubo anunciará por IR.
 * Pásale cube_state_get_current_name().
 */
void ir_link_set_local_element_name(const char *name);

bool ir_link_get_event(ir_event_t *out, uint32_t timeout_ms);
bool ir_link_get_status(ir_status_t *out);

bool ir_link_is_ready(void);
ir_face_t ir_link_get_locked_face(void);
ir_role_t ir_link_get_role(void);

const char *ir_link_state_name(ir_link_state_t state);
const char *ir_link_role_name(ir_role_t role);
const char *ir_link_event_name(ir_event_type_t event);
const char *ir_link_face_name(ir_face_t face);
const char *ir_link_element_name_from_id(uint8_t id);

#ifdef __cplusplus
}
#endif
