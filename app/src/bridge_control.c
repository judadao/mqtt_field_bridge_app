#include <string.h>
#include <zephyr/logging/log.h>

#include "bridge_control.h"
#include "product_config.h"

LOG_MODULE_REGISTER(bridge_control, LOG_LEVEL_INF);

void bridge_control_init(void)
{
    LOG_INF("bridge control initialized");
    bridge_control_apply_peers();
}

int bridge_control_apply_peers(void)
{
    int enabled = 0;

    for (int i = 0; i < FIELD_BRIDGE_PEER_MAX; i++) {
        field_bridge_peer_t peer;

        if (product_config_get_peer(i, &peer) != 0) {
            continue;
        }
        if (!peer.enabled) {
            continue;
        }
        if (peer.host[0] == '\0') {
            LOG_WRN("peer %d: enabled but host empty, skipping", i);
            continue;
        }
        if (peer.mqtt_port == 0 || peer.p2p_port == 0) {
            LOG_WRN("peer %d: zero port (mqtt=%u p2p=%u), skipping",
                    i, peer.mqtt_port, peer.p2p_port);
            continue;
        }
        LOG_INF("peer %d: %s mqtt=%u p2p=%u",
                i, peer.host, peer.mqtt_port, peer.p2p_port);
        enabled++;
    }

    LOG_INF("apply_peers: %d peer(s) active", enabled);
    return enabled;
}
