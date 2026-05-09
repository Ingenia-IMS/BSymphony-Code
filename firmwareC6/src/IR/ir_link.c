#include "ir_link.h"
#include "ir_config.h"

#include <string.h>

#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

static const char *TAG = "IR";

#define IR_FACE_COUNT 4

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

    uint64_t state_since_us;
    uint64_t search_started_us;
    uint64_t sync_time_us;
    uint64_t last_rx_us;
    uint64_t last_sync_tx_us;
    uint64_t last_presence_tx_us;
    uint64_t claim_deadline_us;
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
static void delay_us(uint32_t us);

static void enter_state(ir_ctx_t *ctx, ir_link_state_t state);
static void publish_event(const ir_ctx_t *ctx, ir_event_type_t type);
static void shuffle_order(ir_ctx_t *ctx);
static uint32_t random_percent(void);

static void signal_send_beacon(uint8_t bursts);
static bool signal_detect_activity(uint32_t window_us);

static void handle_search(ir_ctx_t *ctx);
static void handle_confirm(ir_ctx_t *ctx);
static void handle_syncing(ir_ctx_t *ctx);
static void handle_ready(ir_ctx_t *ctx);

void ir_link_init(void)
{
    memset(&s_ctx, 0, sizeof(s_ctx));
    s_ctx.state = IR_LINK_IDLE;
    s_ctx.locked_face = IR_FACE_NONE;
    s_ctx.candidate_face = IR_FACE_NONE;
    s_ctx.role = IR_ROLE_UNKNOWN;

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

void ir_link_start_search(void)
{
    if (s_cmd_q == NULL) return;

    ir_status_t st;
    if (ir_link_get_status(&st)) {
        if (st.state != IR_LINK_IDLE) {
            ESP_LOGI(TAG, "Ignoro start_search porque estado actual=%s", ir_link_state_name(st.state));
            return;
        }
    }

    ir_cmd_t cmd = {.type = IR_CMD_START_SEARCH};
    xQueueSend(s_cmd_q, &cmd, 0);
}

void ir_link_stop(void)
{
    if (s_cmd_q == NULL) return;
    ir_cmd_t cmd = {.type = IR_CMD_STOP};
    xQueueSend(s_cmd_q, &cmd, 0);
}

bool ir_link_get_event(ir_event_t *out, uint32_t timeout_ms)
{
    if (out == NULL || s_event_q == NULL) return false;
    return xQueueReceive(s_event_q, out, pdMS_TO_TICKS(timeout_ms)) == pdTRUE;
}

bool ir_link_get_status(ir_status_t *out)
{
    if (out == NULL || s_mutex == NULL) return false;

    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(10)) != pdTRUE) {
        return false;
    }

    out->state = s_ctx.state;
    out->locked_face = s_ctx.locked_face;
    out->candidate_face = s_ctx.candidate_face;
    out->role = s_ctx.role;
    out->sync_time_us = s_ctx.sync_time_us;
    out->last_rx_us = s_ctx.last_rx_us;

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
    if (!ir_link_get_status(&st)) return IR_FACE_NONE;
    return st.locked_face;
}

ir_role_t ir_link_get_role(void)
{
    ir_status_t st;
    if (!ir_link_get_status(&st)) return IR_ROLE_UNKNOWN;
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
        default: return "UNKNOWN";
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
                s_ctx.last_rx_us = 0;
                s_ctx.sync_time_us = 0;
                s_ctx.last_sync_tx_us = 0;
                s_ctx.last_presence_tx_us = 0;
                s_ctx.search_started_us = now_us();
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
                handle_syncing(&s_ctx);
                break;

            case IR_LINK_READY:
                handle_ready(&s_ctx);
                break;

            case IR_LINK_IDLE:
            case IR_LINK_LOCKED:
            default:
                vTaskDelay(pdMS_TO_TICKS(25));
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

    ESP_LOGI(TAG, "init SEL0=%d SEL1=%d RX=%d TX=%d active=%d",
             IR_SEL0_GPIO, IR_SEL1_GPIO, IR_RX_GPIO, IR_TX_GPIO, IR_RX_ACTIVE_LEVEL);
}

