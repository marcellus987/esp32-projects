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
#include <driver/rtc_io.h>
#include <esp_sleep.h>

#include "../../misc-headers/esp-now-message-struct.h"


#define MAGIC_NUMBER 0xDEADBEEF

#define TEST_CHANNEL 6
#define LOW 0
#define HIGH 1
#define RELEASE_BUILD_SLEEP_TIME 43200000000 /* 43,200,000,000 == 12 hours. */
#define TEST_BUILD_SLEEP_TIME 5000000 /* 5000000 == 5 seconds. */

/* Sleeping times. */
#define TEST_INITIAL_SLEEP_TIME 5000000 /* 5000000 == 5 seconds. */
#define TEST_PIR_START_UP_SLEEP_TIME 60000000 /* 60000000 == 60 seconds. */
#define TEST_FIRST_MOTION_DETECTED_SLEEP_TIME 5000000 /* 5000000 == 5 seconds. */
#define TEST_IR_BEAM_PULSE_INTERVAL 5000000 /* 5000000 == 5 seconds. */

/* RTC capable pins. */
/* IR emitter and sensor pins. */
#define IR_SENSOR_READ_PIN 25
#define IR_SENSOR_TRANSISTOR_PIN 26
#define IR_EMITTER_TRANSISTOR_PIN 27
#define IR_SENSOR_READ_DELAY 5 /* 1s. */

/* PIR pins. */
#define PIR_TRANSISTOR_PIN 32
#define PIR_READ_PIN 33

#define MAX_PULSE_COUNT 3


typedef enum device_state {
    INITIAL_READ, /* Wakeup source: Timer. */
    PIR_READY, /* Sleep until first motion detected. Wakeup source: PIR_READ_PIN. */
    RETRIEVAL_PHASE, /* Sleep to give user time to empty mailbox. Wakeup source: Timer. */
    IR_BEAM_PULSE /* For beam pulse intervals. Wakeup source: Timer.*/
} device_state_t;


typedef enum sleep_mode {
    SLEEP_INITIAL_TIME,
    SLEEP_PIR_START_UP_TIME,
    SLEEP_AWAIT_MOTION,
    SLEEP_RETRIEVAL_TIME,
    SLEEP_IR_BEAM_PULSE_TIME
} sleep_mode_t;

typedef struct saved_state {
    device_state_t state;
    uint32_t magicNumber;
} saved_state_t;


/* Callback function prototype. */
void onSent(const esp_now_send_info_t *peer_info, esp_now_send_status_t status);
void onReceived(const esp_now_recv_info_t *peer_info, const uint8_t *data_received, int data_len);

/* Global variables. */
esp_netif_t *netif_wifi_sta;
RTC_NOINIT_ATTR saved_state_t next_phase; /* Used for checkpoints due to RTC_NOINIT_ATTR. */
RTC_SLOW_ATTR uint8_t pulse_counter = 0;

//  saved_state_t next_phase; /* Used for checkpoints due to RTC_NOINIT_ATTR. */
//  uint8_t pulse_counter = 0;


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
    printf("irPinConfig() call entry...\n");
        
    const gpio_config_t cfg = {
        .pin_bit_mask = (1ULL << IR_SENSOR_READ_PIN | 1ULL << IR_SENSOR_TRANSISTOR_PIN | 1ULL << IR_EMITTER_TRANSISTOR_PIN),
        .mode = GPIO_MODE_OUTPUT, /* Make sure to change IR_SENSOR_READ_PIN to input. */
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&cfg);

    gpio_set_level(IR_SENSOR_TRANSISTOR_PIN, HIGH);
    gpio_set_level(IR_EMITTER_TRANSISTOR_PIN, HIGH);

    gpio_set_direction(IR_SENSOR_READ_PIN, GPIO_MODE_INPUT); 
    gpio_pullup_en(IR_SENSOR_READ_PIN);
    printf("irPinConfig() call exit...\n");
} /* End of pinConfig(). */

void rtc_PirTransistorPinConfig() {
    printf("rtc_PirTransistorPinConfig() call entry...\n");
    rtc_gpio_init(PIR_TRANSISTOR_PIN);
    rtc_gpio_set_direction(PIR_TRANSISTOR_PIN, RTC_GPIO_MODE_OUTPUT_ONLY);
    rtc_gpio_set_level(PIR_TRANSISTOR_PIN, HIGH);
    rtc_gpio_hold_en(PIR_TRANSISTOR_PIN);
    printf("rtc_PirTransistorPinConfig() call exit...\n");

} /* End of rtc_PirTransistorPinConfig(). */

void rtc_PirReadPinConfig() {
    printf("rtc_PirReadPinConfig() call entry...\n");
    rtc_gpio_init(PIR_READ_PIN);
    rtc_gpio_set_direction(PIR_READ_PIN, RTC_GPIO_MODE_INPUT_ONLY);
    printf("rtc_PirReadPinConfig() call exit...\n");
} /* End of rtc_PirReadPinConfig(). */

