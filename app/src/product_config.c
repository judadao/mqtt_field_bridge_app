#include <string.h>

#include <zephyr/logging/log.h>

#include "product_config.h"

LOG_MODULE_REGISTER(product_config, LOG_LEVEL_INF);

static field_bridge_peer_t peers[FIELD_BRIDGE_PEER_MAX];

void product_config_init(void)
{
    memset(peers, 0, sizeof(peers));
    LOG_INF("product config initialized");
}

int product_config_peer_count(void)
{
    return FIELD_BRIDGE_PEER_MAX;
}

int product_config_get_peer(int index, field_bridge_peer_t *out)
{
    if (!out || index < 0 || index >= FIELD_BRIDGE_PEER_MAX) {
        return -1;
    }

    *out = peers[index];
    return 0;
}

int product_config_set_peer(int index, const field_bridge_peer_t *peer)
{
    if (!peer || index < 0 || index >= FIELD_BRIDGE_PEER_MAX) {
        return -1;
    }

    peers[index] = *peer;
    return 0;
}
