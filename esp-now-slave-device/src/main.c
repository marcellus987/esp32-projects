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


#define TEST_CHANNEL 6
#define LOW 0
#define HIGH 1
#define RELEASE_BUILD_SLEEP_TIME 43200000000 /* 43,200,000,000 == 12 hours. */
#define TEST_BUILD_SLEEP_TIME 5000000 /* 5000000 == 5 seconds. */

/* RTC capable pins. */
#define IR_SENSOR_PIN 25
#define IR_EMITTER_PIN 26
#define PIR_POWER_PIN 27

typedef enum send_counter {
    INITIAL_SEND, 
    SUBSEQUENT_SEND
} send_counter_t;


/* Callback function prototype. */
void onSent(const esp_now_send_info_t *peer_info, esp_now_send_status_t status);
void onReceived(const esp_now_recv_info_t *peer_info, const uint8_t *data_received, int data_len);



/* Global variables. */
esp_netif_t *netif_wifi_sta;
send_counter_t send_counter_state; /* Callback function registered for sending should be the only one to update this. */



/* Setup. */

bool initWiFi() {
    printf("initWiFi() call entry...\n");
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
    netif_wifi_sta = esp_netif_create_default_wifi_sta(); // Creates the wifi interface. Aborts if it fails. If need graceful handling, check if NULL.
    wifi_init_config_t config = WIFI_INIT_CONFIG_DEFAULT(); 
    ESP_ERROR_CHECK(esp_wifi_init(&config)); // Initialize wifi driver used by the interface.
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start()); // Start wifi in set mode.
    ESP_ERROR_CHECK(esp_wifi_set_channel(TEST_CHANNEL, WIFI_SECOND_CHAN_NONE));
    ESP_ERROR_CHECK(esp_wifi_disconnect()); // Disconnect to ensure d/'evice does not auto-connect to AP or other peer.
   
    printf("initWiFi() call exit...\n");
    
    return true;
}

bool initESPNOW() {
    printf("initESPNOW() call entry...\n");

    ESP_ERROR_CHECK(esp_now_init());

    /* Register callback functions. */
    ESP_ERROR_CHECK(esp_now_register_send_cb(onSent));
    ESP_ERROR_CHECK(esp_now_register_recv_cb(onReceived));
    printf("initESPNOW() call exit...\n");

    return true;
}

void pinConfig() { 
    printf("pinConfig() call entry...\n");

    /* For beam sensor. */
    printf("Setting Sensor direction...\n");
    gpio_set_direction(IR_SENSOR_PIN, GPIO_MODE_INPUT);
     gpio_set_intr_type(IR_SENSOR_PIN, GPIO_INTR_DISABLE);
    gpio_set_pull_mode(IR_SENSOR_PIN, GPIO_PULLUP_ONLY);
    gpio_pullup_en(IR_SENSOR_PIN);

    printf("pinConfig() call exit...\n");
} // End of pinConfig().

void sleepConfig() {
    printf("sleepConfig() call entry...\n");

    /* Turn ON then OFF all Power Domains (PDs). Based on my current knowledge of 
       this is just for avoiding assertion when ref >= 0 is false for any of the PDs.
       Turning is OFF sets ref == 0 which makes it eligible for ESP_PD_OPTION_OFF since
       ref >= 0 is true.
    */
    printf("\n\nTurning PDs to neutral state for sleep...\n\n"); 
    for(int i = 0; i < ESP_PD_DOMAIN_MAX; ++i) {
        esp_sleep_pd_config(i, ESP_PD_OPTION_ON);
    }

    printf("\n\nTurning PDs OFF for sleep...\n\n");

    for(int i = 0; i < ESP_PD_DOMAIN_MAX; ++i) {
        esp_sleep_pd_config(i, ESP_PD_OPTION_OFF);
    }

    // Enable RTC timer as wakeup source.
    esp_sleep_enable_timer_wakeup(TEST_BUILD_SLEEP_TIME);

    printf("sleepConfig() call exit...\n");

} // End of sleepConfig().


/* Send callback function definition start. */

