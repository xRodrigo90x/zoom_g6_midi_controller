#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "led_strip.h"
#include "esp_timer.h"
#include "usb/usb_host.h"
#include "class_driver.h"

#define PIN_TIRA 39
#define NUM_LEDS 8
#define CANTIDAD 8
#define TIEMPO_STANDBY 15000000 

static const char *TAG = "MAIN_HW";
static const gpio_num_t pinesBotones[CANTIDAD] = { GPIO_NUM_13, GPIO_NUM_12, GPIO_NUM_11, GPIO_NUM_10, GPIO_NUM_9, GPIO_NUM_8, GPIO_NUM_7, GPIO_NUM_6 };
static led_strip_handle_t led_strip;
static int ultimoLedEncendido = -1;
static int64_t ultimaVezInteractuado = 0;
static bool enModoStandBy = false;

uint32_t color_wheel(uint8_t pos) {
    pos = 255 - pos;
    if (pos < 85) return ((uint32_t)(255 - pos * 3) << 16) | (pos * 3);
    if (pos < 170) { pos -= 85; return ((uint32_t)(pos * 3) << 8) | (255 - pos * 3); }
    pos -= 170;
    return ((uint32_t)(pos * 3) << 16) | ((uint32_t)(255 - pos * 3) << 8);
}

void efectoStandBy(void) {
    static uint8_t hue = 0;
    for (int j = 0; j < NUM_LEDS; j++) {
        uint32_t col = color_wheel((hue + j * 32) & 255);
        led_strip_set_pixel(led_strip, j, (col >> 16) & 0xFF, (col >> 8) & 0xFF, col & 0xFF);
    }
    led_strip_refresh(led_strip);
    hue++;
}

void secuencia_bloqueante_inicial(void) {
    // 1. Azul
    for (int i = 0; i < NUM_LEDS; i++) {
        led_strip_set_pixel(led_strip, i, 0, 0, 100); 
        led_strip_refresh(led_strip);
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    led_strip_clear(led_strip);
    led_strip_refresh(led_strip);

    // 2. Espera 5 segundos
    vTaskDelay(pdMS_TO_TICKS(5000));

    // 3. Arcoiris 4 veces
    for (int loops = 0; loops < 4; loops++) {
        for (int hue = 0; hue < 256; hue += 5) {
            for (int j = 0; j < NUM_LEDS; j++) {
                uint32_t col = color_wheel((hue + j * 32) & 255);
                led_strip_set_pixel(led_strip, j, (col >> 16) & 0xFF, (col >> 8) & 0xFF, col & 0xFF);
            }
            led_strip_refresh(led_strip);
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }

    // 4. Morado 4 veces
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < NUM_LEDS; j++) led_strip_set_pixel(led_strip, j, 150, 0, 200);
        led_strip_refresh(led_strip);
        vTaskDelay(pdMS_TO_TICKS(300));
        led_strip_clear(led_strip);
        led_strip_refresh(led_strip);
        vTaskDelay(pdMS_TO_TICKS(300));
    }
}

void hardware_control_task(void *arg) {
    led_strip_config_t strip_config = { .strip_gpio_num = PIN_TIRA, .max_leds = NUM_LEDS, .led_pixel_format = LED_PIXEL_FORMAT_GRB, .led_model = LED_MODEL_WS2812 };
    led_strip_rmt_config_t rmt_config = { .clk_src = RMT_CLK_SRC_DEFAULT, .resolution_hz = 10000000 };
    led_strip_new_rmt_device(&strip_config, &rmt_config, &led_strip);

    for (int i = 0; i < CANTIDAD; i++) {
        gpio_reset_pin(pinesBotones[i]);
        gpio_set_direction(pinesBotones[i], GPIO_MODE_INPUT);
        gpio_set_pull_mode(pinesBotones[i], GPIO_PULLUP_ONLY);
    }

    secuencia_bloqueante_inicial();
    ultimaVezInteractuado = esp_timer_get_time();

    while (1) {
        int64_t tiempoAhora = esp_timer_get_time();
        bool algunBotonPulsado = false;

        for (int i = 0; i < CANTIDAD; i++) {
            if (gpio_get_level(pinesBotones[i]) == 0) { 
                algunBotonPulsado = true;
                if (enModoStandBy) {
                    enModoStandBy = false;
                    led_strip_clear(led_strip);
                } else {
                    if (midi_msg_queue != NULL) {
                        midi_msg_t msg = { .data1 = (uint8_t)i }; 
                        xQueueSend(midi_msg_queue, &msg, 0);
                    }
                    led_strip_clear(led_strip);
                    if (i < 4) led_strip_set_pixel(led_strip, i, 0, 200, 0); // Verde para Z
                    else led_strip_set_pixel(led_strip, i, 0, 0, 200);      // Azul para AA
                    ultimoLedEncendido = i;
                }
                led_strip_refresh(led_strip);
                ultimaVezInteractuado = tiempoAhora;
                vTaskDelay(pdMS_TO_TICKS(250)); 
            }
        }

        if (!algunBotonPulsado && (tiempoAhora - ultimaVezInteractuado > TIEMPO_STANDBY)) {
            enModoStandBy = true;
            efectoStandBy(); 
        } else if (!enModoStandBy && ultimoLedEncendido != -1 && !algunBotonPulsado) {
            if (ultimoLedEncendido < 4) led_strip_set_pixel(led_strip, ultimoLedEncendido, 0, 200, 0);
            else led_strip_set_pixel(led_strip, ultimoLedEncendido, 0, 0, 200);
            led_strip_refresh(led_strip);
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

void usb_host_lib_task(void *arg) {
    const usb_host_config_t host_config = { .intr_flags = ESP_INTR_FLAG_LEVEL1 };
    usb_host_install(&host_config);
    xTaskNotifyGive((TaskHandle_t)arg);
    while (1) {
        usb_host_lib_handle_events(portMAX_DELAY, NULL);
    }
}

void app_main(void) {
    midi_msg_queue = xQueueCreate(10, sizeof(midi_msg_t));
    xTaskCreatePinnedToCore(hardware_control_task, "hw", 4096, NULL, 5, NULL, 1);
    TaskHandle_t main_hdl = xTaskGetCurrentTaskHandle();
    xTaskCreatePinnedToCore(usb_host_lib_task, "usb", 4096, (void *)main_hdl, 2, NULL, 0);
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    xTaskCreatePinnedToCore(class_driver_task, "midi", 4096, NULL, 3, NULL, 0);
}