#include <stdlib.h>
#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
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

static void xfer_cb(usb_transfer_t *transfer) {
    // Liberamos la transferencia una vez completada
    usb_host_transfer_free(transfer);
}

static void send_midi_zoom_g6(uint8_t button_index) {
    if (!ctx.dev_hdl) {
        ESP_LOGW(TAG, "Zoom G6 no detectada. No se puede enviar MIDI.");
        return;
    }

    usb_transfer_t *xfer;
    // Reservamos espacio para 12 bytes (3 paquetes MIDI USB de 4 bytes cada uno)
    if (usb_host_transfer_alloc(64, 0, &xfer) == ESP_OK) {
        xfer->num_bytes = 12; 
        xfer->bEndpointAddress = 0x03; // Endpoint MIDI Out de la Zoom G6
        xfer->device_handle = ctx.dev_hdl;
        xfer->callback = xfer_cb;

        // LÓGICA DE BANCOS ZOOM G6:
        // Botones 0-3 -> Banco Z (LSB 0x19), Parches 0-3
        // Botones 4-7 -> Banco AA (LSB 0x1A), Parches 0-3
        
        uint8_t lsb_bank = (button_index < 4) ? 0x19 : 0x1A;
        uint8_t patch_id = (button_index < 4) ? button_index : (button_index - 4);

        // Mensaje 1: Bank Select MSB (Control Change 0, Valor 0)
        xfer->data_buffer[0] = 0x0B; // MIDI USB Cine-byte (Control Change)
        xfer->data_buffer[1] = 0xB0; // Status: CC Canal 1
        xfer->data_buffer[2] = 0x00; // CC#0 (Bank Select MSB)
        xfer->data_buffer[3] = 0x00; // Valor 0

        // Mensaje 2: Bank Select LSB (Control Change 32, Valor 0x19 o 0x1A)
        xfer->data_buffer[4] = 0x0B; 
        xfer->data_buffer[5] = 0xB0; 
        xfer->data_buffer[6] = 0x20; // CC#32 (Bank Select LSB)
        xfer->data_buffer[7] = lsb_bank;

        // Mensaje 3: Program Change (El parche dentro del banco)
        xfer->data_buffer[8] = 0x0C; // MIDI USB Cine-byte (Program Change)
        xfer->data_buffer[9] = 0xC0; // Status: PC Canal 1
        xfer->data_buffer[10] = patch_id; 
        xfer->data_buffer[11] = 0x00;

        esp_err_t err = usb_host_transfer_submit(xfer);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Error al enviar: 0x%x", err);
            usb_host_transfer_free(xfer);
        } else {
            ESP_LOGI(TAG, "Enviado: Boton %d -> Banco %s Parche %d", 
                     button_index, (lsb_bank == 0x19 ? "Z" : "AA"), patch_id + 1);
        }
    }
}

static void handle_client_event(const usb_host_client_event_msg_t *msg, void *arg) {
    if (msg->event == USB_HOST_CLIENT_EVENT_NEW_DEV) {
        if (usb_host_device_open(ctx.client_hdl, msg->new_dev.address, &ctx.dev_hdl) == ESP_OK) {
            // Reclamamos la interfaz MIDI (usualmente la 4 en Zoom G6)
            usb_host_interface_claim(ctx.client_hdl, ctx.dev_hdl, 4, 0);
            ESP_LOGI(TAG, "--- ZOOM G6 CONECTADA ---");
        }
    } else if (msg->event == USB_HOST_CLIENT_EVENT_DEV_GONE) {
        ctx.dev_hdl = NULL;
        ESP_LOGW(TAG, "--- ZOOM G6 DESCONECTADA ---");
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
        // Manejamos eventos USB con timeout para no bloquear
        usb_host_client_handle_events(ctx.client_hdl, pdMS_TO_TICKS(10)); 
        
        midi_msg_t m;
        // Si hay un mensaje en la cola, lo enviamos a la pedalera
        if (xQueueReceive(midi_msg_queue, &m, 0) == pdTRUE) {
            send_midi_zoom_g6(m.data1);
        }
        
        // Pequeño respiro para el Watchdog
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}