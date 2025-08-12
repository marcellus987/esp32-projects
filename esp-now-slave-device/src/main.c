/*
Author: Marcellus Von Sacramento
Purpose: This source code is meant to be uploaded to Master ESP32 device.
*/


#include <esp_now.h>
#include <esp_wifi.h>
#include <esp_event.h>
#include <nvs_flash.h>
#include <stdio.h>
#include <esp_mac.h>
#include <string.h>
#include <driver/gpio.h>
#include <esp_sleep.h>

#include "../../misc-headers/esp-now-message-struct.h"


#define CHANNEL 6
#define SENSOR_PIN 25
#define RED_LED_PIN 26
#define GREEN_LED_PIN 27
#define LOW 0
#define HIGH 1
#define RELEASE_BUILD_SLEEP_TIME 43200000000 /* 43,200,000,000 == 12 hours. */
#define TEST_BUILD_SLEEP_TIME 5000000 /* 5000000 == 5 seconds. */

/* Callback function prototype. */
void onSent(const esp_now_send_info_t *peer_info, esp_now_send_status_t status);
void onReceived(const esp_now_recv_info_t *peer_info, const uint8_t *data_received, int data_len);


/* Setup. */

bool initWiFi() {
    esp_err_t err = nvs_flash_init();

    // Initialize NVS flash.
    // Recover in case nvs_flash_init() fails.
    if(err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }

    ESP_ERROR_CHECK(err); // Check if NVS init still fail.   
    ESP_ERROR_CHECK(esp_netif_init()); // Initialize Network Interface.
    ESP_ERROR_CHECK(esp_event_loop_create_default()); // Create default event loop for event handling.
    esp_netif_create_default_wifi_sta(); // Creates the wifi interface. Aborts if it fails. If need graceful handling, check if NULL.
    wifi_init_config_t config = WIFI_INIT_CONFIG_DEFAULT(); 
    ESP_ERROR_CHECK(esp_wifi_init(&config)); // Initialize wifi driver used by the interface.
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start()); // Start wifi in set mode.
    ESP_ERROR_CHECK(esp_wifi_set_channel(CHANNEL, WIFI_SECOND_CHAN_NONE));
    ESP_ERROR_CHECK(esp_wifi_disconnect()); // Disconnect to ensure d/'evice does not auto-connect to AP or other peer.
    
    return true;
}

bool initESPNOW() {
    ESP_ERROR_CHECK(esp_now_init());

    /* Register callback functions. */
    ESP_ERROR_CHECK(esp_now_register_send_cb(onSent));
    ESP_ERROR_CHECK(esp_now_register_recv_cb(onReceived));
    return true;
}

void pinConfig() { 
    gpio_config_t cfg;
    cfg.pin_bit_mask = (1ULL << GPIO_NUM_26 | 1ULL << GPIO_NUM_27); /* Pins used by RED_LED_PIN and GREEN_LED_PIN. */
    cfg.mode = GPIO_MODE_OUTPUT;

    gpio_config(&cfg);

    /* Reset state of LEDs. */
    gpio_set_level(RED_LED_PIN, LOW);
    gpio_set_level(GREEN_LED_PIN, LOW); 

    /* For beam sensor. */
    gpio_set_direction(SENSOR_PIN, GPIO_MODE_INPUT);
    gpio_set_pull_mode(SENSOR_PIN, GPIO_PULLUP_ONLY);
    gpio_pullup_en(SENSOR_PIN);
} // End of pinConfig().

void sleepConfig() {
    esp_sleep_enable_timer_wakeup(TEST_BUILD_SLEEP_TIME);
} // End of sleepConfig().




/* Send callback function. */

void onSent(const esp_now_send_info_t *peer_info, esp_now_send_status_t status) {
    if(status == ESP_NOW_SEND_SUCCESS) {
        printf("Data delivered successfully and Master received the data.\n\n");
    }
    else {
        printf("Send fail.\n\n");
    }
}

void onReceived(const esp_now_recv_info_t *peer_info, const uint8_t *data_received, int data_len) {
    printf("\nReceived from:\n");
    printf("Sender MAC address: " MACSTR "\n", MAC2STR(peer_info->src_addr));
    
    // This is sizeof(data_received) - 1 because null char is not included.
    printf("Message length: %d\n", strlen((const char*)data_received));  
    printf("Message: \n\t%s\n\n", data_received);
}

/* MISC Functions. */


/* Global variables. */
int message_count = 0;
uint8_t master[ESP_NOW_ETH_ALEN] = {0x88, 0x13, 0xbf, 0x0b, 0xe1, 0x50};

void setup() {
    
    // Init wifi and esp_now.
    if(initWiFi() && initESPNOW()) {
        printf("\n\nWifi and ESP_NOW Initialization succeeded!\n\n");
    }

    // Fill peer info.
   

    esp_now_peer_info_t peer = {
        .channel = 6,
        .ifidx = WIFI_IF_STA
    };

    // Copy address of peer to the struct.
    memcpy(peer.peer_addr, master, ESP_NOW_ETH_ALEN); 

    // Add peer to list of devices connected to this device.
    ESP_ERROR_CHECK(esp_now_add_peer(&peer));

    /* For debug: Construct first message. */
    char data[100];
    sprintf(data, "Message #%d: Hello from slave.", ++message_count);

    /* Note: Non-blocking. Don't expect call back to print immediately. */
    esp_now_send(master, (const uint8_t*) data, sizeof(data));

    // //printf("Dumping all GPIO configurations of Slave device...\n");
    // gpio_dump_io_configuration(stdout, SOC_GPIO_VALID_GPIO_MASK);

    /* Configure pin to be used. */
    pinConfig();
}




void app_main() {

    setup(); // Set up components to be used.
    int sensor_previous_state = 0;
    int sensor_current_state = 0;

    /* Loop. */
    while (true) {
        sensor_current_state = gpio_get_level(SENSOR_PIN); /* Read sensor state. */

        if(sensor_current_state != sensor_previous_state) {
            if(sensor_current_state) { /* HIGH. */
                gpio_set_level(GREEN_LED_PIN, HIGH);
                gpio_set_level(RED_LED_PIN, LOW);
            }
            else {
                gpio_set_level(GREEN_LED_PIN, LOW);
                gpio_set_level(RED_LED_PIN, HIGH);
            }

            // This may not be necessary if not printing to serial for debug.
            char state[50];

            // This can be included in the message instead.
            sprintf(state, sensor_current_state ? "Beam unbroken. No mail in the mailbox" : "Beam broken. There is mail in the mailbox");
            printf(state);
            printf("\n");

            esp_message msg;
             
            msg.sensor_status_flag = sensor_current_state; // The first byte of the message must alway contain the sensor state.
            snprintf(msg.message, sizeof(msg.message), "Message #%d: %s" , ++message_count, state);
            esp_now_send(master, (uint8_t *)&msg, sizeof(msg));
        
            // Save state.
            sensor_previous_state = sensor_current_state;
        }

        
    }
}