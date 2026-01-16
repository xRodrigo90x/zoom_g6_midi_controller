/*
 * SPDX-FileCopyrightText: 2021-2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */

#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "usb/usb_host.h"

#define CLIENT_NUM_EVENT_MSG        5

typedef enum {
    ACTION_OPEN_DEV         = (1 << 0),
    ACTION_GET_DEV_INFO     = (1 << 1),
    ACTION_GET_DEV_DESC     = (1 << 2),
    ACTION_GET_CONFIG_DESC  = (1 << 3),
    ACTION_GET_STR_DESC     = (1 << 4),
    ACTION_CLOSE_DEV        = (1 << 5),
} action_t;

#define DEV_MAX_COUNT           128

typedef struct {
    usb_host_client_handle_t client_hdl;
    uint8_t dev_addr;
    usb_device_handle_t dev_hdl;
    action_t actions;
} usb_device_t;

typedef struct {
    struct {
        union {
            struct {
                uint8_t unhandled_devices: 1;
                uint8_t shutdown: 1;
                uint8_t reserved6: 6;
            };
            uint8_t val;
        } flags;
        usb_device_t device[DEV_MAX_COUNT];
    } mux_protected;

    struct {
        usb_host_client_handle_t client_hdl;
        SemaphoreHandle_t mux_lock;
    } constant;
} class_driver_t;

static const char *TAG = "CLASS";
static class_driver_t *s_driver_obj;

static void midi_transfer_cb(usb_transfer_t *transfer)
{
    if (transfer->status == USB_TRANSFER_STATUS_COMPLETED) {
        ESP_LOGI("MIDI_CB", "¡Transferencia completada!");
    } else {
        ESP_LOGW("MIDI_CB", "Estado de transferencia: %d", transfer->status);
    }
    usb_host_transfer_free(transfer);
}

static void client_event_cb(const usb_host_client_event_msg_t *event_msg, void *arg)
{
    class_driver_t *driver_obj = (class_driver_t *)arg;
    switch (event_msg->event) {
    case USB_HOST_CLIENT_EVENT_NEW_DEV:
        xSemaphoreTake(driver_obj->constant.mux_lock, portMAX_DELAY);
        driver_obj->mux_protected.device[event_msg->new_dev.address].dev_addr = event_msg->new_dev.address;
        driver_obj->mux_protected.device[event_msg->new_dev.address].dev_hdl = NULL;
        driver_obj->mux_protected.device[event_msg->new_dev.address].actions |= ACTION_OPEN_DEV;
        driver_obj->mux_protected.flags.unhandled_devices = 1;
        xSemaphoreGive(driver_obj->constant.mux_lock);
        break;
    case USB_HOST_CLIENT_EVENT_DEV_GONE:
        xSemaphoreTake(driver_obj->constant.mux_lock, portMAX_DELAY);
        for (uint8_t i = 0; i < DEV_MAX_COUNT; i++) {
            if (driver_obj->mux_protected.device[i].dev_hdl == event_msg->dev_gone.dev_hdl) {
                driver_obj->mux_protected.device[i].actions = ACTION_CLOSE_DEV;
                driver_obj->mux_protected.flags.unhandled_devices = 1;
            }
        }
        xSemaphoreGive(driver_obj->constant.mux_lock);
        break;
    default:
        break;
    }
}

static void action_open_dev(usb_device_t *device_obj)
{
    assert(device_obj->dev_addr != 0);
    ESP_LOGI(TAG, "Opening device at address %d", device_obj->dev_addr);
    ESP_ERROR_CHECK(usb_host_device_open(device_obj->client_hdl, device_obj->dev_addr, &device_obj->dev_hdl));
    device_obj->actions |= ACTION_GET_DEV_INFO;
}

static void action_get_info(usb_device_t *device_obj)
{
    assert(device_obj->dev_hdl != NULL);
    usb_device_info_t dev_info;
    ESP_ERROR_CHECK(usb_host_device_info(device_obj->dev_hdl, &dev_info));
    device_obj->actions |= ACTION_GET_DEV_DESC;
}

static void action_get_dev_desc(usb_device_t *device_obj)
{
    assert(device_obj->dev_hdl != NULL);
    const usb_device_desc_t *dev_desc;
    ESP_ERROR_CHECK(usb_host_get_device_descriptor(device_obj->dev_hdl, &dev_desc));
    usb_print_device_descriptor(dev_desc);
    device_obj->actions |= ACTION_GET_CONFIG_DESC;
}

static void action_get_config_desc(usb_device_t *device_obj)
{
    assert(device_obj->dev_hdl != NULL);
    const usb_config_desc_t *config_desc;
    ESP_ERROR_CHECK(usb_host_get_active_config_descriptor(device_obj->dev_hdl, &config_desc));
    usb_print_config_descriptor(config_desc, NULL);
    device_obj->actions |= ACTION_GET_STR_DESC;
}

