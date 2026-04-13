#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/i2s_pdm.h"
#include "audio_core.h"
#include "string.h"


static int16_t buffers[BUFFER_COUNT][BUFFER_SAMPLES];
static QueueHandle_t ready_queue;   // buffers listos para reproducir
static QueueHandle_t free_queue;    // buffers libres para rellenar

// Generador actual (puede ser un wave table, un sintetizador, etc.)
static audio_generator_t *current_generator = NULL;

void set_audio_generator(audio_generator_t *gen)
{
    current_generator = gen;
}

void generate_audio(int16_t *buffer, size_t num_samples)
{
    if (current_generator)
        current_generator->generate(current_generator->state, buffer, num_samples);
    else
        memset(buffer, 0, num_samples * sizeof(int16_t));
}

// Tarea productora: genera audio y encola buffers rellenos
static void producer_task(void *arg)
{
    int16_t *buf;
    while (1) {
        // Espera un buffer libre
        xQueueReceive(free_queue, &buf, portMAX_DELAY);

        // Rellena el buffer
        generate_audio(buf, BUFFER_SAMPLES);

        // Encola para reproducción
        xQueueSend(ready_queue, &buf, portMAX_DELAY);
    }
}

// Tarea consumidora: envía buffers al I2S por DMA
static void consumer_task(void *arg)
{
    i2s_chan_handle_t i2s_tx_handle = (i2s_chan_handle_t)arg;
    int16_t *buf;
    size_t bytes_written;

    while (1) {
        // Espera buffer listo
        xQueueReceive(ready_queue, &buf, portMAX_DELAY);

        // Envía al I2S (bloqueante hasta que DMA lo acepta)
        i2s_channel_write(i2s_tx_handle, buf,
                          BUFFER_SAMPLES * sizeof(int16_t),
                          &bytes_written, portMAX_DELAY);

        // Devuelve el buffer al pool de libres
        xQueueSend(free_queue, &buf, portMAX_DELAY);
    }
}

void start_audio_engine(i2s_chan_handle_t i2s_tx_handle)
{
    // Crear queues
    ready_queue = xQueueCreate(BUFFER_COUNT, sizeof(int16_t *));
    free_queue  = xQueueCreate(BUFFER_COUNT, sizeof(int16_t *));

    // Meter todos los buffers en la cola de libres inicialmente
    for (int i = 0; i < BUFFER_COUNT; i++) {
        int16_t *ptr = buffers[i];
        xQueueSend(free_queue, &ptr, portMAX_DELAY);
    }

    // Lanzar tareas
    xTaskCreate(producer_task, "audio_prod", 4096, NULL,         5, NULL);
    xTaskCreate(consumer_task, "audio_cons", 4096, i2s_tx_handle,    6, NULL);
    // Consumidor con prioridad mayor para no dejar el I2S sin datos
}