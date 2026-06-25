#ifndef PRODUCT_CONFIG_H
#define PRODUCT_CONFIG_H

#include <stdint.h>

#ifndef FIELD_BRIDGE_PEER_MAX
#define FIELD_BRIDGE_PEER_MAX 2
#endif
#define FIELD_BRIDGE_NAME_MAX 32
#define FIELD_BRIDGE_HOST_MAX 64
#define FIELD_BRIDGE_SITE_MAX 32
#define FIELD_BRIDGE_TOPIC_MAX 64

typedef struct {
    char name[FIELD_BRIDGE_NAME_MAX];
    char host[FIELD_BRIDGE_HOST_MAX];
    uint16_t mqtt_port;
    uint16_t p2p_port;
    uint8_t enabled;
} field_bridge_peer_t;

typedef struct {
    char device_name[FIELD_BRIDGE_NAME_MAX];
} field_bridge_system_settings_t;

typedef struct {
    char wifi_ssid[FIELD_BRIDGE_HOST_MAX];
    char wifi_password[FIELD_BRIDGE_HOST_MAX];
    char device_ip[FIELD_BRIDGE_HOST_MAX];
    char gateway[FIELD_BRIDGE_HOST_MAX];
    char netmask[FIELD_BRIDGE_HOST_MAX];
    char dns[FIELD_BRIDGE_HOST_MAX];
    uint8_t dhcp_enabled;
} field_bridge_network_settings_t;

typedef struct {
    char broker_ip[FIELD_BRIDGE_HOST_MAX];
    char site_id[FIELD_BRIDGE_SITE_MAX];
    char topic_prefix[FIELD_BRIDGE_TOPIC_MAX];
    uint16_t mqtt_port;
    uint16_t p2p_port;
    uint8_t broker_enabled;
    uint8_t bridge_enabled;
    uint8_t mesh_enabled;
} field_bridge_broker_settings_t;

typedef struct {
    field_bridge_system_settings_t system;
    field_bridge_network_settings_t network;
    field_bridge_broker_settings_t broker;
} field_bridge_settings_t;

typedef enum {
    FIELD_BRIDGE_PROFILE_SMALL = 0,
    FIELD_BRIDGE_PROFILE_LARGE = 1,
} field_bridge_defaults_profile_t;

void product_config_init(void);
int product_config_peer_count(void);
int product_config_get_peer(int index, field_bridge_peer_t *out);
int product_config_set_peer(int index, const field_bridge_peer_t *peer);
int product_config_get_settings(field_bridge_settings_t *out);
int product_config_set_settings(const field_bridge_settings_t *settings);
int product_config_reset_all(void);
int product_config_apply_defaults(field_bridge_defaults_profile_t profile);

#endif /* PRODUCT_CONFIG_H */
