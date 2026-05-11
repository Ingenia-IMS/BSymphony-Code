#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "esp_timer.h"

#include "leds/led_manager.h"
#include "imu/imu_manager.h"
#include "sonido/sound_player.h"
#include "elementos/cube_state.h"
#include "elementos/element_catalog.h"
#include "IR/ir_link.h"

#define LED_BRIGHTNESS_20_PERCENT 51
#define MAIN_TASK_DELAY_MS        10

#define SYNC_LED_PERIOD_US        450000ULL
#define SYNC_LED_ON_US            150000ULL

static const char *TAG = "MAIN";

static const char *INITIAL_ELEMENTS[] = {
    "agua",
    "fuego",
    "viento",
};

#define INITIAL_ELEMENT_COUNT \
    (sizeof(INITIAL_ELEMENTS) / sizeof(INITIAL_ELEMENTS[0]))

static size_t s_initial_element_index = 0;

static volatile bool s_shake_requested = false;
static volatile bool s_pickup_requested = false;

static bool s_ir_ready_visual = false;
static ir_role_t s_visual_role = IR_ROLE_UNKNOWN;
static uint64_t s_visual_sync_time_us = 0;
static bool s_visual_last_on = false;

static bool s_combination_done_for_link = false;

static void on_imu_shake(void)
{
    s_shake_requested = true;
}

static void on_imu_pickup(void)
{
    s_pickup_requested = true;
}

static void update_ir_advertised_element(void)
{
    ir_link_set_local_element_name(cube_state_get_current_name());
}

static void set_initial_element_by_index(size_t index, bool play_sound)
{
    s_initial_element_index = index % INITIAL_ELEMENT_COUNT;

    const char *name = INITIAL_ELEMENTS[s_initial_element_index];

    ESP_LOGI(TAG, "Elemento inicial seleccionado: %s", name);

    if (cube_state_set_element_by_name(name)) {
        update_ir_advertised_element();

        if (play_sound) {
            cube_state_play_current_sound();
        }
    } else {
        ESP_LOGW(TAG, "No existe el elemento inicial: %s", name);
    }
}

static void select_next_initial_element(void)
{
    set_initial_element_by_index(s_initial_element_index + 1, true);
}

static void sync_initial_index_with_current_element(void)
{
    const char *current = cube_state_get_current_name();

    if (current == NULL) {
        return;
    }

    for (size_t i = 0; i < INITIAL_ELEMENT_COUNT; i++) {
        if (strcmp(current, INITIAL_ELEMENTS[i]) == 0) {
            s_initial_element_index = i;
            return;
        }
    }
}

static bool ir_is_active(void)
{
    ir_status_t st;

    if (!ir_link_get_status(&st)) {
        return false;
    }

    return st.state != IR_LINK_IDLE;
}

static void reset_link_combination_state(void)
{
    s_combination_done_for_link = false;
}

static void led_show_searching(void)
{
    s_ir_ready_visual = false;
    led_manager_set_blink_enabled(true);
    led_manager_set_solid(LED_COLOR_YELLOW);
}

static void led_show_candidate(void)
{
    s_ir_ready_visual = false;
    led_manager_set_blink_enabled(true);
    led_manager_set_solid(LED_COLOR_ORANGE);
}

static void led_show_locked_face(ir_face_t face)
{
    s_ir_ready_visual = false;
    led_manager_set_blink_enabled(false);

    switch (face) {
        case IR_FACE_0:
            led_manager_set_solid(LED_COLOR_BLUE);
            break;
        case IR_FACE_1:
            led_manager_set_solid(LED_COLOR_LIGHT_BLUE);
            break;
        case IR_FACE_2:
            led_manager_set_solid(LED_COLOR_PURPLE);
            break;
        case IR_FACE_3:
            led_manager_set_solid(LED_COLOR_PINK);
            break;
        default:
            led_manager_set_solid(LED_COLOR_WHITE);
            break;
    }
}

static void led_start_synced_visual(ir_role_t role, uint64_t sync_time_us)
{
    led_manager_set_blink_enabled(false);

    s_ir_ready_visual = true;
    s_visual_role = role;
    s_visual_sync_time_us = sync_time_us;
    s_visual_last_on = false;
}

static void led_stop_synced_visual(void)
{
    s_ir_ready_visual = false;
    led_manager_set_blink_enabled(false);
}

static void led_show_current_element(void)
{
    led_stop_synced_visual();

    const char *name = cube_state_get_current_name();

    if (name != NULL) {
        cube_state_set_element_by_name(name);
    } else {
        led_manager_set_off();
    }
}

static void led_show_error(void)
{
    led_stop_synced_visual();
    led_manager_set_solid(LED_COLOR_RED);
}

static void led_update_synced_visual(void)
{
    if (!s_ir_ready_visual) {
        return;
    }

    uint64_t now_us = (uint64_t)esp_timer_get_time();
    uint64_t phase_us = (now_us - s_visual_sync_time_us) % SYNC_LED_PERIOD_US;
    bool on = phase_us < SYNC_LED_ON_US;

    if (on == s_visual_last_on) {
        return;
    }

    s_visual_last_on = on;

    if (!on) {
        led_manager_set_off();
        return;
    }

    if (s_visual_role == IR_ROLE_LEADER) {
        led_manager_set_solid(LED_COLOR_GREEN);
    } else if (s_visual_role == IR_ROLE_FOLLOWER) {
        led_manager_set_solid(LED_COLOR_CYAN);
    } else {
        led_manager_set_solid(LED_COLOR_WHITE);
    }
}

