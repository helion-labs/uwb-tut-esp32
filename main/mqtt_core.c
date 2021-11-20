/* MQTT Mutual Authentication Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "nvs_flash.h"
#include "protocol_examples_common.h"
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "lwip/dns.h"
#include "lwip/netdb.h"
#include "lwip/sockets.h"

#include "cJSON.h"

#include "esp_log.h"
#include "mqtt_client.h"

#include "trace_packet_helper.h"
#include "aws_clientcredential.h"
#include "global_defines.h"
#include "mqtt_core.h"

/*********************************************************
*                                                STATICS *
*********************************************************/
static const char* TAG = "MQTTS_CORE";

static mqtt_published_element_t pub_array[PUB_ARR_SIZE];
static SemaphoreHandle_t        mqtt_arr_sem;
static QueueHandle_t            sentQ;   // used to indicate a messageID was published
static QueueHandle_t            replayQ; // used to replay a message

static replay_message_t         replay_arr[PUB_ARR_SIZE];
static esp_mqtt_client_handle_t client;

/*********************************************************
*                                                EXTERNS *
*********************************************************/

extern const uint8_t client_cert_pem_start[] asm("_binary_client_crt_start");
extern const uint8_t client_cert_pem_end[] asm("_binary_client_crt_end");
extern const uint8_t client_key_pem_start[] asm("_binary_client_key_start");
extern const uint8_t client_key_pem_end[] asm("_binary_client_key_end");

static esp_err_t mqtt_event_handler(esp_mqtt_event_handle_t event) {
    esp_mqtt_client_handle_t client = event->client;
    int                      msg_id;
    // your_context_t *context = event->context;
    switch (event->event_id) {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");
        msg_id = esp_mqtt_client_subscribe(client, "/topic/qos0", 0);
        ESP_LOGI(TAG, "sent subscribe successful, msg_id=%d", msg_id);

        msg_id = esp_mqtt_client_subscribe(client, "/topic/qos1", 1);
        ESP_LOGI(TAG, "sent subscribe successful, msg_id=%d", msg_id);

        msg_id = esp_mqtt_client_unsubscribe(client, "/topic/qos1");
        ESP_LOGI(TAG, "sent unsubscribe successful, msg_id=%d", msg_id);
        break;
    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "MQTT_EVENT_DISCONNECTED");
        break;

    case MQTT_EVENT_SUBSCRIBED:
        ESP_LOGI(TAG, "MQTT_EVENT_SUBSCRIBED, msg_id=%d", event->msg_id);
        msg_id = esp_mqtt_client_publish(client, "/topic/qos0", "data", 0, 0, 0);
        ESP_LOGI(TAG, "sent publish successful, msg_id=%d", msg_id);
        break;
    case MQTT_EVENT_UNSUBSCRIBED:
        ESP_LOGI(TAG, "MQTT_EVENT_UNSUBSCRIBED, msg_id=%d", event->msg_id);
        break;
    case MQTT_EVENT_PUBLISHED:
        ESP_LOGI(TAG, "MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);
        replay_message_t test_mem;
        test_mem.message_id    = event->msg_id;
        test_mem.total_replays = 0;
        xQueueSend(sentQ, &test_mem, 1000);

        break;
    case MQTT_EVENT_DATA:
        ESP_LOGI(TAG, "MQTT_EVENT_DATA");
        printf("TOPIC=%.*s\r\n", event->topic_len, event->topic);
        printf("DATA=%.*s\r\n", event->data_len, event->data);
        break;
    case MQTT_EVENT_ERROR:
        ESP_LOGI(TAG, "MQTT_EVENT_ERROR");
        break;
    default:
        ESP_LOGI(TAG, "Other event id:%d", event->event_id);
        break;
    }
    return ESP_OK;
}

static void mqtt_app_start(void) {
    char mq_uri[100];
    memset(mq_uri, 0, sizeof(mq_uri));
    sprintf(mq_uri, "mqtts://%s:%d", clientcredentialMQTT_BROKER_ENDPOINT, clientcredentialMQTT_BROKER_PORT);
    const esp_mqtt_client_config_t mqtt_cfg = {
        .uri             = mq_uri,
        .client_cert_pem = (const char*)keyCLIENT_CERTIFICATE_PEM,
        .client_key_pem  = (const char*)keyCLIENT_PRIVATE_KEY_PEM,
        .event_handle    = mqtt_event_handler,
    };

    ESP_LOGI(TAG, "[APP] Free memory: %d bytes", esp_get_free_heap_size());
    client = esp_mqtt_client_init(&mqtt_cfg);
    esp_mqtt_client_start(client);
}