static void hw_select_face(ir_face_t face)
{
    if (face < IR_FACE_0 || face > IR_FACE_3) {
        hw_tx_off();
        return;
    }

    uint8_t f = (uint8_t)face;

    gpio_set_level(IR_SEL0_GPIO, f & 0x01);
    gpio_set_level(IR_SEL1_GPIO, (f >> 1) & 0x01);

    delay_us(20);
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

static void delay_us(uint32_t us)
{
    esp_rom_delay_us(us);
}

// -----------------------------------------------------------------------------
// Señal IR mínima
// -----------------------------------------------------------------------------

static void signal_send_beacon(uint8_t bursts)
{
    for (uint8_t i = 0; i < bursts; i++) {
        hw_tx_on();
        delay_us(IR_BEACON_BURST_US);
        hw_tx_off();
        delay_us(IR_BEACON_GAP_US);
    }
}

static bool signal_detect_activity(uint32_t window_us)
{
    /*
     * Ventana de recepción pura: NO se emite nada aquí.
     * Esto es clave para evitar auto-detección.
     */
    hw_tx_off();

    uint64_t end = now_us() + window_us;
    uint64_t active_since = 0;

    while (now_us() < end) {
        if (hw_rx_active()) {
            if (active_since == 0) {
                active_since = now_us();
            } else if ((now_us() - active_since) >= IR_RX_MIN_ACTIVE_US) {
                return true;
            }
        } else {
            active_since = 0;
        }

        delay_us(IR_RX_POLL_US);
    }

    return false;
}

// -----------------------------------------------------------------------------
// Máquina de estados
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
    if (s_event_q == NULL) return;

    ir_event_t ev = {
        .type = type,
        .state = ctx->state,
        .face = ctx->locked_face != IR_FACE_NONE ? ctx->locked_face : ctx->candidate_face,
        .role = ctx->role,
        .sync_time_us = ctx->sync_time_us,
    };

    xQueueSend(s_event_q, &ev, 0);
}

