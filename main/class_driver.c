#include <stdlib.h>
#include <stdint.h>  // Soluciona el error de uint8_t
#include "freertos/FreeRTOS.h"
#include "esp_log.h"
#include "usb/usb_host.h"
#include "class_driver.h"

static const char *TAG = "CLASS_DRV";
QueueHandle_t midi_msg_queue = NULL;

typedef struct {
    usb_host_client_handle_t client_hdl;
    usb_device_handle_t dev_hdl;
} midi_context_t;

static midi_context_t ctx = {0};

// Callback para liberar la memoria del transfer
static void xfer_cb(usb_transfer_t *transfer) {
    usb_host_transfer_free(transfer);
}

// Función que envía la ráfaga de 12 bytes para Banco Z + Parche
static void send_midi_bank_z(uint8_t patch_id) {
    if (!ctx.dev_hdl) return;

    usb_transfer_t *xfer;
    // Solicitamos espacio para la ráfaga completa
    if (usb_host_transfer_alloc(64, 0, &xfer) == ESP_OK) {
        xfer->num_bytes = 12; // 3 mensajes MIDI USB (4 bytes cada uno)
        xfer->bEndpointAddress = 0x03; // Endpoint de salida MIDI de la Zoom
        xfer->device_handle = ctx.dev_hdl;
        xfer->callback = xfer_cb;

        // PAQUETE 1: Bank Select MSB (B0 00 00)
        xfer->data_buffer[0] = 0x0B; // CIN 0x0B (Control Change)
        xfer->data_buffer[1] = 0xB0; // Status CC Canal 1
        xfer->data_buffer[2] = 0x00; // CC #0 (MSB)
        xfer->data_buffer[3] = 0x00; // Valor 0

        // PAQUETE 2: Bank Select LSB (B0 20 19) -> ESTO ACTIVA EL BANCO Z
        xfer->data_buffer[4] = 0x0B; // CIN 0x0B
        xfer->data_buffer[5] = 0xB0; // Status CC Canal 1
        xfer->data_buffer[6] = 0x20; // CC #32 (LSB)
        xfer->data_buffer[7] = 0x19; // Valor 0x19 (25 en decimal = Banco Z)

        // PAQUETE 3: Program Change (C0 patch_id 00)
        xfer->data_buffer[8] = 0x0C;  // CIN 0x0C (Program Change)
        xfer->data_buffer[9] = 0xC0;  // Status PC Canal 1
        xfer->data_buffer[10] = patch_id; // ID del Parche (0-7)
        xfer->data_buffer[11] = 0x00; // Relleno

        usb_host_transfer_submit(xfer);
        ESP_LOGI(TAG, "Enviado: Banco Z, Parche %d", patch_id + 1);
    }
}

static void handle_client_event(const usb_host_client_event_msg_t *msg, void *arg) {
    if (msg->event == USB_HOST_CLIENT_EVENT_NEW_DEV) {
        usb_host_device_open(ctx.client_hdl, msg->new_dev.address, &ctx.dev_hdl);
        ESP_LOGI(TAG, "Zoom G6 Conectada - Reclamando interfaz MIDI");
        // La Zoom G6 usa la interfaz 4 para MIDI
        usb_host_interface_claim(ctx.client_hdl, ctx.dev_hdl, 4, 0);
    }
}

void class_driver_task(void *arg) {
    usb_host_client_config_t cfg = {
        .is_synchronous = false,
        .max_num_event_msg = 5,
        .async = { .client_event_callback = handle_client_event, .callback_arg = NULL }
    };
    usb_host_client_register(&cfg, &ctx.client_hdl);

    while (1) {
        usb_host_client_handle_events(ctx.client_hdl, 1);
        midi_msg_t m;
        if (xQueueReceive(midi_msg_queue, &m, 0) == pdTRUE) {
            // Usamos la nueva función con los 12 bytes
            send_midi_bank_z(m.data1);
        }
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}