void onSent(const esp_now_send_info_t *peer_info, esp_now_send_status_t status) {
    printf("onSent() call entry...\n");
    char *send_counter_state_desc = send_counter_state == INITIAL_SEND ? "Initial" : "Subsequent";

    if(status == ESP_NOW_SEND_SUCCESS) {
        printf("\n\n%s send succeed.\n\n", send_counter_state_desc);
        
    }
    else {
        printf("\n\n%s Send fail.\n\n", send_counter_state_desc);
    }

    /* To prepare for deep-sleep regardless of delivery state. */
    if(send_counter_state == SUBSEQUENT_SEND) {
        esp_wifi_stop();
        esp_netif_deinit();
        esp_netif_destroy_default_wifi(netif_wifi_sta);

        /* Configure sleep mode to be used. */
        printf("Configuring sleep mode...\n");
        sleepConfig();
        
        /* For debug. */
        printf("\n\nEntering Deep-sleep mode.\n\n");
        ESP_ERROR_CHECK(esp_deep_sleep_try_to_start());
    }

    // Updates only after initial send.
    if(send_counter_state == INITIAL_SEND) {
        send_counter_state = SUBSEQUENT_SEND;
    }
    
    printf("onSent() call exit...\n");
}/* End of onSent(). */

void onReceived(const esp_now_recv_info_t *peer_info, const uint8_t *data_received, int data_len) {
    printf("onReceived() call entry...\n");

    printf("\nReceived from:\n");
    printf("Sender MAC address: " MACSTR "\n", MAC2STR(peer_info->src_addr));
    
    // This is sizeof(data_received) - 1 because null char is not included.
    printf("Message length: %d\n", strlen((const char*)data_received));  
    printf("Message: \n\t%s\n\n", data_received);
    printf("onReceived() call exit...\n");

} /* End of onReceived(). */
/* Send callback function definition end. */

void setupComponents(const uint8_t *master_mac_addr, const uint8_t wifi_channel) {
    printf("setup() call entry...\n");
    
    // Init wifi and esp_now.
    if(initWiFi() && initESPNOW()) {
        printf("\n\nWifi and ESP_NOW Initialization succeeded!\n\n");
    }

    // Fill peer info.
    esp_now_peer_info_t master_info = {
        .channel = wifi_channel,
        .ifidx = WIFI_IF_STA
    };

    // Copy address of peer to the struct.
    memcpy(master_info.peer_addr, master_mac_addr, ESP_NOW_ETH_ALEN); 

    // Add peer to list of devices connected to this device.
    ESP_ERROR_CHECK(esp_now_add_peer(&master_info));

    /* Set send counter state. */
    send_counter_state = INITIAL_SEND;

} // End of setupComponents().

/*
    Program will first read the pin. If the pin reads HIGH, it will not do anything and will go back to sleep.
    Otherwise, the program will set up appropriate components of ESP32 to send the data back to master device.
    This way, the program can avoid unnecessary component setup when pin is HIGH, since it will not send anything.
*/

void app_main() {

    int sensor_read_level = 0;

    /* Configure pin to be used. */
    printf("\nCalling pinConfig().\n");
    pinConfig();

    printf("\nReading sensor level...\n");
    sensor_read_level = gpio_get_level(IR_SENSOR_PIN); /* Read sensor state. */
    printf("\nSensor read level: %s\n", sensor_read_level == HIGH ? "HIGH" : "LOW");

    if(sensor_read_level == LOW) {
        /* Hard-coded for test. But in release, this must still be 
           known ahead of time if broadcast is not used to acquire it. 
        */
        const uint8_t master_mac_addr[ESP_NOW_ETH_ALEN] = {0x88, 0x13, 0xbf, 0x0b, 0xe1, 0x50};

        printf("\nCalling setupESPNOW()...\n");
        /* Set up components to be used for ESP-NOW data transmission. */
        setupComponents(master_mac_addr, TEST_CHANNEL); 

        esp_message msg;

        /* Hard-coded since initial message will not contain actual sensor read level. */
        msg.sensor_read_level = HIGH;

        sprintf(msg.message, "Greetings from Slave device!");
        printf("Sending initial message to greet Master...\n");

        /* Sending initial message to Master device. */
        esp_now_send(master_mac_addr, (const uint8_t*)&msg, sizeof(msg)); /* Non-blocking. */

        /* Description for LOW level sensor read. */
        char sensor_read_level_description[50];
        sprintf(sensor_read_level_description, "Beam broken. There is mail in the mailbox.");

        // This may not be necessary if not printing to serial for debug.
        printf(sensor_read_level_description);
        printf("\n");

        msg.sensor_read_level = sensor_read_level; // The first byte of the message must alway contain the sensor state.
        snprintf(msg.message, sizeof(msg.message), sensor_read_level_description);
        printf("\nSending subsequent message...\n");
        esp_now_send(master_mac_addr, (const uint8_t *)&msg, sizeof(msg));
    }
    else {
        sleepConfig();
        ESP_ERROR_CHECK(esp_deep_sleep_try_to_start()); // Do not send until
    }

} // End of app_main().