#ifndef PRODUCT_CONFIG_H
#define PRODUCT_CONFIG_H

#include <stdint.h>

#ifndef FIELD_BRIDGE_PEER_MAX
#define FIELD_BRIDGE_PEER_MAX 10
#endif
#define FIELD_BRIDGE_NAME_MAX 32
#define FIELD_BRIDGE_HOST_MAX 64

typedef struct {
    char name[FIELD_BRIDGE_NAME_MAX];
    char host[FIELD_BRIDGE_HOST_MAX];
    uint16_t mqtt_port;
    uint16_t p2p_port;
    uint8_t enabled;
} field_bridge_peer_t;

void product_config_init(void);
int product_config_peer_count(void);
int product_config_get_peer(int index, field_bridge_peer_t *out);
int product_config_set_peer(int index, const field_bridge_peer_t *peer);

#endif /* PRODUCT_CONFIG_H */