void rtc_PirTurnOff() {
    printf("rtc_PirTurnOff() call entry...\n");
    rtc_gpio_hold_dis(PIR_TRANSISTOR_PIN);
    rtc_gpio_set_direction(PIR_TRANSISTOR_PIN, RTC_GPIO_MODE_DISABLED);
    rtc_gpio_set_direction(PIR_READ_PIN, RTC_GPIO_MODE_DISABLED);
    rtc_gpio_deinit(PIR_TRANSISTOR_PIN);
    rtc_gpio_deinit(PIR_READ_PIN);
    printf("rtc_PirTurnOff() call exit...\n");
} /* End of rtc_PirTurnOff(). */

/**/
void turnOffIrPin(uint64_t mask) {
    printf("turnOffIrPin() call entry...\n");
    const gpio_config_t cfg = {
        .pin_bit_mask = mask,
        .mode = GPIO_MODE_DISABLE
    };

    gpio_config(&cfg);
    printf("turnOffIrPin() call exit...\n");
}/* End of turnOffIrPin(). */

/********** readIrPin() wrapper functions start. **********/
uint8_t readIrPin() {
    uint8_t sensor_read_level;

    /* Configure IR pins to be used. */
    printf("\nCalling irPinConfig().\n");
    irPinConfig();

    printf("\nReading sensor level...\n");
    sensor_read_level = gpio_get_level(IR_SENSOR_READ_PIN); /* Read sensor state. */
    printf("Delaying ~5ms to allow IR sensor to process signal...\n");
   
    vTaskDelay(IR_SENSOR_READ_DELAY);
    printf("Sensor read level: %d.\n", sensor_read_level);    
    /* For debug. */
    printf("Deactivating IR pins...\n"); 

    turnOffIrPin(1ULL << IR_EMITTER_TRANSISTOR_PIN | 1ULL << IR_SENSOR_TRANSISTOR_PIN);

    // printf("IR pin turned OFF. Signal should be LOW...\n");
    // printf("Delaying... Check Signal if LOW...\n");
    // vTaskDelay(IR_SENSOR_READ_DELAY);

    return sensor_read_level;
}
/********** readIrPin() wrapper functions end. **********/
/********** Pin configurations End. **********/


/********** Sleep configurations start. **********/
void configDeepSleep(sleep_mode_t mode) {
    bool rtc_pd_shutdown = true;
    printf("configDeepSleep() call entry...\n");

    /* Turn ON then OFF all Power Domains (PDs). Based on my current knowledge of 
       this is just for avoiding assertion when ref >= 0 is false for any of the PDs.
       Turning is OFF sets ref == 0 which makes it eligible for ESP_PD_OPTION_OFF since
       ref >= 0 is true.
    */
    if(mode == SLEEP_INITIAL_TIME) {
        esp_sleep_enable_timer_wakeup(TEST_INITIAL_SLEEP_TIME);
    }
    else if(mode == SLEEP_PIR_START_UP_TIME) {
        esp_sleep_enable_timer_wakeup(TEST_PIR_START_UP_SLEEP_TIME);
    }
    else if(mode == SLEEP_AWAIT_MOTION) {
        rtc_pd_shutdown = false;
        esp_sleep_enable_ext0_wakeup(PIR_READ_PIN, HIGH); /* This one is signal driven. The rest are timer-based wakeup source.*/
    }
    else if(mode == SLEEP_RETRIEVAL_TIME) {
        esp_sleep_enable_timer_wakeup(TEST_FIRST_MOTION_DETECTED_SLEEP_TIME);
    }
    else { /* mode == SLEEP_IR_BEAM_PULSE_TIME. */
        esp_sleep_enable_timer_wakeup(TEST_IR_BEAM_PULSE_INTERVAL);
    }

    // if(rtc_pd_shutdown) {
    //     for(int i = 0; i < ESP_PD_DOMAIN_MAX; ++i) {
    //         esp_sleep_pd_config(i, ESP_PD_OPTION_ON);
    //     }


    //     for(int i = 0; i < ESP_PD_DOMAIN_MAX; ++i) {
    //         esp_sleep_pd_config(i, ESP_PD_OPTION_OFF);
    //     }
    // }

    printf("configDeepSleep() call exit...\n");

} /* End of configDeepSleep(). */
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
        .sensor_read_level = 255,
        .message = "Error Broadcasted! Unicast failed. Check system configuration."
    };

    esp_now_send(broadcast_mac, (uint8_t *)&msg, sizeof(msg));
} /* End of broadcastPanic(). */

