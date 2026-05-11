#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"

#include "leds/led_manager.h"
#include "imu/imu_manager.h"
#include "sonido/sound_player.h"
#include "elementos/cube_state.h"
#include "elementos/element_catalog.h"
#include "IR/ir_link.h"

#define LED_BRIGHTNESS_20_PERCENT 51
#define MAIN_TASK_DELAY_MS        10

/*
 * Búsqueda: parpadeo lento manteniendo el color/efecto del elemento actual.
 * Emparejado: usamos el blink nativo del led_manager, que es más rápido.
 */
#define SEARCH_BLINK_HALF_MS      350

/*
 * Reset externo sin abrir el cubo:
 *
 * Tres agitados vigorosos rápidos reinician el elemento del cubo a uno
 * aleatorio entre agua, fuego y viento.
 *
 * No depende de que fallen búsquedas IR.
 * Funciona aunque el cubo esté buscando, emparejado o en cualquier elemento.
 */
#define RESET_SHAKE_REQUIRED_COUNT     3
#define RESET_SHAKE_MAX_GAP_MS         1200

static const char *TAG = "MAIN";

// -----------------------------------------------------------------------------
// Elementos iniciales
// -----------------------------------------------------------------------------
//
// En el juego hablamos de "aire", pero el elemento implementado se llama
// "viento" en el catálogo.
//

static const char *INITIAL_ELEMENTS[] = {
    "agua",
    "fuego",
    "viento",
};

#define INITIAL_ELEMENT_COUNT \
    (sizeof(INITIAL_ELEMENTS) / sizeof(INITIAL_ELEMENTS[0]))

static size_t s_initial_element_index = 0;

// -----------------------------------------------------------------------------
// Flags desde callbacks IMU
// -----------------------------------------------------------------------------

static volatile bool s_shake_requested = false;
static volatile bool s_pickup_requested = false;

// -----------------------------------------------------------------------------
// Estado de combinación / conexión
// -----------------------------------------------------------------------------

/*
 * Evita varias transformaciones dentro de la misma conexión IR.
 *
 * También sirve para que, una vez recibido el elemento remoto y decidido
 * si hay cambio o no, se apague el parpadeo de emparejado y se mantenga
 * el elemento actual hasta que se separen los cubos.
 */
static bool s_exchange_done_for_link = false;

// -----------------------------------------------------------------------------
// Estado de secuencia de reset por triple agitado
// -----------------------------------------------------------------------------

static uint8_t s_reset_shake_count = 0;
static uint32_t s_last_reset_shake_ms = 0;

// -----------------------------------------------------------------------------
// Estado visual
// -----------------------------------------------------------------------------

typedef enum {
    VISUAL_IDLE = 0,
    VISUAL_SEARCH_BLINK,
    VISUAL_PAIRED_FAST_BLINK,
} visual_mode_t;

static visual_mode_t s_visual_mode = VISUAL_IDLE;
static bool s_search_visible = true;
static uint32_t s_last_search_blink_ms = 0;

// -----------------------------------------------------------------------------
// Estado de inicialización
// -----------------------------------------------------------------------------

static bool s_ir_initialized = false;

// -----------------------------------------------------------------------------
// Tiempo
// -----------------------------------------------------------------------------

static uint32_t now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000ULL);
}

// -----------------------------------------------------------------------------
// Callbacks IMU
// -----------------------------------------------------------------------------

static void on_imu_shake(void)
{
    s_shake_requested = true;
}

static void on_imu_pickup(void)
{
    s_pickup_requested = true;
}

// -----------------------------------------------------------------------------
// Helpers IR / elemento
// -----------------------------------------------------------------------------

static void update_ir_advertised_element(void)
{
    if (!s_ir_initialized) {
        return;
    }

    ir_link_set_local_element_name(cube_state_get_current_name());
}

static bool ir_is_active(void)
{
    ir_status_t st;

    if (!ir_link_get_status(&st)) {
        return false;
    }

    return st.state != IR_LINK_IDLE;
}

