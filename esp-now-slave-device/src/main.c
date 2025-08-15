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
/* IR emitter and sensor pins. */
#define IR_SENSOR_READ_PIN 25
#define IR_SENSOR_TRANSISTOR_PIN 26
#define IR_EMITTER_TRANSISTOR_PIN 27

/* PIR pins. */
#define PIR_TRANSISTOR_PIN 32
#define PIR_READ_PIN 33

/* Callback function prototype. */
void onSent(const esp_now_send_info_t *peer_info, esp_now_send_status_t status);
void onReceived(const esp_now_recv_info_t *peer_info, const uint8_t *data_received, int data_len);

/* Global variables. */
esp_netif_t *netif_wifi_sta;

/********** ESP-NOW Component setup start. **********/
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
}/* End of initWiFi(). */

bool initESPNOW() {
    printf("initESPNOW() call entry...\n");

    ESP_ERROR_CHECK(esp_now_init());

    /* Register callback functions. */
    ESP_ERROR_CHECK(esp_now_register_send_cb(onSent));
    ESP_ERROR_CHECK(esp_now_register_recv_cb(onReceived));
    printf("initESPNOW() call exit...\n");

    return true;
} /* End of initESPNOW(). */


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
} // End of setupComponents().
/********** ESP-NOW Component setup end. **********/


/********** Pin configurations start. **********/
void irPinConfig() { 
   const gpio_config_t cfg = {
    .pin_bit_mask = (1ULL << IR_SENSOR_READ_PIN | 1ULL | IR_SENSOR_TRANSISTOR_PIN | 1ULL << IR_EMITTER_TRANSISTOR_PIN),
    .mode = GPIO_MODE_OUTPUT, /* Make sure to change IR_SENSOR_READ_PIN to input. */
    .intr_type = GPIO_INTR_DISABLE
   };
   gpio_config(&cfg);

   gpio_set_direction(IR_SENSOR_READ_PIN, GPIO_MODE_INPUT); 
   gpio_pullup_en(IR_SENSOR_READ_PIN);
} // End of pinConfig().

void pirPinConfig() {
   gpio_set_direction(PIR_READ_PIN, GPIO_MODE_INPUT);
   gpio_set_direction(PIR_TRANSISTOR_PIN, GPIO_MODE_OUTPUT);
   gpio_set_intr_type(PIR_READ_PIN, GPIO_INTR_DISABLE); /* Disabled for now. Need more knowledge about this.*/
}

/**/
void turnOffPin(uint64_t mask) {
    const gpio_config_t cfg = {
        .pin_bit_mask = mask,
        .mode = GPIO_MODE_DISABLE
    };

    gpio_config(&cfg);
}
/********** Pin configurations End. **********/


/********** Sleep configurations start. **********/
void configDeepSleep() {
    printf("configDeepSleep() call entry...\n");

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

    printf("configDeepSleep() call exit...\n");

} // End of sleepConfig().

void configLightSleep() {
    printf("configLightSleep() call entry...\n");


    printf("configLightSleep() call exit...\n");
} /* End of configLightSleep(). */
/********** Sleep configurations end. **********/



/********** Send callback function definition start. **********/
void onSent(const esp_now_send_info_t *peer_info, esp_now_send_status_t status) {
    printf("onSent() call entry...\n");
    printf("Send %s\n", status == ESP_NOW_SEND_SUCCESS ? "Succeeded" : "Failed");
    printf("onSent() call exit...\n");
}/* End of onSent(). */


void onReceived(const esp_now_recv_info_t *peer_info, const uint8_t *data_received, int data_len) {
    esp_message *msg = (esp_message *)data_received;
    printf("onReceived() call entry...\n");

    printf("\nReceived from:\n");
    printf("Sender MAC address: " MACSTR "\n", MAC2STR(peer_info->src_addr));
    printf("Message Flag: %s\n", msg->flag == NORMAL_MESSAGE ? "NORMAL_MESSAGE" : msg->flag == SENSOR_READ ? "SENSOR_READ" : "ERROR_BROADCAST");
    if(msg->flag == SENSOR_READ) {
        printf("Sensor read level: %s\n", msg->sensor_read_level == HIGH ? "HIGH" : "LOW");
    }
    
    printf("Description: %s\n", msg->message);
    printf("onReceived() call exit...\n");
} /* End of onReceived(). */
/********** Send callback function definition end. **********/


