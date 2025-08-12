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
#include "../../misc-headers/esp-now-message-struct.h"

#define CHANNEL 6
#define RED_LED_PIN 25
#define GREEN_LED_PIN 26
#define HIGH 1
#define LOW 0


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
    ESP_ERROR_CHECK(esp_wifi_disconnect()); // Disconnect to ensure device does not auto-connect to AP or other peer.
    
    return true;
}

bool initESPNOW() {
    ESP_ERROR_CHECK(esp_now_init());

    /* Register callback functions. */
    ESP_ERROR_CHECK(esp_now_register_send_cb(onSent));
    ESP_ERROR_CHECK(esp_now_register_recv_cb(onReceived));
    return true;
}

void configPins() {
    gpio_config_t cfg;
    cfg.pin_bit_mask = (1ULL << 25 | 1ULL << 26); // PINS 25 & 26.
    cfg.mode = GPIO_MODE_OUTPUT;

    gpio_config(&cfg);

    /* Reset state of LEDs. */
    gpio_set_level(RED_LED_PIN, LOW);
    gpio_set_level(GREEN_LED_PIN, LOW);
}

/* Send callback function. */

void onSent(const esp_now_send_info_t *peer_info, esp_now_send_status_t status) {
    if(status == ESP_NOW_SEND_SUCCESS) {
        printf("\nData delivered successfully and peer received the data.\n");
    }
    else {
        printf("\nSend fail.\n");
    }
}

void onReceived(const esp_now_recv_info_t *peer_info, const uint8_t *data_received, int data_len) {
    bool sensor_status = data_received[0];

    printf("\nReceived from:\n");
    printf("Sender MAC address: " MACSTR "\n", MAC2STR(peer_info->src_addr));
    
    
    // This is sizeof(data_received) - 1 because null char is not included.
    printf("Message length: %d\n", strlen((const char*)data_received+1));  
    printf("Message: \n");
    printf("Beam status: %s.\n", sensor_status == HIGH ? "Unbroken" : "Broken");
    printf("Rest of the message: %s.\n\n", data_received + 1); // Skip the first byte which contains non-message info.

    if(sensor_status == HIGH) { /* Beam is not broken. No mail in the mailbox!. */
        gpio_set_level(RED_LED_PIN, LOW);
        gpio_set_level(GREEN_LED_PIN, HIGH);
    }
    else { /* Beam broken. There is mail in the mailbox. */
        gpio_set_level(RED_LED_PIN, HIGH);
        gpio_set_level(GREEN_LED_PIN, LOW);
    }
} // End of onReceived().

/* MISC Functions. */


/* Global variables. */
int message_count = 0;

uint8_t num = 1;
void app_main() {
   
    // Init wifi and esp_now.
    if(initWiFi() && initESPNOW()) {
        printf("\n\nWifi and ESP_NOW Initialization succeeded!\n\n");
    }

    configPins();

    // Fill peer info.
    uint8_t slave_mac[ESP_NOW_ETH_ALEN] = {0x88, 0x13, 0xbf, 0x0d, 0x82, 0xec}; 
    esp_now_peer_info_t peer = {
        .channel = 6,
        .ifidx = WIFI_IF_STA
    };

    // Copy address of peer to the struct.
    memcpy(peer.peer_addr, slave_mac, ESP_NOW_ETH_ALEN); 

    // Add peer to list of devices connected to this device.
    ESP_ERROR_CHECK(esp_now_add_peer(&peer));

    /* Construct first message. */
    char data[100];
    sprintf(data, "Message #%d: Hello from master.", ++message_count);

    esp_now_send(slave_mac, (const uint8_t*) data, sizeof(data));


/* Loop. */


    // while (true) {
       
    // }

}