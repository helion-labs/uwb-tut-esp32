#pragma once

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/queue.h"

#include "freertos/FreeRTOS.h"

/**********************************************************
*                                                 GLOBALS *
**********************************************************/

/**********************************************************
*                                                 DEFINES *
**********************************************************/
#define PUB_ARR_SIZE           (16)
#define MQTT_STACK_SIZE        (2048)
#define MQTT_THREAD_PRIORITY   (5)
#define DEPTTH_MQTT_Q          (5)
#define MQTT_SEM_TICKS_TO_WAIT (1000 / portTICK_PERIOD_MS)
#define REPLAY_TIME            (500 / portTICK_PERIOD_MS)

#define MQTT_SUCCESS    (0)
#define MQTT_ERROR      (1)
#define MAXIMUM_REPLAYS (2)

/*********************************************************
*                                               TYPEDEFS *
**********************************************************/
typedef struct {
    bool          valid;
    uint32_t      max_valid_age_in_ticks; // maximum age in tick (uptime) that we will wait for ACK
    int           message_id;
    QueueHandle_t notification_q; // When pending for a response,
                                  // we create a queue and block on it
                                  // once we timeout/get an ack, we send
                                  // a message through this queue to the thread
                                  // that intitially registered for a response
} mqtt_published_element_t;

typedef struct {
    bool     valid;
    int      message_id;
    uint32_t replay_time_in_upticks;
    int      total_replays;
} replay_message_t;

/**********************************************************
*                                        GLOBAL FUNCTIONS *
**********************************************************/
void mqtt_init(void);
int  send_packet_to_aws(uint8_t* packet);
