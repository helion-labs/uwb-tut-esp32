#pragma once
#include "cJSON.h"

/**********************************************************
*                                                 DEFINES *
**********************************************************/

/**********************************************************
*                                        GLOBAL FUNCTIONS *
**********************************************************/

cJSON* get_json_from_trace_packet(uint8_t* trace_packet);
cJSON* get_json_uwb_packet(uint8_t* uwb_packet);