/********** ESP_NOW_SEND wrapper functions start. **********/
void broadcastPanic(uint8_t wifi_channel) {
 /* Used for broadcasting messages. Usually, for error messages. */
    const uint8_t broadcast_mac[ESP_NOW_ETH_ALEN] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

    esp_now_peer_info_t broadcast_info = {
        .channel = wifi_channel,
        .ifidx = WIFI_IF_STA
    };

    memcpy(broadcast_info.peer_addr, broadcast_mac, ESP_NOW_ETH_ALEN); 
    ESP_ERROR_CHECK(esp_now_add_peer(&broadcast_info));

    esp_message msg = {
        .flag = ERROR_BROADCAST,
        .sensor_read_level = 0,
        .message = "Error Broadcasted! Unicast failed. Check system configuration."
    };

    esp_now_send(broadcast_mac, (uint8_t *)&msg, sizeof(msg));
} /* End of broadcastPanic(). */

/* Will resend message 3 times at most if it fails during the first try.
*/
esp_err_t try_send(const uint8_t *master_mac_addr, const esp_message msg) {
    esp_err_t err;
    int try_cap = 4; /* 1 for the first send. 3 for the resend. */

    for(int i = 0; i < try_cap; ++i) {
    /* For debug. */
        printf("Retry #%d...\n", i);
        err = esp_now_send(master_mac_addr, (const uint8_t *)&msg, sizeof(msg));

        if(err == ESP_OK) {
            break;
        }
    }

    if(err != ESP_OK) {
        /* Need to retrieve channel for broadcast. */
        esp_now_peer_info_t master_info;
        esp_now_get_peer(master_mac_addr, &master_info);
        broadcastPanic(master_info.channel);
    }

    return err;
} /*End of try_send(). */


/********** ESP_NOW_SEND wrapper functions end. **********/


/********** APP_MAIN start. **********/

/*
    Program will first read the pin. If the pin reads HIGH, it will not do anything and will go back to sleep.
    Otherwise, the program will set up appropriate components of ESP32 to send the data back to master device.
    This way, the program can avoid unnecessary component setup when pin is HIGH, since it will not send anything.
*/

void app_main() {
    /* Device will always strive to sleep deep to conserve energy. */
    bool toSleepDeep = true; 
    uint8_t sensor_read_level = 0;

    /* Configure IR pins to be used. */
    printf("\nCalling irPinConfig().\n");
    irPinConfig();

    printf("\nReading sensor level...\n");
    sensor_read_level = gpio_get_level(IR_SENSOR_READ_PIN); /* Read sensor state. */
    printf("\nSensor read level: %s\n", sensor_read_level == HIGH ? "HIGH" : "LOW");

    /* If mail in mailbox, setup necessary components for data tranmission to update Master. */
    if(sensor_read_level == LOW) {
        /* Hard-coded for test. But in release, this must still be 
           known ahead of time if broadcast is not used to acquire it. 
        */
        const uint8_t master_mac_addr[ESP_NOW_ETH_ALEN] = {0x88, 0x13, 0xbf, 0x0b, 0xe1, 0x50};
        
        printf("\nCalling setupESPNOW()...\n");
        /* Set up components to be used for ESP-NOW data transmission. */
        setupComponents(master_mac_addr, TEST_CHANNEL); 

        /* Message structure. */
        esp_message msg;

        /* ----- Initial message to master. This can be removed in necessary. ----- */
        msg.flag = NORMAL_MESSAGE;
        msg.sensor_read_level = 0; /* Hard-coded since initial message will not contain actual sensor read level. */
        sprintf(msg.message, "Greetings from Slave device!");
        printf("Sending initial message to greet Master...\n");

        /* Try to send inital message to check if there's any problem. */
        if(try_send(master_mac_addr, msg) == ESP_OK) {
            /* ----- Sensor Read Message ----- */

            /* Description for LOW level sensor read. */
            char sensor_read_level_description[50];
            sprintf(sensor_read_level_description, "Beam broken. There is mail in the mailbox.");

            /* This may not be necessary if not printing to serial for debug. */
            printf(sensor_read_level_description);
            printf("\n");

            msg.flag = SENSOR_READ;
            msg.sensor_read_level = sensor_read_level;
            snprintf(msg.message, sizeof(msg.message), sensor_read_level_description);
            printf("\nSending subsequent message...\n");
    
            if(try_send(master_mac_addr, msg) == ESP_OK) {
                toSleepDeep = false; /* Device will be on light-sleep until mailbox is emptied. */

                /* Activate PIR sensor. */
                /* For debug. */
                printf("Activating PIR sensor pins...\n");
                pirPinConfig();

                /* For debug. */
                printf("Deactivating IR pins...\n");
                turnOffPin(1ULL << IR_EMITTER_TRANSISTOR_PIN | 1ULL << IR_SENSOR_READ_PIN | 1ULL << IR_SENSOR_TRANSISTOR_PIN);
                
            }
        }
    }

    if(toSleepDeep) {
        configDeepSleep();
        ESP_ERROR_CHECK(esp_deep_sleep_try_to_start()); // Do not send until
    }
    else {
        // light sleep
    }
    

} // End of app_main().

/********** APP_MAIN end. **********/


