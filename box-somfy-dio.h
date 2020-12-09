#ifndef BOX_SOMFY_DIO_H
#define BOX_SOMFY_DIO_H

#include <Arduino.h>

struct somfy_config_remote_t {
  const char *mqtt_topic_base;
  const char *description;
};

struct config_group_t {
  const char *mqtt_topic_base;
  const char *description;
  const int *remote_indexes;
};

struct dio_config_remote_t {
  const char *mqtt_topic_base;
  const char *description;
  unsigned long sender;
  unsigned long interruptor;
};

enum RemoteType { SomfyType, DioType };

struct remote_t {
  RemoteType type;
  bool is_group;
  unsigned int id;
  // Pointer to somfy_config_remote_t or somfy_config_group_t field (not
  // allocated)
  const char *mqtt_topic_base;
  // Pointer to somfy_config_remote_t or somfy_config_group_t field (not
  // allocated)
  const char *description;
  char *mqtt_topic_set;
  char *mqtt_topic_state;
  // For SomfyType only
  uint32_t eeprom_address;
  // For SomfyGroupType only (pointer not allocated)
  const int *remote_indexes;
  // For Chacon Dio
  unsigned long sender;
  unsigned long interruptor;
};

#endif
