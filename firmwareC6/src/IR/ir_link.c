#include "ir_link.h"
#include "ir_config.h"

#include <string.h>

#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

static const char *TAG = "IR_LINK";

#define IR_FACE_COUNT 4
#define IR_NAME_MAX   24

typedef enum {
    IR_CMD_START_SEARCH = 1,
    IR_CMD_STOP,
} ir_cmd_type_t;

typedef struct {
    ir_cmd_type_t type;
} ir_cmd_t;

typedef struct {
    ir_link_state_t state;
    ir_face_t locked_face;
    ir_face_t candidate_face;
    ir_role_t role;

    uint8_t order[IR_FACE_COUNT];
    uint8_t order_index;
    uint8_t confirm_hits;
    uint8_t sync_hits;

    uint64_t state_since_us;
    uint64_t search_started_us;
    uint64_t sync_time_us;
    uint64_t last_rx_us;
    uint64_t next_frame_tx_us;

    uint8_t local_element_id;
    uint8_t remote_element_id;
    char local_element_name[IR_NAME_MAX];
    char remote_element_name[IR_NAME_MAX];
} ir_ctx_t;

static QueueHandle_t s_cmd_q = NULL;
static QueueHandle_t s_event_q = NULL;
static SemaphoreHandle_t s_mutex = NULL;
static TaskHandle_t s_task = NULL;
static ir_ctx_t s_ctx;

static void ir_task(void *arg);

static void hw_init(void);
static void hw_select_face(ir_face_t face);
static void hw_tx_on(void);
static void hw_tx_off(void);
static bool hw_rx_active(void);

static uint64_t now_us(void);
static uint32_t now_ms(void);
static uint32_t rand_percent(void);
static uint32_t rand_range_ms(uint32_t max_ms);

static void enter_state(ir_ctx_t *ctx, ir_link_state_t state);
static void publish_event(const ir_ctx_t *ctx, ir_event_type_t type);
static void shuffle_order(ir_ctx_t *ctx);
static void schedule_next_frame(ir_ctx_t *ctx);

static void signal_send_presence(void);
static bool signal_receive_window(uint32_t window_ms);

static void frame_send_element(uint8_t element_id);
static bool frame_try_receive_element(uint8_t *out_element_id, bool *out_activity);
static bool wait_rx_active(uint32_t timeout_ms);
static uint32_t measure_rx_active_ms(uint32_t max_ms);

static uint8_t element_id_from_name(const char *name);
static void copy_element_name_from_id(uint8_t id, char *dst, size_t dst_size);

static void handle_search(ir_ctx_t *ctx);
static void handle_confirm(ir_ctx_t *ctx);
static void handle_sync(ir_ctx_t *ctx);
static void handle_ready(ir_ctx_t *ctx);

void ir_link_init(void)
{
    memset(&s_ctx, 0, sizeof(s_ctx));
    s_ctx.state = IR_LINK_IDLE;
    s_ctx.locked_face = IR_FACE_NONE;
    s_ctx.candidate_face = IR_FACE_NONE;
    s_ctx.role = IR_ROLE_UNKNOWN;
    s_ctx.local_element_id = element_id_from_name("agua");
    s_ctx.remote_element_id = 0xFF;
    strncpy(s_ctx.local_element_name, "agua", sizeof(s_ctx.local_element_name) - 1);
    strncpy(s_ctx.remote_element_name, "desconocido", sizeof(s_ctx.remote_element_name) - 1);

    hw_init();
    shuffle_order(&s_ctx);

    s_cmd_q = xQueueCreate(6, sizeof(ir_cmd_t));
    s_event_q = xQueueCreate(IR_EVENT_QUEUE_LEN, sizeof(ir_event_t));
    s_mutex = xSemaphoreCreateMutex();

    BaseType_t ok = xTaskCreatePinnedToCore(
        ir_task,
        "ir_link",
        IR_TASK_STACK_BYTES,
        NULL,
        IR_TASK_PRIORITY,
        &s_task,
        IR_TASK_CORE
    );

    if (ok != pdPASS) {
        ESP_LOGE(TAG, "No se pudo crear la task IR");
    }
}

