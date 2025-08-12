/*
Author: Marcellus Von Sacramento
Purpose: This header file will contain various custom esp-message structure as needed.

*/

#ifndef ESP_NOW_MESSAGE_STRUCT
#define ESP_NOW_MESSAGE_STRUCT

typedef struct esp_message {
	uint8_t sensor_status_flag;
	char message[101];
} esp_message;

#endif /* ESP_NOW_MESSAGE_STRUCT */