static uint32_t random_percent(void)
{
    return esp_random() % 100U;
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

static void handle_search(ir_ctx_t *ctx)
{
    uint64_t t = now_us();

    if (t - ctx->search_started_us > IR_SEARCH_TIMEOUT_US) {
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

    bool tx_slot = random_percent() < IR_SEARCH_TX_PROB_PERCENT;

    if (tx_slot) {
        /*
         * Emito, pero NO cuento RX en este slot.
         * Así no me bloqueo por mi propia emisión.
         */
        signal_send_beacon(IR_BEACON_BURSTS);
    } else {
        /*
         * Escucha pura.
         */
        if (signal_detect_activity(IR_SEARCH_RX_WINDOW_US)) {
            ctx->candidate_face = face;
            ctx->confirm_hits = 1;
            ctx->last_rx_us = now_us();
            enter_state(ctx, IR_LINK_CONFIRMING_FACE);
            publish_event(ctx, IR_EVENT_CANDIDATE_FACE);
            vTaskDelay(pdMS_TO_TICKS(IR_CONFIRM_TICK_MS));
            return;
        }
    }

    vTaskDelay(pdMS_TO_TICKS(IR_SEARCH_TICK_MS));
}

static void handle_confirm(ir_ctx_t *ctx)
{
    uint64_t t = now_us();

    if (ctx->candidate_face == IR_FACE_NONE) {
        enter_state(ctx, IR_LINK_SEARCHING);
        return;
    }

    if (t - ctx->state_since_us > IR_CONFIRM_TIMEOUT_US) {
        ctx->candidate_face = IR_FACE_NONE;
        ctx->confirm_hits = 0;
        shuffle_order(ctx);
        enter_state(ctx, IR_LINK_SEARCHING);
        return;
    }

    hw_select_face(ctx->candidate_face);

    bool tx_slot = random_percent() < IR_CONFIRM_TX_PROB_PERCENT;

    if (tx_slot) {
        signal_send_beacon(IR_BEACON_BURSTS);
    } else {
        if (signal_detect_activity(IR_CONFIRM_RX_WINDOW_US)) {
            ctx->confirm_hits++;
            ctx->last_rx_us = now_us();
        }
    }

    if (ctx->confirm_hits >= IR_CONFIRM_REQUIRED_HITS) {
        ctx->locked_face = ctx->candidate_face;
        ctx->candidate_face = IR_FACE_NONE;
        ctx->role = IR_ROLE_UNKNOWN;
        hw_select_face(ctx->locked_face);

        enter_state(ctx, IR_LINK_LOCKED);
        publish_event(ctx, IR_EVENT_FACE_LOCKED);

        ctx->claim_deadline_us = now_us() + IR_SYNC_CLAIM_MIN_US + (esp_random() % IR_SYNC_CLAIM_RANDOM_US);
        enter_state(ctx, IR_LINK_SYNCING);
        vTaskDelay(pdMS_TO_TICKS(IR_SYNC_TICK_MS));
        return;
    }

    vTaskDelay(pdMS_TO_TICKS(IR_CONFIRM_TICK_MS));
}

static void handle_syncing(ir_ctx_t *ctx)
{
    uint64_t t = now_us();

    if (t - ctx->state_since_us > IR_SYNC_TIMEOUT_US) {
        ctx->locked_face = IR_FACE_NONE;
        ctx->candidate_face = IR_FACE_NONE;
        ctx->role = IR_ROLE_UNKNOWN;
        enter_state(ctx, IR_LINK_IDLE);
        publish_event(ctx, IR_EVENT_LINK_LOST);
        return;
    }

    hw_select_face(ctx->locked_face);

    if (t < ctx->claim_deadline_us) {
        if (signal_detect_activity(IR_SYNC_RX_WINDOW_US)) {
            ctx->role = IR_ROLE_FOLLOWER;
            ctx->last_rx_us = now_us();
            ctx->sync_time_us = now_us();
            enter_state(ctx, IR_LINK_READY);
            publish_event(ctx, IR_EVENT_SYNCED);
            vTaskDelay(pdMS_TO_TICKS(IR_READY_TICK_MS));
            return;
        }

        if (random_percent() < IR_SYNC_TX_PROB_PERCENT) {
            signal_send_beacon(IR_BEACON_BURSTS);
        }

        vTaskDelay(pdMS_TO_TICKS(IR_SYNC_TICK_MS));
        return;
    }

    ctx->role = IR_ROLE_LEADER;
    ctx->sync_time_us = now_us();
    signal_send_beacon(5);
    ctx->last_sync_tx_us = now_us();
    ctx->last_presence_tx_us = now_us();
    ctx->last_rx_us = now_us();

    enter_state(ctx, IR_LINK_READY);
    publish_event(ctx, IR_EVENT_SYNCED);
    vTaskDelay(pdMS_TO_TICKS(IR_READY_TICK_MS));
}

static void handle_ready(ir_ctx_t *ctx)
{
    uint64_t t = now_us();

    hw_select_face(ctx->locked_face);

    if (signal_detect_activity(IR_READY_RX_WINDOW_US)) {
        ctx->last_rx_us = now_us();

        if (ctx->role == IR_ROLE_FOLLOWER) {
            ctx->sync_time_us = now_us();
        }
    }

    if (ctx->last_rx_us != 0 && (t - ctx->last_rx_us > IR_LOST_TIMEOUT_US)) {
        hw_tx_off();
        ctx->locked_face = IR_FACE_NONE;
        ctx->candidate_face = IR_FACE_NONE;
        ctx->role = IR_ROLE_UNKNOWN;
        enter_state(ctx, IR_LINK_IDLE);
        publish_event(ctx, IR_EVENT_LINK_LOST);
        vTaskDelay(pdMS_TO_TICKS(IR_READY_TICK_MS));
        return;
    }

    if (ctx->role == IR_ROLE_LEADER) {
        if (t - ctx->last_sync_tx_us >= IR_SYNC_PERIOD_US) {
            ctx->sync_time_us = now_us();
            signal_send_beacon(5);
            ctx->last_sync_tx_us = now_us();
        }
    } else if (ctx->role == IR_ROLE_FOLLOWER) {
        if (t - ctx->last_presence_tx_us >= IR_PRESENCE_PERIOD_US) {
            signal_send_beacon(2);
            ctx->last_presence_tx_us = now_us();
        }
    }

    vTaskDelay(pdMS_TO_TICKS(IR_READY_TICK_MS));
}