void ir_link_set_local_element_name(const char *name)
{
    if (name == NULL) {
        name = "ninguno";
    }

    if (s_mutex == NULL) {
        return;
    }

    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
        s_ctx.local_element_id = element_id_from_name(name);
        strncpy(s_ctx.local_element_name, name, sizeof(s_ctx.local_element_name) - 1);
        s_ctx.local_element_name[sizeof(s_ctx.local_element_name) - 1] = '\0';
        xSemaphoreGive(s_mutex);
    }
}

void ir_link_start_search(void)
{
    if (s_cmd_q == NULL) {
        return;
    }

    ir_status_t st;
    if (ir_link_get_status(&st)) {
        if (st.state != IR_LINK_IDLE) {
            ESP_LOGI(TAG, "start_search ignorado: estado=%s", ir_link_state_name(st.state));
            return;
        }
    }

    ir_cmd_t cmd = {.type = IR_CMD_START_SEARCH};
    xQueueSend(s_cmd_q, &cmd, 0);
}

void ir_link_stop(void)
{
    if (s_cmd_q == NULL) {
        return;
    }

    ir_cmd_t cmd = {.type = IR_CMD_STOP};
    xQueueSend(s_cmd_q, &cmd, 0);
}

bool ir_link_get_event(ir_event_t *out, uint32_t timeout_ms)
{
    if (out == NULL || s_event_q == NULL) {
        return false;
    }

    return xQueueReceive(s_event_q, out, pdMS_TO_TICKS(timeout_ms)) == pdTRUE;
}

bool ir_link_get_status(ir_status_t *out)
{
    if (out == NULL || s_mutex == NULL) {
        return false;
    }

    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(10)) != pdTRUE) {
        return false;
    }

    out->state = s_ctx.state;
    out->locked_face = s_ctx.locked_face;
    out->candidate_face = s_ctx.candidate_face;
    out->role = s_ctx.role;
    out->sync_time_us = s_ctx.sync_time_us;
    out->last_rx_us = s_ctx.last_rx_us;

    out->local_element_id = s_ctx.local_element_id;
    out->remote_element_id = s_ctx.remote_element_id;
    strncpy(out->local_element_name, s_ctx.local_element_name, sizeof(out->local_element_name) - 1);
    strncpy(out->remote_element_name, s_ctx.remote_element_name, sizeof(out->remote_element_name) - 1);
    out->local_element_name[sizeof(out->local_element_name) - 1] = '\0';
    out->remote_element_name[sizeof(out->remote_element_name) - 1] = '\0';

    xSemaphoreGive(s_mutex);
    return true;
}

bool ir_link_is_ready(void)
{
    ir_status_t st;
    return ir_link_get_status(&st) && st.state == IR_LINK_READY;
}

ir_face_t ir_link_get_locked_face(void)
{
    ir_status_t st;
    if (!ir_link_get_status(&st)) {
        return IR_FACE_NONE;
    }
    return st.locked_face;
}

ir_role_t ir_link_get_role(void)
{
    ir_status_t st;
    if (!ir_link_get_status(&st)) {
        return IR_ROLE_UNKNOWN;
    }
    return st.role;
}

const char *ir_link_state_name(ir_link_state_t state)
{
    switch (state) {
        case IR_LINK_IDLE: return "IDLE";
        case IR_LINK_SEARCHING: return "SEARCHING";
        case IR_LINK_CONFIRMING_FACE: return "CONFIRMING_FACE";
        case IR_LINK_LOCKED: return "LOCKED";
        case IR_LINK_SYNCING: return "SYNCING";
        case IR_LINK_READY: return "READY";
        default: return "UNKNOWN";
    }
}

const char *ir_link_role_name(ir_role_t role)
{
    switch (role) {
        case IR_ROLE_LEADER: return "LEADER";
        case IR_ROLE_FOLLOWER: return "FOLLOWER";
        default: return "UNKNOWN";
    }
}

