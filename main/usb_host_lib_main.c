/*
 * SPDX-FileCopyrightText: 2021-2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/event_groups.h"
#include "esp_log.h"
#include "esp_intr_alloc.h"
#include "usb/usb_host.h"
#include "driver/gpio.h"

#define HOST_LIB_TASK_PRIORITY    2
#define CLASS_TASK_PRIORITY       3
#define APP_QUIT_PIN              CONFIG_APP_QUIT_PIN

extern void class_driver_task(void *arg);
extern void class_driver_client_deregister(void);

static const char *TAG = "USB_MAIN";
QueueHandle_t app_event_queue = NULL;

typedef enum {
    APP_EVENT = 0,
} app_event_group_t;

typedef struct {
    app_event_group_t event_group;
} app_event_queue_t;

/**
 * @brief Callback del botón BOOT para salir de la aplicación
 */
static void gpio_cb(void *arg)
{
    const app_event_queue_t evt_queue = {
        .event_group = APP_EVENT,
    };
    BaseType_t xTaskWoken = pdFALSE;
    if (app_event_queue) {
        xQueueSendFromISR(app_event_queue, &evt_queue, &xTaskWoken);
    }
    if (xTaskWoken == pdTRUE) {
        portYIELD_FROM_ISR();
    }
}

/**
 * @brief Tarea principal de la librería USB Host
 */
static void usb_host_lib_task(void *arg)
{
    ESP_LOGI(TAG, "Instalando USB Host Library");
    usb_host_config_t host_config = {
        .skip_phy_setup = false,
        .intr_flags = ESP_INTR_FLAG_LEVEL1,
    };
    ESP_ERROR_CHECK(usb_host_install(&host_config));

    // Notificar a app_main que la librería está instalada
    xTaskNotifyGive(arg);

    bool has_clients = true;
    bool has_devices = false;
    while (has_clients) {
        uint32_t event_flags;
        ESP_ERROR_CHECK(usb_host_lib_handle_events(portMAX_DELAY, &event_flags));
        if (event_flags & USB_HOST_LIB_EVENT_FLAGS_NO_CLIENTS) {
            if (ESP_OK == usb_host_device_free_all()) {
                has_clients = false;
            } else {
                has_devices = true;
            }
        }
        if (has_devices && event_flags & USB_HOST_LIB_EVENT_FLAGS_ALL_FREE) {
            has_clients = false;
        }
    }

    ESP_LOGI(TAG, "Desinstalando USB Host Library");
    ESP_ERROR_CHECK(usb_host_uninstall());
    vTaskSuspend(NULL);
}

void app_main(void)
{
    ESP_LOGI(TAG, "Iniciando ejemplo USB MIDI para Zoom G6");

    // Configuración del botón para cerrar la app
    const gpio_config_t input_pin = {
        .pin_bit_mask = BIT64(APP_QUIT_PIN),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .intr_type = GPIO_INTR_NEGEDGE,
    };
    ESP_ERROR_CHECK(gpio_config(&input_pin));
    ESP_ERROR_CHECK(gpio_install_isr_service(ESP_INTR_FLAG_LEVEL1));
    ESP_ERROR_CHECK(gpio_isr_handler_add(APP_QUIT_PIN, gpio_cb, NULL));

    app_event_queue = xQueueCreate(10, sizeof(app_event_queue_t));
    app_event_queue_t evt_queue;

    TaskHandle_t host_lib_task_hdl, class_driver_task_hdl;

    // Crear tarea de la librería
    xTaskCreatePinnedToCore(usb_host_lib_task, "usb_host", 4096, xTaskGetCurrentTaskHandle(), HOST_LIB_TASK_PRIORITY, &host_lib_task_hdl, 0);

    // Esperar a que la librería se instale
    ulTaskNotifyTake(false, 1000);

    // Crear tarea del driver de clase (donde está tu código MIDI)
    xTaskCreatePinnedToCore(class_driver_task, "class", 4096, NULL, CLASS_TASK_PRIORITY, &class_driver_task_hdl, 0);

    // Bucle principal: Esperar evento de salida
    while (1) {
        if (xQueueReceive(app_event_queue, &evt_queue, portMAX_DELAY)) {
            if (APP_EVENT == evt_queue.event_group) {
                ESP_LOGW(TAG, "Saliendo del programa...");
                break;
            }
        }
    }

    // Limpieza al salir
    class_driver_client_deregister();
    vTaskDelay(20);
    vTaskDelete(class_driver_task_hdl);
    vTaskDelete(host_lib_task_hdl);
    gpio_isr_handler_remove(APP_QUIT_PIN);
    vQueueDelete(app_event_queue);
    
    ESP_LOGI(TAG, "Programa terminado");
}