static void action_get_str_desc(usb_device_t *device_obj)
{
    assert(device_obj->dev_hdl != NULL);
    usb_device_info_t dev_info;
    ESP_ERROR_CHECK(usb_host_device_info(device_obj->dev_hdl, &dev_info));

    if (dev_info.str_desc_product) {
        ESP_LOGI(TAG, "Enviando secuencia MIDI exacta: Bank Z, Patch 1");
        
        // 1. Reclamar interfaz
        ESP_ERROR_CHECK(usb_host_interface_claim(device_obj->client_hdl, device_obj->dev_hdl, 4, 0));

        // 2. Reservar memoria para 12 bytes (3 mensajes USB MIDI)
        usb_transfer_t *transfer;
        ESP_ERROR_CHECK(usb_host_transfer_alloc(64, 0, &transfer));

        transfer->num_bytes = 12;
        transfer->bEndpointAddress = 0x03; 
        transfer->device_handle = device_obj->dev_hdl;
        transfer->callback = midi_transfer_cb;
        transfer->context = NULL;

        // --- PAQUETE 1: Bank MSB (B0 00 00) ---
        transfer->data_buffer[0] = 0x0B;   // CIN 0x0B (Control Change)
        transfer->data_buffer[1] = 0xB0;   // Status
        transfer->data_buffer[2] = 0x00;   // CC 0
        transfer->data_buffer[3] = 0x00;   // Valor 0

        // --- PAQUETE 2: Bank LSB (B0 20 19) -> ESTO ACTIVA EL BANCO Z ---
        transfer->data_buffer[4] = 0x0B;   // CIN 0x0B
        transfer->data_buffer[5] = 0xB0;   // Status
        transfer->data_buffer[6] = 0x20;   // CC 32 (LSB)
        transfer->data_buffer[7] = 0x19;   // Valor 0x19 (Banco Z)

        // --- PAQUETE 3: Program Change (C0 00) -> PATCH 1 ---
        transfer->data_buffer[8] = 0x0C;   // CIN 0x0C (Program Change)
        transfer->data_buffer[9] = 0xC0;   // Status
        transfer->data_buffer[10] = 0x00;  // Patch 1
        transfer->data_buffer[11] = 0x00;

        ESP_LOGI(TAG, "Enviando Bank Z, Patch 1...");
        ESP_ERROR_CHECK(usb_host_transfer_submit(transfer));
    }
}

static void action_close_dev(usb_device_t *device_obj)
{
    ESP_ERROR_CHECK(usb_host_device_close(device_obj->client_hdl, device_obj->dev_hdl));
    device_obj->dev_hdl = NULL;
    device_obj->dev_addr = 0;
}

static void class_driver_device_handle(usb_device_t *device_obj)
{
    uint8_t actions = device_obj->actions;
    device_obj->actions = 0;

    while (actions) {
        if (actions & ACTION_OPEN_DEV) action_open_dev(device_obj);
        if (actions & ACTION_GET_DEV_INFO) action_get_info(device_obj);
        if (actions & ACTION_GET_DEV_DESC) action_get_dev_desc(device_obj);
        if (actions & ACTION_GET_CONFIG_DESC) action_get_config_desc(device_obj);
        if (actions & ACTION_GET_STR_DESC) action_get_str_desc(device_obj);
        if (actions & ACTION_CLOSE_DEV) action_close_dev(device_obj);

        actions = device_obj->actions;
        device_obj->actions = 0;
    }
}

void class_driver_task(void *arg)
{
    class_driver_t driver_obj = {0};
    usb_host_client_handle_t class_driver_client_hdl = NULL;
    SemaphoreHandle_t mux_lock = xSemaphoreCreateMutex();
    
    usb_host_client_config_t client_config = {
        .is_synchronous = false,
        .max_num_event_msg = CLIENT_NUM_EVENT_MSG,
        .async = {
            .client_event_callback = client_event_cb,
            .callback_arg = (void *) &driver_obj,
        },
    };
    ESP_ERROR_CHECK(usb_host_client_register(&client_config, &class_driver_client_hdl));
    driver_obj.constant.mux_lock = mux_lock;
    driver_obj.constant.client_hdl = class_driver_client_hdl;
    
    for (uint8_t i = 0; i < DEV_MAX_COUNT; i++) {
        driver_obj.mux_protected.device[i].client_hdl = class_driver_client_hdl;
    }
    s_driver_obj = &driver_obj;

    while (1) {
        if (driver_obj.mux_protected.flags.unhandled_devices) {
            xSemaphoreTake(driver_obj.constant.mux_lock, portMAX_DELAY);
            for (uint8_t i = 0; i < DEV_MAX_COUNT; i++) {
                if (driver_obj.mux_protected.device[i].actions) {
                    class_driver_device_handle(&driver_obj.mux_protected.device[i]);
                }
            }
            driver_obj.mux_protected.flags.unhandled_devices = 0;
            xSemaphoreGive(driver_obj.constant.mux_lock);
        } else {
            if (driver_obj.mux_protected.flags.shutdown == 0) {
                usb_host_client_handle_events(class_driver_client_hdl, portMAX_DELAY);
            } else {
                break;
            }
        }
    }
    ESP_ERROR_CHECK(usb_host_client_deregister(class_driver_client_hdl));
    vSemaphoreDelete(mux_lock);
    vTaskSuspend(NULL);
}

void class_driver_client_deregister(void)
{
    xSemaphoreTake(s_driver_obj->constant.mux_lock, portMAX_DELAY);
    for (uint8_t i = 0; i < DEV_MAX_COUNT; i++) {
        if (s_driver_obj->mux_protected.device[i].dev_hdl != NULL) {
            s_driver_obj->mux_protected.device[i].actions |= ACTION_CLOSE_DEV;
            s_driver_obj->mux_protected.flags.unhandled_devices = 1;
        }
    }
    s_driver_obj->mux_protected.flags.shutdown = 1;
    xSemaphoreGive(s_driver_obj->constant.mux_lock);
    ESP_ERROR_CHECK(usb_host_client_unblock(s_driver_obj->constant.client_hdl));
}