const char *ir_link_event_name(ir_event_type_t event)
{
    switch (event) {
        case IR_EVENT_SEARCH_STARTED: return "SEARCH_STARTED";
        case IR_EVENT_CANDIDATE_FACE: return "CANDIDATE_FACE";
        case IR_EVENT_FACE_LOCKED: return "FACE_LOCKED";
        case IR_EVENT_SYNCED: return "SYNCED";
        case IR_EVENT_SEARCH_TIMEOUT: return "SEARCH_TIMEOUT";
        case IR_EVENT_LINK_LOST: return "LINK_LOST";
        case IR_EVENT_STOPPED: return "STOPPED";
        case IR_EVENT_REMOTE_ELEMENT_RX: return "REMOTE_ELEMENT_RX";
        default: return "UNKNOWN";
    }
}

const char *ir_link_face_name(ir_face_t face)
{
    switch (face) {
        case IR_FACE_0: return "arriba";
        case IR_FACE_1: return "abajo";
        case IR_FACE_2: return "izquierda";
        case IR_FACE_3: return "derecha";
        default: return "none";
    }
}

const char *ir_link_element_name_from_id(uint8_t id)
{
    switch (id) {
        case 0:  return "agua";
        case 1:  return "electricidad";
        case 2:  return "fuego";
        case 3:  return "humano";
        case 4:  return "metal";
        case 5:  return "mono";
        case 6:  return "naturaleza";
        case 7:  return "oeste";
        case 8:  return "pajaro";
        case 9:  return "piedra";
        case 10: return "pistola";
        case 11: return "reggaeton";
        case 12: return "robot";
        case 13: return "rock";
        case 14: return "tormenta";
        case 15: return "viento";
        default: return "desconocido";
    }
}

static void ir_task(void *arg)
{
    (void)arg;
    ir_cmd_t cmd;

    while (1) {
        while (xQueueReceive(s_cmd_q, &cmd, 0) == pdTRUE) {
            if (cmd.type == IR_CMD_START_SEARCH) {
                hw_tx_off();

                s_ctx.locked_face = IR_FACE_NONE;
                s_ctx.candidate_face = IR_FACE_NONE;
                s_ctx.role = IR_ROLE_UNKNOWN;
                s_ctx.confirm_hits = 0;
                s_ctx.sync_hits = 0;
                s_ctx.last_rx_us = 0;
                s_ctx.sync_time_us = 0;
                s_ctx.search_started_us = now_us();
                s_ctx.next_frame_tx_us = 0;
                s_ctx.remote_element_id = 0xFF;
                strncpy(s_ctx.remote_element_name, "desconocido", sizeof(s_ctx.remote_element_name) - 1);

                shuffle_order(&s_ctx);
                enter_state(&s_ctx, IR_LINK_SEARCHING);
                publish_event(&s_ctx, IR_EVENT_SEARCH_STARTED);

            } else if (cmd.type == IR_CMD_STOP) {
                hw_tx_off();

                s_ctx.locked_face = IR_FACE_NONE;
                s_ctx.candidate_face = IR_FACE_NONE;
                s_ctx.role = IR_ROLE_UNKNOWN;

                enter_state(&s_ctx, IR_LINK_IDLE);
                publish_event(&s_ctx, IR_EVENT_STOPPED);
            }
        }

        switch (s_ctx.state) {
            case IR_LINK_SEARCHING:
                handle_search(&s_ctx);
                break;

            case IR_LINK_CONFIRMING_FACE:
                handle_confirm(&s_ctx);
                break;

            case IR_LINK_SYNCING:
                handle_sync(&s_ctx);
                break;

            case IR_LINK_READY:
                handle_ready(&s_ctx);
                break;

            case IR_LINK_IDLE:
            case IR_LINK_LOCKED:
            default:
                hw_tx_off();
                vTaskDelay(pdMS_TO_TICKS(30));
                break;
        }
    }
}

// -----------------------------------------------------------------------------
// Hardware
// -----------------------------------------------------------------------------

