/*

This program allows you to emulate a Somfy RTS or Simu HZ remote. If you want to
learn more about the Somfy RTS protocol, check out
https://pushstack.wordpress.com/somfy-rts-protocol/

The rolling code will be stored in non-volatile storage (Preferences), so that
you can power the Arduino off.

Modifications should only be needed in config.h.

Licence CC BY NC SA

Original code https://github.com/Nickduino/Somfy_Remote Adapted by TOST Corp.
(2018 to 2020). Modified by Stéphane Raimbault <stephane.raimbault@gmail.com>,
11/2020
- MQTT Discovery for easy integration with Home Assistant
- a cover can be associated to many groups
- groups are exposed as remotes in Home Assistant
- simpler configuration (no rolling code, EEPROM addresses, etc)
- no dynamic allocation in loop() to avoid memory fragmentation
- faster code (remove many delays, fast path, pointers, pure string management,
  early return in functions)
- removed useless Ticker
- comments in english
- don't code to publish set up on weird server
- remove publishing of every handled commands on MQTT box topic
- DRY on many parts of the code
- common hostname/MQTT ID/MQTT topic for the box generated from MAC address The

*/

#include "PubSubClient.h"
#include <Arduino.h>
#include <EEPROM.h>
#include <ESP8266WiFi.h>
#include <string.h>

#include "box-somfy-dio.h"
#include "config.h"

#define SIG_HIGH GPIO_REG_WRITE(GPIO_OUT_W1TS_ADDRESS, 1 << PORT_TX)
#define SIG_LOW  GPIO_REG_WRITE(GPIO_OUT_W1TC_ADDRESS, 1 << PORT_TX)

// Buttons
#define SYMBOL 640
#define UP     0x2
#define STOP   0x1
#define DOWN   0x4
#define PROG   0x8

// Output data on pin 23 (can range from 0 to 31). Check pin numbering on
// ESP8266.
#define PORT_TX D1

// To display frames
// #define DEBUG_ENABLED

WiFiClient wifiClient;
PubSubClient mqttClient;

static String s_hostname;
// MQTT status topic of the box (online etc)
static char *mqtt_box_topic_status = NULL;
static byte frame[7];
static remote_t *remotes = NULL;
static int nb_remotes = 0;

void setup() {
  Serial.begin(115200);
  while (!Serial) {
    ; // wait for serial port to connect. Needed for native USB
  }
  delay(1000);

  Serial.println("Available features:");
  Serial.println("- SOMFY RTS");
  Serial.println("- CHACON DiO 1.0");
  Serial.println("");
  Serial.println("Starting the Box...");

  pinMode(LED_BUILTIN, OUTPUT);
  led_on();

  // Open the output for 433.42MHz and 433.92MHz transmitter
  pinMode(PORT_TX, OUTPUT);
  SIG_LOW;
  digitalWrite(PORT_TX, LOW);

  s_hostname = String(HOSTNAME_PREFIX) + "-" + WiFi.macAddress();
  // Remove ':'
  while (s_hostname.indexOf(':') != -1) {
    int index_to_remove = s_hostname.indexOf(':');
    s_hostname.remove(index_to_remove, 1);
  }

  Serial.print("Hostname with MAC address: ");
  Serial.println(s_hostname);

  // Connect to WiFi
  Serial.print("WiFi connection to ");
  Serial.print(WIFI_SSID);
  Serial.print(" ");

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  WiFi.hostname(s_hostname.c_str());
  mqtt_box_topic_status =
      new_str_from_base_and_suffix(s_hostname.c_str(), "/status");

  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");

    // Blink on every test
    wait_and_blink_led(100, 50);
  }
  led_off();

  Serial.print("Connected to WiFi | IP address ");
  Serial.println(WiFi.localIP());

  // Configure MQTT
  mqttClient.setClient(wifiClient);
  mqttClient.setServer(MQTT_SERVER, MQTT_PORT);
  mqttClient.setCallback(received_callback);
  mqttClient.setBufferSize(320);

  EEPROM.begin(1024);

  Serial.println("Read the configuration...");

  // Also reset the rolling codes for ESP8266 if needed.
  remotes = get_remotes();
  for (int i = 0; i < nb_remotes; i++) {
    // Print out all the configured remotes.
    remote_print(&remotes[i]);
  }

  Serial.print("");
}

