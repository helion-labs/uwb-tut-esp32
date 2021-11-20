#include "esp_log.h"
#include "esp_system.h"
#include "string.h"
#include <stdio.h>

#include "cJSON.h"
#include "flash_core.h"
#include "uwb_core.h"
#include "global_defines.h"


/**********************************************************
*                                                 STATICS *
**********************************************************/
static const char* TAG = "TRACE_HELPER";

/**********************************************************
*                                          IMPLEMENTATION *
**********************************************************/

// encode a manufacturing data into a string
static void encode_man_data(uint8_t* data, char* str) {
    if (!data || !str) {
        ESP_LOGE(TAG, "null ARGS!");
        //ASSERT(0);
    }

    for (int i = 0; i < BLE_MANUFACTURERS_DATA_LEN; i++) {

        //sprintf adds a null termination
        if (i == BLE_MANUFACTURERS_DATA_LEN - 1) {
            snprintf(str, 4, "%02X", *data); // don't add an extra space to the last entry
        } else {
            snprintf(str, 4, "%02X ", *data);
        }
        str = str + 3; // go forward and replace the last null termination
        data++;
    }
}

// The edge devices upload 8 trace packets at a time,
// sometimes, it the edge device might upload a chunk (n packets)
// that are only partially filled - in this case, we will
// only send the packets to AWS that are valid, the rest of the
// data would just be 0xFF
//
// It's also possible (for the first packet in a page) that
// the header will be header packet, not a trace packet,
// we will not send this to AWS either
cJSON* get_json_from_trace_packet(uint8_t* trace_packet) {
    if (!trace_packet) {
        ESP_LOGE(TAG, "Trace packet was null!");
        ASSERT(0);
    }

    flash_packet_t* test = (flash_packet_t*)trace_packet;
    char            adv_data_str[BLE_MANUFACTURERS_DATA_LEN * 4];
    cJSON*          root = cJSON_CreateArray();
    if (!root) {
        ESP_LOGE(TAG, "node was null!");
        ASSERT(0);
    }

    for (int i = 0; i < FLASH_PACKETS_PER_CHUNK; i++) {
        if (i > 0 && test->type != PAGE_NORMAL_ENTRY_MAGIC) {
            ESP_LOGI(TAG, "Had a partial packet, stopping!");
            break;
        }

        if (test->type == PAGE_NORMAL_ENTRY_MAGIC) {
            cJSON* node = cJSON_CreateObject();
            if (!node) {
                ESP_LOGE(TAG, "node was null!");
                ASSERT(0);
            }

            memset(adv_data_str, 0, sizeof(adv_data_str));
            encode_man_data(test->manufactuers_data, adv_data_str);
            cJSON_AddStringToObject(node, "adv_code", adv_data_str);
            if(test->RSSI){
              cJSON_AddNumberToObject(node, "RSSI",        test->RSSI);
              cJSON_AddNumberToObject(node, "counts",      test->counts);
            } else {
              cJSON_AddNumberToObject(node, "distance_cm", test->specifics.distance_uwb);
            }
            ESP_LOGI(TAG, "Adding sub node RSSI = %d Counts =%d", test->RSSI, test->counts);
            cJSON_AddItemToObject(root, "root", node);
        }
        test++;
    }
    return root;
}


// Parses a simple UWB packet
cJSON* get_json_uwb_packet(uint8_t* uwb_packet) {
    if (!uwb_packet) {
        ESP_LOGE(TAG, "UWB packet was null!");
        ASSERT(0);
    }

    uwb_packet_t* test = (uwb_packet_t*)uwb_packet;
    cJSON*               root = cJSON_CreateObject();
    if (!root) {
        ESP_LOGE(TAG, "node was null!");
        ASSERT(0);
    }

    cJSON_AddNumberToObject(root, "Distance", test->distance_uwb);
    cJSON_AddNumberToObject(root, "Time",     test->time);
    return root;
}