static void hw_init(void)
{
    gpio_config_t out_conf = {
        .pin_bit_mask =
            (1ULL << IR_SEL0_GPIO) |
            (1ULL << IR_SEL1_GPIO) |
            (1ULL << IR_TX_GPIO),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&out_conf));

    gpio_config_t in_conf = {
        .pin_bit_mask = (1ULL << IR_RX_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&in_conf));

    hw_tx_off();
    hw_select_face(IR_FACE_0);

    ESP_LOGI(TAG,
             "init face map: 0=arriba 1=abajo 2=izquierda 3=derecha | SEL0=%d SEL1=%d RX=%d TX=%d",
             IR_SEL0_GPIO, IR_SEL1_GPIO, IR_RX_GPIO, IR_TX_GPIO);
}

static void hw_select_face(ir_face_t face)
{
    if (face < IR_FACE_0 || face > IR_FACE_3) {
        hw_tx_off();
        return;
    }

    uint8_t f = (uint8_t)face & 0x03;

    gpio_set_level(IR_SEL0_GPIO, f & 0x01);
    gpio_set_level(IR_SEL1_GPIO, (f >> 1) & 0x01);
}

static void hw_tx_on(void)
{
    gpio_set_level(IR_TX_GPIO, 1);
}

static void hw_tx_off(void)
{
    gpio_set_level(IR_TX_GPIO, 0);
}

static bool hw_rx_active(void)
{
    return gpio_get_level(IR_RX_GPIO) == IR_RX_ACTIVE_LEVEL;
}

static uint64_t now_us(void)
{
    return (uint64_t)esp_timer_get_time();
}

static uint32_t now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000ULL);
}

static uint32_t rand_percent(void)
{
    return esp_random() % 100U;
}

static uint32_t rand_range_ms(uint32_t max_ms)
{
    if (max_ms == 0) {
        return 0;
    }
    return esp_random() % max_ms;
}

// -----------------------------------------------------------------------------
// Señal IR de presencia lenta y cooperativa
// -----------------------------------------------------------------------------

static void signal_send_presence(void)
{
    hw_tx_on();
    vTaskDelay(pdMS_TO_TICKS(IR_TX_ON_MS));
    hw_tx_off();
    vTaskDelay(pdMS_TO_TICKS(IR_TX_OFF_MS));
}

static bool signal_receive_window(uint32_t window_ms)
{
    hw_tx_off();

    uint32_t start = now_ms();
    uint8_t active_count = 0;

    while ((now_ms() - start) < window_ms) {
        if (hw_rx_active()) {
            active_count++;
            if (active_count >= IR_RX_REQUIRED_ACTIVE_SAMPLES) {
                return true;
            }
        }

        vTaskDelay(pdMS_TO_TICKS(IR_RX_SAMPLE_PERIOD_MS));
    }

    return false;
}

// -----------------------------------------------------------------------------
// Frame mínimo de elemento
// -----------------------------------------------------------------------------

static uint8_t frame_crc4(uint8_t element_id)
{
    /*
     * CRC simple de 4 bits. Suficiente para esta fase.
     */
    uint8_t x = (element_id & 0x0F) ^ 0x0A;
    x ^= (uint8_t)(x >> 2);
    x ^= (uint8_t)(x >> 1);
    return x & 0x0F;
}

static void frame_send_bit(bool bit_one)
{
    hw_tx_on();
    vTaskDelay(pdMS_TO_TICKS(bit_one ? IR_FRAME_BIT1_ON_MS : IR_FRAME_BIT0_ON_MS));
    hw_tx_off();
    vTaskDelay(pdMS_TO_TICKS(IR_FRAME_BIT_OFF_MS));
}

static void frame_send_element(uint8_t element_id)
{
    element_id &= 0x0F;
    uint8_t crc = frame_crc4(element_id);
    uint8_t payload = (uint8_t)((element_id << 4) | crc);

    hw_tx_on();
    vTaskDelay(pdMS_TO_TICKS(IR_FRAME_PREAMBLE_ON_MS));
    hw_tx_off();
    vTaskDelay(pdMS_TO_TICKS(IR_FRAME_PREAMBLE_OFF_MS));

    for (int bit = 7; bit >= 0; bit--) {
        bool one = ((payload >> bit) & 0x01) != 0;
        frame_send_bit(one);
    }
}

