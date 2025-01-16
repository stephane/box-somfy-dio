#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>
#include "box-somfy-dio.h"


// ---------------------------------
//
// Step 1 - SOMFY equipments
//
// ---------------------------------
// 1. mqtt_topic_base
//    It should be prefixed by the MQTT discovery prefix of Home Assistant to
//    expose the device, eg. "somfy/kitchen" -> "homeassistant/cover/somfy/kitchen"
//    The command should be sent to <topic>/set and state will be stored at
//    <topic>/state.
// 2. Name of the cover
//

const unsigned long SOMFY_REMOTE_BASE_ID = 0x100000;

const somfy_config_remote_t SOMFY_CONFIG_REMOTES[] = {
    {"homeassistant/cover/kitchen", "Kitchen"}, // 0
    {"homeassistant/cover/desktop", "Desktop"}, // 1
};


// First value 0 for Kitchen, 1 for Desktop and final index should be -1;
const int main_group_indexes[] = {0, 1, -1};
const config_group_t SOMFY_CONFIG_GROUPS[] = {
    {"homeassistant/cover/group/main", "All", main_group_indexes}
};

// For Chacon DiO 1.0
const unsigned long DIO_REMOTE_BASE_ID = 0x200000;
const dio_config_remote_t DIO_CONFIG_REMOTES[] = {};
const config_group_t DIO_CONFIG_GROUPS[] = {};

// Leave to false except if you want to reset the rolling code
const bool RESET_ROLLING_CODES = false;
const int DEFAULT_ROLLING_CODE = 1;

// ---------------------------------
//
// Step 2 - WIFI
//
// ---------------------------------
const char *WIFI_SSID = "";
const char *WIFI_PASSWORD = "";

// MAC address will be automatically added to hostname to provide unique ID
const char *HOSTNAME_PREFIX = "box-somfy-dio";

// ---------------------------------
//
// Step 3 - MQTT SERVER
//
// ---------------------------------
// The box will blink until sucessful connection.
const char *MQTT_SERVER = "192.168.1.x";
const unsigned int MQTT_PORT = 1883;
const char *MQTT_USER = "";
const char *MQTT_PASSWORD = "";

const char *MQTT_HOMEASSISTANT_DISCOVERY_SUFFIX = "/config";
const char *MQTT_DISCOVERY_CONFIG = "{\"~\":\"<BASE_TOPIC>\",\"name\":\"<NAME>\",\"command_topic\":\"~/set\",\"payload_close\":\"d\",\"payload_stop\":\"s\",\"payload_open\":\"u\"}";

#endif