/* Will resend message 3 times at most if it fails during the first try.
*/
esp_err_t try_send(const uint8_t *master_mac_addr, const esp_message msg) {
    esp_err_t err;
    int try_cap = 4; /* 1 for the first send. 3 for the resend. */
    size_t message_size = sizeof(msg.flag) + sizeof(msg.sensor_read_level) + strlen(msg.message) + 1; /* +1 for the NULL char. */

    for(int i = 0; i < try_cap; ++i) {
    /* For debug. */
        printf("Retry #%d...\n", i);
        err = esp_now_send(master_mac_addr, (const uint8_t *)&msg, message_size);

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
    printf("app_main() start...\n");
    device_state_t current_state;
    sleep_mode_t next_sleep_mode = SLEEP_INITIAL_TIME;

    printf("Checking magic number to verify next state...\n");
    if(next_phase.magicNumber != MAGIC_NUMBER) {
        printf("Invalid Magic Number!\n");
        next_phase.state = INITIAL_READ;
        next_phase.magicNumber = MAGIC_NUMBER;
    }
    
    current_state = next_phase.state;

    printf("Entering switch(current_state) statement...\n");
 
    /* next_phase is stored in RTC SLOW MEMORY. */
    switch(current_state) {
        case INITIAL_READ: {
            printf("Case INITIAL_READ");
            uint8_t sensor_read_level = readIrPin();

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
                        /* Activate PIR sensor. */
                        /* For debug. */
                        printf("Activating rtc PIR transistor pins...\n");
                        rtc_PirTransistorPinConfig();
                        next_sleep_mode = SLEEP_PIR_START_UP_TIME;
                        next_phase.state = PIR_READY;
                        printf("PIR Sensor ON. Going to deep-sleep to allow it to calibrate...\n");
                    } /* if for sensor read message. */
                } /* if for inital message. */
            }
            break;
        }  /* case FROM_INITIAL_READ: */

        case PIR_READY: { /* After PIR startup. */
            printf("PIR Sensor Ready...\n");
            printf("Activating rtc PIR read pins...\n");
            rtc_PirReadPinConfig();
            next_sleep_mode = SLEEP_AWAIT_MOTION;
            printf("Entering deep-sleep to await motion trigger...\n");
            next_phase.state = RETRIEVAL_PHASE;
            break;
        } /* case PIR_START_UP: */
        case RETRIEVAL_PHASE: {
            printf("First motion detected. Entering deep-sleep to allow user to empty mailbox...\n");
            rtc_PirTurnOff();
            next_sleep_mode = SLEEP_RETRIEVAL_TIME;
            next_phase.state = IR_BEAM_PULSE;
            break;
        } /* case AWAITING_FOR_MOTION: */
        case IR_BEAM_PULSE: {            

            printf("IR Beam Pulse Phase...\n");
            next_sleep_mode = SLEEP_INITIAL_TIME; /* Assuming it's Done. Reset state.*/

            if(pulse_counter < MAX_PULSE_COUNT) {
                uint8_t sensor_read_level = readIrPin();
                if(sensor_read_level == HIGH) {
                    /* Hard-coded for test. But in release, this must still be 
                    known ahead of time if broadcast is not used to acquire it. 
                    */
                    const uint8_t master_mac_addr[ESP_NOW_ETH_ALEN] = {0x88, 0x13, 0xbf, 0x0b, 0xe1, 0x50};
                
                    printf("\nCalling setupESPNOW()...\n");
                    /* Set up components to be used for ESP-NOW data transmission. */
                    setupComponents(master_mac_addr, TEST_CHANNEL); 

                    /* Message structure. */
                    esp_message msg;

                    /* Description for HIGH level sensor read. */
                    char sensor_read_level_description[50];
                    sprintf(sensor_read_level_description, "Beam unbroken. Mailbox now empty.");

                    /* This may not be necessary if not printing to serial for debug. */
                    printf(sensor_read_level_description);
                    printf("\n");

                    msg.flag = SENSOR_READ;
                    msg.sensor_read_level = sensor_read_level;
                    snprintf(msg.message, sizeof(msg.message), sensor_read_level_description);
                    printf("\nSending subsequent message...\n");
                    
                    try_send(master_mac_addr, msg);
                    next_phase.state = INITIAL_READ;
                    
                } /* for if(sensor_read_level == HIGH). */
                else { /* sensor_read_level == LOW */
                    next_sleep_mode = SLEEP_IR_BEAM_PULSE_TIME;
                    ++pulse_counter;
                    printf("IR Pulse Count: %d.\n", pulse_counter);
                    if(pulse_counter == MAX_PULSE_COUNT) {
                        printf("Max Pulse Count Reached! Returning to initial state.\n");
                        next_phase.state = INITIAL_READ;
                        pulse_counter = 0;
                    }
                }
            } /* for if(pulse_counter < MAX_PULSE_COUNT).*/
            break;
        } /* case IR_BEAM_PULSE: */
    } /* switch(phase). */

    configDeepSleep(next_sleep_mode);
    ESP_ERROR_CHECK(esp_deep_sleep_try_to_start()); // Do not send until
} // End of app_main().


/********** APP_MAIN end. **********/


/*

deepsleep(INITIAL): 12hr.
deepsleep(PIR_START_UP): 1min.
deepsleep(WAIT_FOR_MOTION): Trigger.
deepsleep(FIRST_MOTION): 1min.
deepsleep(PULSE): 30 sec.

*/