static bool wait_rx_active(uint32_t timeout_ms)
{
    uint32_t start = now_ms();

    while ((now_ms() - start) < timeout_ms) {
        if (hw_rx_active()) {
            return true;
        }
        vTaskDelay(pdMS_TO_TICKS(IR_FRAME_SAMPLE_MS));
    }

    return false;
}

static uint32_t measure_rx_active_ms(uint32_t max_ms)
{
    uint32_t start = now_ms();

    while ((now_ms() - start) < max_ms) {
        if (!hw_rx_active()) {
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(IR_FRAME_SAMPLE_MS));
    }

    return now_ms() - start;
}

static bool frame_try_receive_element(uint8_t *out_element_id, bool *out_activity)
{
    if (out_element_id == NULL || out_activity == NULL) {
        return false;
    }

    *out_activity = false;

    hw_tx_off();

    if (!wait_rx_active(IR_FRAME_WAIT_START_MS)) {
        return false;
    }

    *out_activity = true;

    uint32_t preamble_ms = measure_rx_active_ms(IR_FRAME_MAX_PULSE_MS);

    if (preamble_ms < IR_FRAME_PREAMBLE_MIN_MS || preamble_ms > IR_FRAME_PREAMBLE_MAX_MS) {
        return false;
    }

    /*
     * Esperar a que llegue el primer bit.
     */
    uint8_t payload = 0;

    for (int bit = 7; bit >= 0; bit--) {
        if (!wait_rx_active(IR_FRAME_PREAMBLE_OFF_MS + IR_FRAME_BIT_OFF_MS + 60)) {
            return false;
        }

        uint32_t pulse_ms = measure_rx_active_ms(IR_FRAME_MAX_PULSE_MS);
        bool one = pulse_ms >= IR_FRAME_BIT_THRESHOLD_MS;

        if (one) {
            payload |= (uint8_t)(1U << bit);
        }
    }

    uint8_t element_id = (payload >> 4) & 0x0F;
    uint8_t crc = payload & 0x0F;

    if (crc != frame_crc4(element_id)) {
        return false;
    }

    *out_element_id = element_id;
    return true;
}

// -----------------------------------------------------------------------------
// Elementos
// -----------------------------------------------------------------------------

static uint8_t element_id_from_name(const char *name)
{
    if (name == NULL) return 0x0F;

    if (strcmp(name, "agua") == 0) return 0;
    if (strcmp(name, "electricidad") == 0) return 1;
    if (strcmp(name, "fuego") == 0) return 2;
    if (strcmp(name, "humano") == 0) return 3;
    if (strcmp(name, "metal") == 0) return 4;
    if (strcmp(name, "mono") == 0) return 5;
    if (strcmp(name, "naturaleza") == 0) return 6;
    if (strcmp(name, "oeste") == 0) return 7;
    if (strcmp(name, "pajaro") == 0) return 8;
    if (strcmp(name, "piedra") == 0) return 9;
    if (strcmp(name, "pistola") == 0) return 10;
    if (strcmp(name, "reggaeton") == 0) return 11;
    if (strcmp(name, "robot") == 0) return 12;
    if (strcmp(name, "rock") == 0) return 13;
    if (strcmp(name, "tormenta") == 0) return 14;
    if (strcmp(name, "viento") == 0) return 15;

    return 0x0F;
}

static void copy_element_name_from_id(uint8_t id, char *dst, size_t dst_size)
{
    if (dst == NULL || dst_size == 0) {
        return;
    }

    const char *name = ir_link_element_name_from_id(id);
    strncpy(dst, name, dst_size - 1);
    dst[dst_size - 1] = '\0';
}

// -----------------------------------------------------------------------------
// Estado y eventos
// -----------------------------------------------------------------------------

static void enter_state(ir_ctx_t *ctx, ir_link_state_t state)
{
    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        ctx->state = state;
        ctx->state_since_us = now_us();
        xSemaphoreGive(s_mutex);
    } else {
        ctx->state = state;
        ctx->state_since_us = now_us();
    }
}

