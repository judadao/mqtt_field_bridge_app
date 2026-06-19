#ifndef PRODUCT_RUNTIME_H
#define PRODUCT_RUNTIME_H

#include <stdint.h>

#include "product_config.h"

#define FIELD_BRIDGE_STATE_MAX 16
#define FIELD_BRIDGE_ERROR_MAX 96
#define FIELD_BRIDGE_TOPIC_FULL_MAX 128
#define FIELD_BRIDGE_PAYLOAD_MAX 256

typedef struct {
    char wifi_state[FIELD_BRIDGE_STATE_MAX];
    char ip_addr[FIELD_BRIDGE_HOST_MAX];
    char broker_state[FIELD_BRIDGE_STATE_MAX];
    char p2p_role[FIELD_BRIDGE_STATE_MAX];
    uint8_t connected_peers;
    uint16_t remote_subscriptions;
    char last_error[FIELD_BRIDGE_ERROR_MAX];
} field_bridge_runtime_status_t;

typedef struct {
    char topic[FIELD_BRIDGE_TOPIC_FULL_MAX];
    char payload[FIELD_BRIDGE_PAYLOAD_MAX];
    uint8_t qos;
    uint8_t retain;
} field_bridge_publish_test_t;

typedef struct {
    uint8_t index;
    char name[FIELD_BRIDGE_NAME_MAX];
    char host[FIELD_BRIDGE_HOST_MAX];
    uint16_t p2p_port;
    uint8_t enabled;
    char state[FIELD_BRIDGE_STATE_MAX];
    char last_error[FIELD_BRIDGE_ERROR_MAX];
} field_bridge_peer_status_t;

void product_runtime_init(void);
void product_runtime_network_start(const field_bridge_settings_t *settings);
void product_runtime_broker_started(void);
void product_runtime_broker_failed(const char *error);
int product_runtime_set_broker_enabled(uint8_t enabled);
int product_runtime_get_status(field_bridge_runtime_status_t *out);
int product_runtime_get_peer_statuses(field_bridge_peer_status_t *out, int max);
int product_runtime_record_publish_test(const field_bridge_publish_test_t *test);
int product_runtime_get_last_publish_test(field_bridge_publish_test_t *out);

#endif /* PRODUCT_RUNTIME_H */