static void apply_remote_element_if_needed(const ir_event_t *ev)
{
    const char *local_name = cube_state_get_current_name();
    const char *remote_name = ev->remote_element_name;

    ESP_LOGI(TAG,
             "Elemento remoto: id=%u name=%s | local=%s | role=%s",
             ev->remote_element_id,
             remote_name,
             local_name,
             ir_link_role_name(ev->role));

    if (s_combination_done_for_link) {
        ESP_LOGI(TAG, "No transformo: esta conexión ya fue procesada");
        return;
    }

    const char *result = element_catalog_get_local_change_result(local_name, remote_name);

    if (result == NULL) {
        ESP_LOGI(TAG,
                 "Sin cambio local: local=%s remote=%s",
                 local_name,
                 remote_name);
        return;
    }

    s_combination_done_for_link = true;

    ESP_LOGI(TAG,
             "COMBINACION LOCAL: %s + %s -> %s",
             local_name,
             remote_name,
             result);

    led_stop_synced_visual();

    if (!cube_state_set_element_by_name(result)) {
        ESP_LOGW(TAG, "Resultado de combinación inexistente: %s", result);
        return;
    }

    cube_state_play_current_sound();
    update_ir_advertised_element();
    sync_initial_index_with_current_element();
}

static void handle_ir_search_timeout(void)
{
    ESP_LOGI(TAG, "IR timeout: no se encontró otro cubo. Cambio elemento inicial.");

    reset_link_combination_state();

    led_show_error();
    vTaskDelay(pdMS_TO_TICKS(200));

    select_next_initial_element();
}

static void handle_ir_link_lost(void)
{
    ESP_LOGI(TAG, "IR perdido: 3 s sin ver al otro cubo");

    reset_link_combination_state();

    led_show_error();
    vTaskDelay(pdMS_TO_TICKS(300));
    led_show_current_element();
}

static void handle_ir_event(const ir_event_t *ev)
{
    ESP_LOGI(TAG,
             "IR event=%s state=%s face=%d(%s) role=%s",
             ir_link_event_name(ev->type),
             ir_link_state_name(ev->state),
             ev->face,
             ir_link_face_name(ev->face),
             ir_link_role_name(ev->role));

    switch (ev->type) {
        case IR_EVENT_SEARCH_STARTED:
            reset_link_combination_state();
            led_show_searching();
            break;

        case IR_EVENT_CANDIDATE_FACE:
            led_show_candidate();
            break;

        case IR_EVENT_FACE_LOCKED:
            led_show_locked_face(ev->face);
            break;

        case IR_EVENT_SYNCED:
            reset_link_combination_state();
            led_start_synced_visual(ev->role, ev->sync_time_us);
            break;

        case IR_EVENT_REMOTE_ELEMENT_RX:
            apply_remote_element_if_needed(ev);
            break;

        case IR_EVENT_SEARCH_TIMEOUT:
            handle_ir_search_timeout();
            break;

        case IR_EVENT_LINK_LOST:
            handle_ir_link_lost();
            break;

        case IR_EVENT_STOPPED:
            reset_link_combination_state();
            led_show_current_element();
            break;

        default:
            break;
    }
}

static void process_ir_events(void)
{
    ir_event_t ev;

    while (ir_link_get_event(&ev, 0)) {
        handle_ir_event(&ev);
    }
}

static void process_pickup_request(void)
{
    if (!s_pickup_requested) {
        return;
    }

    s_pickup_requested = false;

    if (ir_is_active()) {
        ESP_LOGI(TAG, "Mover suave ignorado: IR activo");
        return;
    }

    ESP_LOGI(TAG,
             "Mover suave -> sonido del elemento actual: %s",
             cube_state_get_current_name());

    cube_state_play_current_sound();
}

static void process_shake_request(void)
{
    if (!s_shake_requested) {
        return;
    }

    s_shake_requested = false;

    ir_status_t st;

    if (!ir_link_get_status(&st) || st.state != IR_LINK_IDLE) {
        ESP_LOGI(TAG, "Agitado ignorado: IR no está idle");
        return;
    }

    /*
     * Siempre se intenta comunicar primero.
     * Si no encuentra otro cubo, IR_EVENT_SEARCH_TIMEOUT hará el cambio:
     * agua -> fuego -> viento -> agua.
     */
    ESP_LOGI(TAG, "Agitado vigoroso -> empieza búsqueda IR");

    ir_link_start_search();
    led_show_searching();
}

static void init_leds(void)
{
    ESP_ERROR_CHECK(led_manager_init());
    ESP_ERROR_CHECK(led_manager_set_master_brightness(LED_BRIGHTNESS_20_PERCENT));
    ESP_ERROR_CHECK(led_manager_set_blink_enabled(false));
    ESP_ERROR_CHECK(led_manager_set_off());
}

static void init_imu(void)
{
    imu_init();
    imu_set_pickup_callback(on_imu_pickup);
    imu_set_shake_callback(on_imu_shake);
    imu_start_task();
}

static void init_ir(void)
{
    ir_link_init();
    update_ir_advertised_element();
}

void app_main(void)
{
    ESP_LOGI(TAG, "Inicio firmware cubo con comunicación IR y combinaciones");

    init_leds();
    sound_player_init();
    cube_state_init();

    /*
     * El cubo empieza siempre en uno de los tres elementos básicos.
     *
     * Índices:
     *   0 = agua
     *   1 = fuego
     *   2 = viento
     */
    set_initial_element_by_index(0, false);

    init_imu();
    init_ir();

    ESP_LOGI(TAG, "Listo. Sacude para comunicar o, si no hay cubo, cambiar agua/fuego/viento.");

    while (1) {
        update_ir_advertised_element();

        process_pickup_request();
        process_shake_request();
        process_ir_events();
        led_update_synced_visual();

        vTaskDelay(pdMS_TO_TICKS(MAIN_TASK_DELAY_MS));
    }
}