remote_t *get_remotes() {
  Serial.println("Defined in configurations:");

  int nb_somfy_remotes =
      get_nb_items(sizeof(SOMFY_CONFIG_REMOTES), sizeof(somfy_config_remote_t));
  Serial.print(nb_somfy_remotes);
  Serial.println(" Somfy remotes");

  int nb_somfy_groups =
      get_nb_items(sizeof(SOMFY_CONFIG_GROUPS), sizeof(config_group_t));
  Serial.print(nb_somfy_groups);
  Serial.println(" Somfy groups");

  int nb_dio_remotes =
      get_nb_items(sizeof(DIO_CONFIG_REMOTES), sizeof(dio_config_remote_t));
  Serial.print(nb_dio_remotes);
  Serial.println(" Dio remotes");

  int nb_dio_groups =
      get_nb_items(sizeof(DIO_CONFIG_GROUPS), sizeof(config_group_t));
  Serial.print(nb_dio_groups);
  Serial.println(" DiO groups");

  // Set global variable nb_remotes
  nb_remotes =
      nb_somfy_remotes + nb_somfy_groups + nb_dio_remotes + nb_dio_groups;
  remote_t *new_remotes = (remote_t *)malloc(nb_remotes * sizeof(remote_t));

  // 1 - Set the Somfy remotes
  int i = 0;
  while (i < nb_somfy_remotes) {
    remote_t *remote = &new_remotes[i];
    remote->description = SOMFY_CONFIG_REMOTES[i].description;
    remote->type = SomfyType;
    remote->is_group = false;
    remote_set_mqtt_topics(remote, SOMFY_CONFIG_REMOTES[i].mqtt_topic_base);
    remote->id = SOMFY_REMOTE_BASE_ID + i;
    remote->eeprom_address = i * 4;

    if (RESET_ROLLING_CODES) {
      EEPROM.put(remote->eeprom_address, DEFAULT_ROLLING_CODE);
      EEPROM.commit();
    }
    i++;
  }

  // 2 - Add groups (somfy remotes + groups)
  i = 0;
  while (i < nb_somfy_groups) {
    remote_t *group_remote = &new_remotes[nb_somfy_remotes + i];
    group_remote->type = SomfyType;
    group_remote->is_group = true;
    group_remote->description = SOMFY_CONFIG_GROUPS[i].description;
    remote_set_mqtt_topics(group_remote,
                           SOMFY_CONFIG_GROUPS[i].mqtt_topic_base);
    group_remote->remote_indexes = SOMFY_CONFIG_GROUPS[i].remote_indexes;
    i++;
  }

  // 3 - Add CHACON Dio remotes
  i = 0;
  while (i < nb_dio_remotes) {
    remote_t *remote = &new_remotes[nb_somfy_remotes + nb_somfy_groups + i];
    remote->description = DIO_CONFIG_REMOTES[i].description;
    remote->type = DioType;
    remote->is_group = false;
    remote_set_mqtt_topics(remote, DIO_CONFIG_REMOTES[i].mqtt_topic_base);
    remote->id = DIO_REMOTE_BASE_ID + i;
    remote->sender = DIO_CONFIG_REMOTES[i].sender;
    remote->interruptor = DIO_CONFIG_REMOTES[i].interruptor;
    i++;
  }

  // 4 - Add DiO groups
  while (i < nb_dio_groups) {
    remote_t *group_remote =
        &new_remotes[nb_somfy_remotes + nb_somfy_groups + nb_dio_remotes + i];
    group_remote->type = DioType;
    group_remote->is_group = true;
    group_remote->description = DIO_CONFIG_GROUPS[i].description;
    remote_set_mqtt_topics(group_remote, DIO_CONFIG_GROUPS[i].mqtt_topic_base);
    // Offset the indexes
    group_remote->remote_indexes = nb_somfy_remotes + nb_somfy_groups +
                                   DIO_CONFIG_GROUPS[i].remote_indexes;
    i++;
  }

  return new_remotes;
}

char *new_str_from_base_and_suffix(const char *base, const char *suffix) {
  int length = strlen(base) + strlen(suffix);
  char *new_str = (char *)malloc((length + 1) * sizeof(char));
  strcpy(new_str, base);
  strcat(new_str, suffix);
  return new_str;
}

void remote_set_mqtt_topics(remote_t *remote, const char *mqtt_topic_base) {
  remote->mqtt_topic_base = mqtt_topic_base;
  remote->mqtt_topic_set =
      new_str_from_base_and_suffix(mqtt_topic_base, "/set");
  remote->mqtt_topic_state =
      new_str_from_base_and_suffix(mqtt_topic_base, "/state");
}

int get_nb_items(size_t size_all, size_t size_one) {
  if (size_all != 0) {
    return size_all / size_one;
  } else {
    return 0;
  }
}