// returns NULL on fail
// returns a handle to the MQTT queue if sucessful
static QueueHandle_t enqueue_reg(int message_id) {
    if (pdTRUE != xSemaphoreTake(mqtt_arr_sem, MQTT_SEM_TICKS_TO_WAIT)) {
        ESP_LOGE(TAG, "Failed to obtain MQTT semaphor!");
        ASSERT(0);
    }

    int index = 0;
    for (; index < PUB_ARR_SIZE; index++) {
        if (pub_array[index].valid == false) {
            break;
        }
    }

    if (index == PUB_ARR_SIZE) {
        ESP_LOGE(TAG, "No room left in the pub arr!");
        xSemaphoreGive(mqtt_arr_sem);
        return NULL;
    }

    QueueHandle_t handle = xQueueCreate(1, sizeof(int));
    ASSERT(handle);
    pub_array[index].valid                  = true;
    pub_array[index].message_id             = message_id;
    pub_array[index].notification_q         = handle;
    pub_array[index].max_valid_age_in_ticks = xTaskGetTickCount() + 1000;

    xSemaphoreGive(mqtt_arr_sem);
    ESP_LOGI(TAG, "Enqueued message id %d, index %d, timeout %d",
             message_id, index, pub_array[index].max_valid_age_in_ticks);
    return handle;
}

static void replay_task(void* arg) {
    int              rxed;
    replay_message_t replay;

    while (true) {
        if (pdTRUE == xQueueReceive(replayQ, &replay, (25 / portTICK_PERIOD_MS))) {
            rxed = true;
        } else {
            rxed = false;
        }

        if (rxed) {
            ESP_LOGI(TAG, "RXed new message id = %d replays = %d", replay.message_id, replay.total_replays);
            if (replay.total_replays >= MAXIMUM_REPLAYS) {
                ESP_LOGE(TAG, "Message id %d reached maximum replayed!", replay.message_id);
                continue;
            }
        }

        if (rxed) {
            for (int i = 0; i < PUB_ARR_SIZE; i++) {
                if (false == replay_arr[i].valid) {
                    replay_arr[i].valid                  = true;
                    replay_arr[i].message_id             = replay.message_id;
                    replay_arr[i].replay_time_in_upticks = xTaskGetTickCount() + REPLAY_TIME;
                    replay_arr[i].total_replays          = replay.total_replays;
                    goto redo_loop;
                }
            }
            ESP_LOGE(TAG, "No place in replay buffer - dropped messaged id %d ", replay.message_id);
            continue;
        }

        // Send message back to the sentQ
        for (int i = 0; i < PUB_ARR_SIZE; i++) {
            if (replay_arr[i].valid) {
                if (replay_arr[i].replay_time_in_upticks < xTaskGetTickCount()) {
                    xQueueSend(sentQ, &replay_arr[i], portMAX_DELAY);
                    replay_arr[i].valid = false;
                    ESP_LOGI(TAG, "Replayed message %d", replay_arr[i].message_id);
                }
            }
        }
    redo_loop:
        continue;
    }
}

static void mqtt_manager(void* arg) {
    ESP_LOGI(TAG, "Starting mqtt manager!");
    replay_message_t message;
    bool             published;

    while (true) {
        if (pdTRUE == xQueueReceive(sentQ, &message, (75 / portTICK_PERIOD_MS))) {
            published = true;
        } else {
            published = false;
        }

        if (pdTRUE != xSemaphoreTake(mqtt_arr_sem, MQTT_SEM_TICKS_TO_WAIT)) {
            ESP_LOGE(TAG, "Failed to obtain MQTT semaphor!");
            ASSERT(0);
        }

        int index = 0;
        for (; index < PUB_ARR_SIZE; index++) {
            if (pub_array[index].valid) {
                // In the case that we got a MQTT_EVENT_PUBLISHED event
                if (published) {
                    if (pub_array[index].message_id == message.message_id) {
                        ASSERT(pub_array[index].notification_q);
                        int sent = MQTT_SUCCESS;
                        ESP_LOGI(TAG, "Message ID %d was acked!", pub_array[index].message_id);
                        xQueueSend(pub_array[index].notification_q, &sent, portMAX_DELAY);
                        pub_array[index].valid = false;
                        goto redo_loop;
                    }
                } else {
                    // in the case that we timed out waiting for MQTT_EVENT_PUBLISHED
                    if (pub_array[index].max_valid_age_in_ticks < xTaskGetTickCount()) {
                        ASSERT(pub_array[index].notification_q);
                        int sent = MQTT_ERROR;
                        ESP_LOGE(TAG, "Message ID %d timedout!", pub_array[index].message_id);
                        xQueueSend(pub_array[index].notification_q, &sent, portMAX_DELAY);
                        pub_array[index].valid = false;
                    }
                }
            }
        }

        // new pub, but nothing registered for it
        if (published && (index == PUB_ARR_SIZE)) {
            message.total_replays++;
            ESP_LOGI(TAG, "Sent a message to the replay buffer");
            xQueueSend(replayQ, &message, portMAX_DELAY);
        }
    redo_loop:
        xSemaphoreGive(mqtt_arr_sem);
    }
}

