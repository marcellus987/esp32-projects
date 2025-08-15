/*
Author: Marcellus Von Sacramento
Purpose: This header file will contain various custom esp-message structure as needed.

*/

#ifndef ESP_NOW_MESSAGE_STRUCT
#define ESP_NOW_MESSAGE_STRUCT

typedef struct esp_message {
	uint8_t flag;
	uint8_t sensor_read_level;
	char message[101];
} esp_message;

typedef enum message_flag {
	NORMAL_MESSAGE, /* Used for normal communication. Sensor read level can be ignored. */
	SENSOR_READ, /* Used for sending sensor read level. Sensor read level must not be ignored if this flag is used. */
	ERROR_BROADCAST
} message_flag;

#endif /* ESP_NOW_MESSAGE_STRUCT */