void remote_print(remote_t *remote) {
  if (remote->is_group) {
    Serial.print("Group");
  }

  if (remote->type == SomfyType) {
    Serial.print("SOMFY RTS: ");
  } else if (remote->type == DioType) {
    Serial.print("CHACON DiO: ");
  }
  Serial.print(remote->description);
  Serial.print(" | topic: ");
  Serial.print(remote->mqtt_topic_base);

  if (remote->type == SomfyType && !remote->is_group) {
    Serial.print(" | number: ");
    Serial.print(remote->id, HEX);
    Serial.print(" | rolling code: ");
    unsigned int current_code;
    EEPROM.get(remote->eeprom_address, current_code);
    Serial.println(current_code);
  } else if (remote->type == DioType && !remote->is_group) {
    Serial.print(" | sender: ");
    Serial.print(remote->sender);
    Serial.print(" | interruptor: ");
    Serial.println(remote->interruptor);
  } else {
    Serial.println("");
  }
}

void loop() {
  // Reconnect MQTT if needed
  while (!mqttClient.connected()) {
    bool ok = mqtt_connect();
    if (ok) {
      Serial.println("The box is ready.");
      led_off();
    } else {
      Serial.print("Unable to connect to MQTT server ");
      Serial.print(MQTT_SERVER);
      Serial.print(" on port ");
      Serial.println(MQTT_PORT);
      wait_and_blink_led(100, 50);
    }
  };
  mqttClient.loop();
}

void mqtt_publish_discovery(const char *mqtt_discovery_json,
                            const char *mqtt_topic_base,
                            const char *description) {
  // homeassistant/cover/ + desktop + /config
  const char *discovery_topic = new_str_from_base_and_suffix(
      mqtt_topic_base, MQTT_HOMEASSISTANT_DISCOVERY_SUFFIX);
  String s_discovery_json = String(mqtt_discovery_json);
  Serial.print("Publish Home Assistant MQTT Discovery ");
  Serial.println(discovery_topic);
  s_discovery_json.replace("<BASE_TOPIC>", mqtt_topic_base);
  s_discovery_json.replace("<NAME>", description);
  mqttClient.publish(discovery_topic, s_discovery_json.c_str(), true);
}

bool mqtt_connect() {
  Serial.print("Connecting to MQTT as ");
  Serial.print(s_hostname);
  if (mqttClient.connect(s_hostname.c_str(), MQTT_USER, MQTT_PASSWORD,
                         mqtt_box_topic_status, 1, 1, "offline")) {
    Serial.println(": connected.");

    // Subscribe to the topic of each remote with QoS 1
    for (int i = 0; i < nb_remotes; i++) {
      const remote_t *remote = &remotes[i];
      Serial.print("Subscribe to ");
      Serial.println(remote->mqtt_topic_set);
      mqttClient.subscribe(remote->mqtt_topic_set, 1);

      mqtt_publish_discovery(MQTT_DISCOVERY_CONFIG, remote->mqtt_topic_base,
                             remote->description);
    }

    // Update status, message is retained
    Serial.println("Publish the status 'online' of the box.");
    mqttClient.publish(mqtt_box_topic_status, "online", true);
    Serial.println("MQTT setup is done.");
    return true;
  } else {
    Serial.print("\nUnable to connect to MQTT server (status code ");
    Serial.print(mqttClient.state());
    Serial.println("). Try again in 2 seconds...");
    // Wait 2 seconds before retrying
    wait_and_blink_led(2000, 200);
    return false;
  }
}

// Not used because it disables the action buttons in HA (no up action when
// open, etc).
//
// void mqtt_publish_state_from_command(const char *mqtt_topic_state, const char
// command, bool in_progress) {
//   const char *state;
//   switch (command) {
//     case 'u':
//       state = (in_progress ? "opening" : "open");
//       break;
//     case 'd':
//       state = (in_progress ? "closing" : "open");
//       break;
//     case 's':
//       state = "open";
//       break;
//     default:
//       return;
//   }
//   Serial.print(mqtt_topic_state);
//   Serial.print(": ");
//   Serial.println(state);
//   mqttClient.publish(mqtt_topic_state, state, false);
// }