static void publish_event(const ir_ctx_t *ctx, ir_event_type_t type)
{
    if (s_event_q == NULL) {
        return;
    }

    ir_event_t ev = {
        .type = type,
        .state = ctx->state,
        .face = ctx->locked_face != IR_FACE_NONE ? ctx->locked_face : ctx->candidate_face,
        .role = ctx->role,
        .sync_time_us = ctx->sync_time_us,
        .remote_element_id = ctx->remote_element_id,
    };

    strncpy(ev.remote_element_name, ctx->remote_element_name, sizeof(ev.remote_element_name) - 1);
    ev.remote_element_name[sizeof(ev.remote_element_name) - 1] = '\0';

    xQueueSend(s_event_q, &ev, 0);
}

static void shuffle_order(ir_ctx_t *ctx)
{
    ctx->order[0] = 0;
    ctx->order[1] = 1;
    ctx->order[2] = 2;
    ctx->order[3] = 3;

    for (int i = IR_FACE_COUNT - 1; i > 0; i--) {
        int j = esp_random() % (i + 1);
        uint8_t tmp = ctx->order[i];
        ctx->order[i] = ctx->order[j];
        ctx->order[j] = tmp;
    }

    ctx->order_index = 0;
}

static void schedule_next_frame(ir_ctx_t *ctx)
{
    uint32_t delay_ms = IR_FRAME_TX_PERIOD_MIN_MS + rand_range_ms(IR_FRAME_TX_PERIOD_JITTER_MS);
    ctx->next_frame_tx_us = now_us() + ((uint64_t)delay_ms * 1000ULL);
}

// -----------------------------------------------------------------------------
// Máquina de estados
// -----------------------------------------------------------------------------

static void handle_search(ir_ctx_t *ctx)
{
    if ((now_ms() - (uint32_t)(ctx->search_started_us / 1000ULL)) > IR_SEARCH_TIMEOUT_MS) {
        hw_tx_off();
        ctx->locked_face = IR_FACE_NONE;
        ctx->candidate_face = IR_FACE_NONE;
        enter_state(ctx, IR_LINK_IDLE);
        publish_event(ctx, IR_EVENT_SEARCH_TIMEOUT);
        return;
    }

    if (ctx->order_index >= IR_FACE_COUNT) {
        shuffle_order(ctx);
    }

    ir_face_t face = (ir_face_t)ctx->order[ctx->order_index++];
    hw_select_face(face);

    if (rand_percent() < IR_SEARCH_TX_PROB_PERCENT) {
        signal_send_presence();
    } else {
        if (signal_receive_window(IR_RX_WINDOW_MS)) {
            ctx->candidate_face = face;
            ctx->confirm_hits = 1;
            ctx->last_rx_us = now_us();

            enter_state(ctx, IR_LINK_CONFIRMING_FACE);
            publish_event(ctx, IR_EVENT_CANDIDATE_FACE);
            vTaskDelay(pdMS_TO_TICKS(IR_CONFIRM_STEP_MS));
            return;
        }
    }

    vTaskDelay(pdMS_TO_TICKS(IR_SEARCH_STEP_MS));
}

static void handle_confirm(ir_ctx_t *ctx)
{
    uint32_t elapsed_ms = (uint32_t)((now_us() - ctx->state_since_us) / 1000ULL);

    if (elapsed_ms > IR_CONFIRM_TIMEOUT_MS) {
        ctx->candidate_face = IR_FACE_NONE;
        ctx->confirm_hits = 0;
        shuffle_order(ctx);
        enter_state(ctx, IR_LINK_SEARCHING);
        vTaskDelay(pdMS_TO_TICKS(IR_SEARCH_STEP_MS));
        return;
    }

    hw_select_face(ctx->candidate_face);

    if (rand_percent() < IR_CONFIRM_TX_PROB_PERCENT) {
        signal_send_presence();
    } else {
        if (signal_receive_window(IR_RX_WINDOW_MS)) {
            ctx->confirm_hits++;
            ctx->last_rx_us = now_us();
        }
    }

    if (ctx->confirm_hits >= IR_CONFIRM_REQUIRED_HITS) {
        ctx->locked_face = ctx->candidate_face;
        ctx->candidate_face = IR_FACE_NONE;

        enter_state(ctx, IR_LINK_LOCKED);
        publish_event(ctx, IR_EVENT_FACE_LOCKED);

        ctx->sync_hits = 0;
        enter_state(ctx, IR_LINK_SYNCING);
        vTaskDelay(pdMS_TO_TICKS(IR_SYNC_STEP_MS));
        return;
    }

    vTaskDelay(pdMS_TO_TICKS(IR_CONFIRM_STEP_MS));
}

