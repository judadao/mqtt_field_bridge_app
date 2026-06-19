#include <string.h>
#include <stdint.h>
#include <zephyr/logging/log.h>

#include "bridge_control.h"
#include "product_config.h"
#include "p2p.h"

#ifndef __ZEPHYR__
#include <arpa/inet.h>
#define BRIDGE_INET_PTON(af, src, dst) inet_pton((af), (src), (dst))

__attribute__((weak)) void p2p_static_seed_clear(void)
{
}

__attribute__((weak)) int p2p_static_seed_add(uint32_t addr, uint16_t p2p_port)
{
    (void)addr;
    (void)p2p_port;
    return 0;
}
#else
#include <zephyr/net/socket.h>
#define BRIDGE_INET_PTON(af, src, dst) zsock_inet_pton((af), (src), (dst))
#endif

LOG_MODULE_REGISTER(bridge_control, LOG_LEVEL_INF);

void bridge_control_init(void)
{
    LOG_INF("bridge control initialized");
    bridge_control_apply_peers();
}

int bridge_control_apply_peers(void)
{
    int enabled = 0;

    p2p_static_seed_clear();

    for (int i = 0; i < FIELD_BRIDGE_PEER_MAX; i++) {
        field_bridge_peer_t peer;
        uint32_t addr;

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
        if (BRIDGE_INET_PTON(AF_INET, peer.host, &addr) != 1) {
            LOG_WRN("peer %d: host is not an IPv4 static seed: %s",
                    i, peer.host);
            continue;
        }
        if (p2p_static_seed_add(addr, peer.p2p_port) != 0) {
            LOG_WRN("peer %d: static seed table full or invalid", i);
            continue;
        }
        LOG_INF("peer %d: %s mqtt=%u p2p=%u",
                i, peer.host, peer.mqtt_port, peer.p2p_port);
        enabled++;
    }

    LOG_INF("apply_peers: %d peer(s) active", enabled);
    return enabled;
}