void received_callback(char *topic, byte *payload, unsigned int length) {
  char command = *payload; // 1st byte of payload

  Serial.print("MQTT message received: ");
  Serial.println(topic);
  Serial.print("Payload: ");
  for (int i = 0; i < length; i++) {
    Serial.print((char)payload[i]);
  }
  Serial.println();

  if (length != 1) {
    return;
  }

  RemoteType detected_type;
  // Somfy command is valid if the payload contains one of the chars below AND
  // the topic corresponds to one of the somfy_remotes
  if (command == 'u' || command == 's' || command == 'd' || command == 'p') {
    Serial.print("SOMFY command received on topic: ");
    Serial.println(command);
    detected_type = SomfyType;
  } else if (command == '1' || command == '0') {
    Serial.print("DiO command received on topic: ");
    Serial.println(command);
    detected_type = DioType;
  } else {
    return;
  }

  led_on();
  for (int i = 0; i < nb_remotes; i++) {
    const remote_t *remote = &remotes[i];
    if (remote->type == detected_type &&
        strcmp(remote->mqtt_topic_set, topic) == 0) {

      // Transitive state
      // mqtt_publish_state_from_command(remote->mqtt_topic_state,
      // command, true);
      if (remote->is_group) {
        // Group
        unsigned int j = 0;

        while (remote->remote_indexes[j] != -1) {
          int remote_index = remote->remote_indexes[j];
          Serial.println(remotes[remote_index].description);
          // mqtt_publish_state_from_command(remotes[remote_index].mqtt_topic_state,
          // command, true);
          send_command(detected_type, command,
                       (const remote_t *)&remotes[remote_index]);
          // mqtt_publish_state_from_command(remotes[remote_index].mqtt_topic_state,
          // command, false);
          j++;
        }
      } else {
        send_command(detected_type, command, remote);
      }
      // Final state
      // mqtt_publish_state_from_command(remote->mqtt_topic_state,
      // command, false);
      led_off();
      return;
    }
  }
}

void send_command(RemoteType type, char command, const remote_t *remote) {
  if (type == SomfyType) {
    somfy_send_command(command, remote);
  } else {
    dio_send_command(command, remote);
  }
}

void somfy_send_command(char command, const remote_t *remote) {
  led_toggle();

  if (command == 'u') {
    Serial.println("COMMAND - Up");
    build_frame(frame, UP, remote);
  } else if (command == 's') {
    Serial.println("COMMAND - Stop");
    build_frame(frame, STOP, remote);
  } else if (command == 'd') {
    Serial.println("COMMAND - Down");
    build_frame(frame, DOWN, remote);
  } else if (command == 'p') {
    Serial.println("COMMAND - Prog");
    build_frame(frame, PROG, remote);
  }

  Serial.println("");

  send_frame(frame, 2);
  for (int i = 0; i < 2; i++) {
    send_frame(frame, 7);
  }
}

#ifdef DEBUG_ENABLED
void print_frame(byte *frame) {
  for (size_t i = 0; i < 7; i++) {
    if (frame[i] >> 4 == 0) { //  Displays leading zero in case the most
                              //  significant nibble is a 0.
      Serial.print("0");
    }
    Serial.print(frame[i], HEX);
    Serial.print(" ");
  }
}
#endif

void build_frame(byte *frame, byte button, const remote_t *remote) {
  unsigned int code;

  EEPROM.get(remote->eeprom_address, code);

  frame[0] = 0xA7; // Encryption key. Doesn't matter much
  frame[1] =
      button
      << 4; // Which button did  you press? The 4 LSB will be the checksum
  frame[2] = code >> 8;        // Rolling code (big endian)
  frame[3] = code;             // Rolling code
  frame[4] = remote->id >> 16; // Remote address
  frame[5] = remote->id >> 8;  // Remote address
  frame[6] = remote->id;       // Remote address

#ifdef DEBUG_ENABLED
  Serial.print("Frame         : ");
  print_frame(frame);
#endif

  // Checksum calculation: a XOR of all the nibbles
  byte checksum = 0;
  for (byte i = 0; i < 7; i++) {
    checksum = checksum ^ frame[i] ^ (frame[i] >> 4);
  }
  checksum &= 0b1111; // We keep the last 4 bits only

  // Checksum integration
  frame[1] |= checksum; //  If a XOR of all the nibbles is equal to 0, the
                        //  blinds will consider the checksum ok.

#ifdef DEBUG_ENABLED
  Serial.println("");
  Serial.print("With checksum : ");
  print_frame(frame);
#endif

  // Obfuscation: a XOR of all the bytes
  for (byte i = 1; i < 7; i++) {
    frame[i] ^= frame[i - 1];
  }

#ifdef DEBUG_ENABLED
  Serial.println("");
  Serial.print("Obfuscated    : ");
  print_frame(frame);

  Serial.println("");
  Serial.print("Rolling Code  : ");
  Serial.println(code);
#endif

  EEPROM.put(remote->eeprom_address, code + 1);
  EEPROM.commit();
}