static void handle_sync(ir_ctx_t *ctx)
{
    uint32_t elapsed_ms = (uint32_t)((now_us() - ctx->state_since_us) / 1000ULL);

    if (elapsed_ms > IR_SYNC_TIMEOUT_MS) {
        ctx->locked_face = IR_FACE_NONE;
        ctx->candidate_face = IR_FACE_NONE;
        ctx->role = IR_ROLE_UNKNOWN;
        enter_state(ctx, IR_LINK_IDLE);
        publish_event(ctx, IR_EVENT_LINK_LOST);
        return;
    }

    hw_select_face(ctx->locked_face);

    if (rand_percent() < IR_SYNC_TX_PROB_PERCENT) {
        signal_send_presence();
    } else {
        if (signal_receive_window(IR_RX_WINDOW_MS)) {
            ctx->sync_hits++;
            ctx->last_rx_us = now_us();
        }
    }

    if (ctx->sync_hits >= IR_SYNC_REQUIRED_HITS) {
        ctx->role = (rand_percent() < 50) ? IR_ROLE_LEADER : IR_ROLE_FOLLOWER;
        ctx->sync_time_us = now_us();
        ctx->last_rx_us = now_us();
        schedule_next_frame(ctx);

        enter_state(ctx, IR_LINK_READY);
        publish_event(ctx, IR_EVENT_SYNCED);
        vTaskDelay(pdMS_TO_TICKS(IR_READY_STEP_MS));
        return;
    }

    vTaskDelay(pdMS_TO_TICKS(IR_SYNC_STEP_MS));
}

static void handle_ready(ir_ctx_t *ctx)
{
    hw_select_face(ctx->locked_face);

    uint64_t t = now_us();

    if (ctx->last_rx_us != 0 && (t - ctx->last_rx_us) > ((uint64_t)IR_LOST_TIMEOUT_MS * 1000ULL)) {
        hw_tx_off();
        ctx->locked_face = IR_FACE_NONE;
        ctx->candidate_face = IR_FACE_NONE;
        ctx->role = IR_ROLE_UNKNOWN;
        enter_state(ctx, IR_LINK_IDLE);
        publish_event(ctx, IR_EVENT_LINK_LOST);
        vTaskDelay(pdMS_TO_TICKS(IR_READY_STEP_MS));
        return;
    }

    if (ctx->next_frame_tx_us == 0 || t >= ctx->next_frame_tx_us) {
        /*
         * Mandamos el elemento local. Como hay jitter en ambos cubos,
         * aunque alguna transmisión choque, otra debería llegar.
         */
        frame_send_element(ctx->local_element_id);
        schedule_next_frame(ctx);
    } else {
        bool activity = false;
        uint8_t remote_id = 0xFF;

        if (frame_try_receive_element(&remote_id, &activity)) {
            ctx->last_rx_us = now_us();
            ctx->sync_time_us = now_us();

            if (remote_id != ctx->remote_element_id) {
                ctx->remote_element_id = remote_id;
                copy_element_name_from_id(remote_id, ctx->remote_element_name, sizeof(ctx->remote_element_name));
                publish_event(ctx, IR_EVENT_REMOTE_ELEMENT_RX);
            } else {
                /*
                 * Aunque sea el mismo elemento, refrescamos vida del enlace.
                 */
                copy_element_name_from_id(remote_id, ctx->remote_element_name, sizeof(ctx->remote_element_name));
            }
        } else if (activity) {
            /*
             * Había actividad IR pero no frame válido. Aun así cuenta como presencia
             * para no perder enlace por colisiones parciales.
             */
            ctx->last_rx_us = now_us();
        }
    }

    vTaskDelay(pdMS_TO_TICKS(IR_READY_STEP_MS));
}
