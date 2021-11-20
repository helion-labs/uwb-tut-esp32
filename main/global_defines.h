#pragma once

/**********************************************************
*                      DEFINES
**********************************************************/
#define RTOS_DONT_WAIT (0)

#define SSID

/**********************************************************
*                       HELPERS 
**********************************************************/
#define ASSERT(x)                                                       \
    do {                                                                \
        if (!(x)) {                                                     \
            ESP_LOGE(TAG, "ASSERT! error %s %u\n", __FILE__, __LINE__); \
            for (;;) {                                                  \
                esp_restart();                                          \
            }                                                           \
        }                                                               \
    } while (0)

#define TRUE  (1)
#define FALSE (0)
