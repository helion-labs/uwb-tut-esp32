#pragma once
#include <stdint.h>

/**********************************************************
*                                                 DEFINES *
**********************************************************/
#define UWB_PACKET_SIZE (8)


/**********************************************************
*                                                   TYPES *
**********************************************************/
// keep this in sync with what the ESP32
typedef struct {
    uint32_t distance_uwb;
    uint32_t time;
}__attribute__((packed)) uwb_packet_t;
_Static_assert(sizeof(uwb_packet_t) == UWB_PACKET_SIZE, "UWB packet is not 8 bytes long!");

/**********************************************************
*                                                 GLOBALS *
**********************************************************/
int uwb_scan_adv();