void send_frame(byte *frame, byte sync) {
  if (sync == 2) { // Only with the first frame.
    // Wake-up pulse & Silence
    SIG_HIGH;
    delayMicroseconds(9415);
    SIG_LOW;
    delayMicroseconds(89565);
  }

  // Hardware sync: two sync for the first frame, seven for the following
  // ones.
  for (int i = 0; i < sync; i++) {
    SIG_HIGH;
    delayMicroseconds(4 * SYMBOL);
    SIG_LOW;
    delayMicroseconds(4 * SYMBOL);
  }

  // Software sync
  SIG_HIGH;
  delayMicroseconds(4550);
  SIG_LOW;
  delayMicroseconds(SYMBOL);

  // Data: bits are sent one by one, starting with the MSB.
  for (byte i = 0; i < 56; i++) {
    if (((frame[i / 8] >> (7 - (i % 8))) & 1) == 1) {
      SIG_LOW;
      delayMicroseconds(SYMBOL);
      SIG_HIGH;
      delayMicroseconds(SYMBOL);
    } else {
      SIG_HIGH;
      delayMicroseconds(SYMBOL);
      SIG_LOW;
      delayMicroseconds(SYMBOL);
    }
  }

  SIG_LOW;
  delayMicroseconds(30415); // Inter-frame silence
}

/** CHACON DIO **/
void dio_send_command(char command, const remote_t *remote) {
  if (command == '0') {
    Serial.println("COMMAND - 0");
    for (int i = 0; i < 5; i++) {
      dio_transmit(remote->sender, remote->interruptor, 0);
      delay(10);
    }
  } else if (command == '1') {
    Serial.println("COMMAND - 1");
    for (int i = 0; i < 5; i++) {
      dio_transmit(remote->sender, remote->interruptor, 1);
      delay(10);
    }
  }

  Serial.println("");
}

void dio_send_bit(bool b) {
  if (b) {
    digitalWrite(PORT_TX, HIGH);
    delayMicroseconds(310); // 275 originally, but tweaked.
    digitalWrite(PORT_TX, LOW);
    delayMicroseconds(1340); // 1225 originally, but tweaked.
  } else {
    digitalWrite(PORT_TX, HIGH);
    delayMicroseconds(310); // 275 originally, but tweaked.
    digitalWrite(PORT_TX, LOW);
    delayMicroseconds(310); // 275 originally, but tweaked.
  }
}

void dio_send_pair(bool b) {
  dio_send_bit(b);
  dio_send_bit(!b);
}

void dio_transmit(unsigned long sender, int interruptor, int bln_on) {
  int i;

  digitalWrite(PORT_TX, HIGH);
  delayMicroseconds(275);
  digitalWrite(PORT_TX, LOW);
  delayMicroseconds(9900);     // first lock
  digitalWrite(PORT_TX, HIGH); // high again
  delayMicroseconds(275);      // wait
  digitalWrite(PORT_TX, LOW);  // second lock
  delayMicroseconds(2675);
  digitalWrite(PORT_TX, HIGH);

  // Emiter ID
  for (i = 0; i < 26; i++) {
    dio_send_pair((sender >> (25 - i)) & 0b1);
  }

  // 26th bit -- grouped command
  dio_send_pair(false);

  // 27th bit -- On or off
  dio_send_pair(bln_on);

  // 4 last bits -- reactor code 0000 -&gt; 0 -- 0001 -&gt; 1
  for (i = 0; i < 4; i++) {
    dio_send_pair((interruptor >> (3 - i)) & 1);
  }

  digitalWrite(PORT_TX, HIGH); // lock - end of data
  delayMicroseconds(275);      // wait
  digitalWrite(PORT_TX, LOW);  // lock - end of signal
}

/** LED **/
void wait_and_blink_led(int time_ms, int delay_ms) {
  int total_time = 0;
  bool state = LOW;
  while (total_time < time_ms) {
    state = !state;
    digitalWrite(LED_BUILTIN, state);
    delay(delay_ms);
    total_time = total_time + delay_ms;
  }
  led_off();
  return;
}

void led_off() { digitalWrite(LED_BUILTIN, HIGH); }

void led_on() { digitalWrite(LED_BUILTIN, LOW); }

void led_toggle() {
  bool state = digitalRead(LED_BUILTIN);
  digitalWrite(LED_BUILTIN, !state);
}