bool mqtt_publish(char* str) {
    int           msg_id = esp_mqtt_client_publish(client, "/topic/incidents", str, strlen(str), 1, 0);
    QueueHandle_t q      = enqueue_reg(msg_id);
    int           status = MQTT_ERROR;
    if (q) {
        xQueueReceive(q, &status, portMAX_DELAY);
        ESP_LOGI(TAG, "GOT ack/NACK, status == %d", status);
    } else {
        ESP_LOGE(TAG, "failed to enquue");
    }
    if (q) {
        vQueueDelete(q);
    }

    return status;
}

void mqtt_init(void) {
    mqtt_arr_sem = xSemaphoreCreateMutex();
    ASSERT(mqtt_arr_sem);

    sentQ   = xQueueCreate(DEPTTH_MQTT_Q, sizeof(replay_message_t));
    replayQ = xQueueCreate(DEPTTH_MQTT_Q, sizeof(replay_message_t));

    ASSERT(sentQ);
    ASSERT(replayQ);

    TaskHandle_t xHandle = NULL;

    // Create the mqtt task, storing the handle.
    BaseType_t xReturned = xTaskCreate(
        mqtt_manager,         // Function that implements the task.
        "mqtt_manager",       // Text name for the task.
        MQTT_STACK_SIZE,      // Stack size in words, not bytes.
        NULL,                 // Parameter passed into the task.
        MQTT_THREAD_PRIORITY, // Priority at which the task is created.
        &xHandle);            // Used to pass out the created task's handle.

    if (xReturned != pdPASS) {
        ASSERT(0);
        ESP_LOGE(TAG, "Failed to create thread!");
    }

    xReturned = xTaskCreate(
        replay_task,          // Function that implements the task.
        "replay_task",        // Text name for the task.
        MQTT_STACK_SIZE,      // Stack size in words, not bytes.
        NULL,                 // Parameter passed into the task.
        MQTT_THREAD_PRIORITY, // Priority at which the task is created.
        &xHandle);            // Used to pass out the created task's handle.

    if (xReturned != pdPASS) {
        ASSERT(0);
        ESP_LOGE(TAG, "Failed to create thread!");
    }

    mqtt_app_start();
#if 0
    for(int i = 0; i < 8; i++){
      test_arr[i].type   = PAGE_NORMAL_ENTRY_MAGIC;
      test_arr[i].RSSI   = i;
      test_arr[i].counts = i*2;
      
      for(int j=0; j < 20; j++){
        test_arr[i].manufactuers_data[j] = i*20+j;
      }
    }
    //test_arr[1].type = 3;

    while(true){ 
      extern cJSON * get_json_from_trace_packet(uint8_t *trace_packet);
      cJSON *root = get_json_from_trace_packet(test_arr);
      char * str = NULL;
      if (root){
        str = cJSON_Print(root);
        ESP_LOGI(TAG, "JSON STRING = %s", str);
      }
     
      int x = esp_mqtt_client_publish(client, "/topic/helloworld", str, strlen(str), 1, 0);
      free(str);
      ESP_LOGI(TAG, "SNET, msg_id=%d", x);
     
      QueueHandle_t q = enqueue_reg(x);
      if(q){
        int x = 0;
        xQueueReceive(q, &x, 3034);
        ESP_LOGI(TAG, "GOT ack/NACK, status == %d", x);
      } else {
        ESP_LOGE(TAG, "failed to enquue");
      }
      if (q){
        ESP_LOGI(TAG, "Deleting Q");
        vQueueDelete(q);
      }
      ESP_LOGI(TAG, "Free %d", heap_caps_get_free_size(MALLOC_CAP_8BIT));
      cJSON_Delete(root);
  }
#endif
}

// returns 1 on error
// zero on sucess
int send_packet_to_aws(uint8_t* packet) {
    if (!packet) {
        ESP_LOGE(TAG, "Packet was null!");
        ASSERT(0);
    }

    cJSON* json_packet = get_json_uwb_packet(packet);
    if (!json_packet) {
        ESP_LOGE(TAG, "Failed to serialize json data!");
        return MQTT_ERROR;
    }

    char* str = NULL;
    str       = cJSON_Print(json_packet);
    if (!str) {
        ESP_LOGE(TAG, "Failed to get string");
        cJSON_Delete(json_packet);
        return MQTT_ERROR;
    }

    ESP_LOGI(TAG, "JSON STRING = %s", str);
    int message_id = esp_mqtt_client_publish(client, "/topic/cat_location", str, strlen(str), 1, 0);
    ESP_LOGI(TAG, "SENT, msg_id=%d", message_id);
    free(str);

    QueueHandle_t q      = enqueue_reg(message_id);
    int           status = MQTT_ERROR;
    if (q) {
        xQueueReceive(q, &status, portMAX_DELAY);
        ESP_LOGI(TAG, "GOT ack/NACK, status == %d", status);
    } else {
        ESP_LOGE(TAG, "failed to enquue");
    }
    if (q) {
        ESP_LOGI(TAG, "Deleting Q");
        vQueueDelete(q);
    }
    cJSON_Delete(json_packet);

    return status;
}
