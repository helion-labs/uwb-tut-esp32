#include "assert.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "nvs_flash.h"
#include <string.h>
#include <sys/param.h>
#include "protocol_examples_common.h"

#include "ble_core.h"
#include "mqtt_core.h"
#include "stnp_core.h"

int app_main() {
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    ESP_ERROR_CHECK(example_connect());

    initialize_sntp();

    //init_wifi();
    mqtt_init();
    ble_init();
    return (0);
}
