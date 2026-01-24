#ifndef CLASS_DRIVER_H
#define CLASS_DRIVER_H

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

typedef struct {
    uint8_t status;
    uint8_t data1;
    uint8_t data2;
} midi_msg_t;

extern QueueHandle_t midi_msg_queue;

void class_driver_task(void *arg);
void class_driver_client_deregister(void);

#endif