static void apply_current_element_light(void)
{
    const char *name = cube_state_get_current_name();

    if (name != NULL) {
        cube_state_set_element_by_name(name);
    } else {
        led_manager_set_off();
    }
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

static void choose_random_initial_element(bool play_sound)
{
    size_t index = esp_random() % INITIAL_ELEMENT_COUNT;
    set_initial_element_by_index(index, play_sound);
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

// -----------------------------------------------------------------------------
// Visuales
// -----------------------------------------------------------------------------

static void visual_idle_current_element(void)
{
    s_visual_mode = VISUAL_IDLE;
    s_search_visible = true;

    led_manager_set_blink_enabled(false);
    apply_current_element_light();
}

static void visual_start_search_blink(void)
{
    s_visual_mode = VISUAL_SEARCH_BLINK;
    s_search_visible = true;
    s_last_search_blink_ms = now_ms();

    led_manager_set_blink_enabled(false);
    apply_current_element_light();
}

static void visual_start_paired_fast_blink(void)
{
    s_visual_mode = VISUAL_PAIRED_FAST_BLINK;
    s_search_visible = true;

    /*
     * Mismo elemento, pero parpadeo rápido.
     * El led_manager ya implementa ese blink internamente.
     */
    apply_current_element_light();
    led_manager_set_blink_enabled(true);
}

static void visual_stop_pairing_blink_keep_element(void)
{
    s_visual_mode = VISUAL_IDLE;
    s_search_visible = true;

    led_manager_set_blink_enabled(false);
    apply_current_element_light();
}

static void visual_flash_green_then_current(void)
{
    s_visual_mode = VISUAL_IDLE;
    s_search_visible = true;

    led_manager_set_blink_enabled(false);
    led_manager_set_solid(LED_COLOR_GREEN);
    vTaskDelay(pdMS_TO_TICKS(80));

    apply_current_element_light();
}

static void visual_flash_reset_then_current(void)
{
    s_visual_mode = VISUAL_IDLE;
    s_search_visible = true;

    led_manager_set_blink_enabled(false);

    /*
     * Triple flash verde para distinguirlo del flash corto de desconexión.
     */
    for (uint8_t i = 0; i < 3; i++) {
        led_manager_set_solid(LED_COLOR_GREEN);
        vTaskDelay(pdMS_TO_TICKS(70));
        led_manager_set_off();
        vTaskDelay(pdMS_TO_TICKS(70));
    }

    apply_current_element_light();
}

static void visual_update(void)
{
    if (s_visual_mode != VISUAL_SEARCH_BLINK) {
        return;
    }

    uint32_t t = now_ms();

    if ((t - s_last_search_blink_ms) < SEARCH_BLINK_HALF_MS) {
        return;
    }

    s_last_search_blink_ms = t;
    s_search_visible = !s_search_visible;

    if (s_search_visible) {
        apply_current_element_light();
    } else {
        led_manager_set_off();
    }
}

// -----------------------------------------------------------------------------
// Reset externo por triple agitado
// -----------------------------------------------------------------------------

static void reset_triple_shake_sequence(void)
{
    s_reset_shake_count = 0;
    s_last_reset_shake_ms = 0;
}

static bool register_shake_for_external_reset(void)
{
    uint32_t t = now_ms();

    if (s_last_reset_shake_ms == 0 ||
        (t - s_last_reset_shake_ms) > RESET_SHAKE_MAX_GAP_MS) {
        s_reset_shake_count = 0;
    }

    s_last_reset_shake_ms = t;
    s_reset_shake_count++;

    ESP_LOGI(TAG,
             "Secuencia reset por agitado: %u/%u",
             s_reset_shake_count,
             RESET_SHAKE_REQUIRED_COUNT);

    if (s_reset_shake_count < RESET_SHAKE_REQUIRED_COUNT) {
        return false;
    }

    reset_triple_shake_sequence();
    return true;
}

static void perform_external_random_reset(void)
{
    ESP_LOGI(TAG, "Reset externo por triple agitado: nuevo elemento inicial aleatorio");

    /*
     * Si estaba buscando o enlazado por IR, cortamos esa sesión.
     * Así el reset deja el cubo limpio y disponible.
     */
    ir_link_stop();

    s_exchange_done_for_link = false;

    choose_random_initial_element(true);
    visual_flash_reset_then_current();
    update_ir_advertised_element();
}

// -----------------------------------------------------------------------------
// Transformación
// -----------------------------------------------------------------------------

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

    /*
     * Ya se recibió un elemento remoto en esta conexión.
     * No reintentamos ni encadenamos cambios.
     */
    if (s_exchange_done_for_link) {
        ESP_LOGI(TAG, "Intercambio ya procesado en esta conexión");
        return;
    }

    s_exchange_done_for_link = true;

    const char *result = element_catalog_get_local_change_result(local_name, remote_name);

    if (result == NULL) {
        ESP_LOGI(TAG,
                 "Sin cambio local: local=%s remote=%s",
                 local_name,
                 remote_name);

        /*
         * Ya terminó el intercambio: no hubo transformación,
         * pero visualmente salimos del modo emparejado.
         */
        visual_stop_pairing_blink_keep_element();
        return;
    }

    ESP_LOGI(TAG,
             "COMBINACION LOCAL: %s + %s -> %s",
             local_name,
             remote_name,
             result);

    led_manager_set_blink_enabled(false);

    if (!cube_state_set_element_by_name(result)) {
        ESP_LOGW(TAG, "Resultado de combinación inexistente: %s", result);
        visual_stop_pairing_blink_keep_element();
        return;
    }

    cube_state_play_current_sound();
    update_ir_advertised_element();
    sync_initial_index_with_current_element();

    /*
     * Ya terminó el intercambio y se ha transformado:
     * queda fijo mostrando el nuevo elemento.
     */
    visual_stop_pairing_blink_keep_element();
}

// -----------------------------------------------------------------------------
// Eventos IR
// -----------------------------------------------------------------------------

static void handle_ir_search_timeout(void)
{
    ESP_LOGI(TAG, "IR timeout: no se encontró otro cubo");

    s_exchange_done_for_link = false;

    /*
     * Ya NO cambia agua/fuego/viento por cada búsqueda fallida.
     * El cambio aleatorio de elemento inicial se hace solo con triple agitado.
     */
    visual_idle_current_element();
}

static void handle_ir_link_lost(void)
{
    ESP_LOGI(TAG, "IR perdido: 3 s sin ver al otro cubo");

    s_exchange_done_for_link = false;

    /*
     * Flash verde muy breve para indicar desconexión.
     * Luego vuelve a mostrar el elemento actual y queda disponible.
     */
    visual_flash_green_then_current();
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
            s_exchange_done_for_link = false;
            visual_start_search_blink();
            break;

        case IR_EVENT_CANDIDATE_FACE:
            /*
             * Desde fuera no cambiamos de color.
             * Sigue parpadeando el elemento actual.
             */
            break;

        case IR_EVENT_FACE_LOCKED:
            /*
             * Ya encontró cara, pero seguimos mostrando el mismo elemento.
             */
            break;

        case IR_EVENT_SYNCED:
            /*
             * Emparejado: mismo elemento, parpadeo más rápido.
             */
            s_exchange_done_for_link = false;
            visual_start_paired_fast_blink();
            break;

        case IR_EVENT_REMOTE_ELEMENT_RX:
            /*
             * Acabó el intercambio, con o sin transformación.
             * Esta función se encarga de quitar el parpadeo.
             */
            apply_remote_element_if_needed(ev);
            break;

        case IR_EVENT_SEARCH_TIMEOUT:
            handle_ir_search_timeout();
            break;

        case IR_EVENT_LINK_LOST:
            handle_ir_link_lost();
            break;

        case IR_EVENT_STOPPED:
            s_exchange_done_for_link = false;
            visual_idle_current_element();
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

// -----------------------------------------------------------------------------
// Acciones por IMU
// -----------------------------------------------------------------------------

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

    /*
     * Primero registramos el agitado para la secuencia de reset.
     * Esto ocurre siempre: da igual que el cubo esté idle, buscando, enlazado
     * o en cualquier elemento.
     */
    if (register_shake_for_external_reset()) {
        perform_external_random_reset();
        return;
    }

    /*
     * Además, un agitado vigoroso intenta comunicar si el IR está disponible.
     */
    ir_status_t st;

    if (!ir_link_get_status(&st) || st.state != IR_LINK_IDLE) {
        ESP_LOGI(TAG, "Agitado registrado para reset, pero IR no está idle");
        return;
    }

    ESP_LOGI(TAG, "Agitado vigoroso -> empieza búsqueda IR");

    ir_link_start_search();
    visual_start_search_blink();
}

// -----------------------------------------------------------------------------
// Inicialización
// -----------------------------------------------------------------------------

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
    s_ir_initialized = true;
    update_ir_advertised_element();
}

// -----------------------------------------------------------------------------
// Main
// -----------------------------------------------------------------------------

void app_main(void)
{
    ESP_LOGI(TAG, "Inicio firmware cubo con comunicación IR y combinaciones");

    init_leds();
    sound_player_init();
    cube_state_init();

    /*
     * Al resetear/encender el cubo, escoge aleatoriamente uno de:
     * agua, fuego o viento.
     */
    choose_random_initial_element(false);

    init_imu();
    init_ir();

    ESP_LOGI(TAG, "Listo. Sacude para comunicar. Triple agitado rápido = reset aleatorio.");

    while (1) {
        update_ir_advertised_element();

        process_pickup_request();
        process_shake_request();
        process_ir_events();
        visual_update();

        vTaskDelay(pdMS_TO_TICKS(MAIN_TASK_DELAY_MS));
    }
}
