#include <string.h>
#include <stdio.h>

#include <zephyr/logging/log.h>

#include "product_runtime.h"

#include "p2p.h"

#ifndef __ZEPHYR__
#include <arpa/inet.h>
#define RUNTIME_INET_PTON(af, src, dst) inet_pton((af), (src), (dst))
#else
#include <zephyr/net/socket.h>
#define RUNTIME_INET_PTON(af, src, dst) zsock_inet_pton((af), (src), (dst))
#endif

#if !defined(__ZEPHYR__) || defined(CONFIG_MQTT_P2P_DYNAMIC)
#define PRODUCT_RUNTIME_HAS_P2P 1
#endif

LOG_MODULE_REGISTER(product_runtime, LOG_LEVEL_INF);

static field_bridge_runtime_status_t runtime_status;
static field_bridge_publish_test_t last_publish;

#if !defined(__ZEPHYR__) || !defined(CONFIG_MQTT_P2P_DYNAMIC)
__attribute__((weak)) int p2p_peer_snapshot(p2p_peer_snapshot_t *out, int max)
{
    (void)out;
    (void)max;
    return 0;
}

__attribute__((weak)) int p2p_router_stats(p2p_router_stats_t *out)
{
    if (out) {
        memset(out, 0, sizeof(*out));
    }
    return 0;
}
#endif

static void copy_str(char *dst, size_t cap, const char *src)
{
    if (!dst || cap == 0) return;
    if (!src) src = "";
    snprintf(dst, cap, "%s", src);
}

void product_runtime_init(void)
{
    memset(&runtime_status, 0, sizeof(runtime_status));
    memset(&last_publish, 0, sizeof(last_publish));
    copy_str(runtime_status.network_state, sizeof(runtime_status.network_state), "init");
    copy_str(runtime_status.ip_addr, sizeof(runtime_status.ip_addr), "0.0.0.0");
    copy_str(runtime_status.broker_state, sizeof(runtime_status.broker_state), "stopped");
    copy_str(runtime_status.p2p_role, sizeof(runtime_status.p2p_role), "unknown");
    LOG_INF("product runtime initialized");
}

void product_runtime_network_start(const field_bridge_settings_t *settings)
{
    if (!settings) {
        copy_str(runtime_status.network_state, sizeof(runtime_status.network_state), "error");
        copy_str(runtime_status.last_error, sizeof(runtime_status.last_error),
                 "network settings missing");
        return;
    }

    copy_str(runtime_status.network_state, sizeof(runtime_status.network_state),
             settings->network.dhcp_enabled ? "dhcp" : "static");
    copy_str(runtime_status.ip_addr, sizeof(runtime_status.ip_addr),
             settings->network.device_ip[0] ? settings->network.device_ip : "0.0.0.0");
    runtime_status.last_error[0] = '\0';
}

int product_runtime_network_ready(void)
{
    return strcmp(runtime_status.network_state, "dhcp") == 0 ||
           strcmp(runtime_status.network_state, "static") == 0;
}

void product_runtime_broker_started(void)
{
    copy_str(runtime_status.broker_state, sizeof(runtime_status.broker_state), "running");
    copy_str(runtime_status.p2p_role, sizeof(runtime_status.p2p_role), "dynamic");
    runtime_status.last_error[0] = '\0';
}

void product_runtime_broker_failed(const char *error)
{
    copy_str(runtime_status.broker_state, sizeof(runtime_status.broker_state), "error");
    copy_str(runtime_status.last_error, sizeof(runtime_status.last_error), error);
}

int product_runtime_set_broker_enabled(uint8_t enabled)
{
    if (enabled > 1) {
        return -1;
    }
    copy_str(runtime_status.broker_state, sizeof(runtime_status.broker_state),
             enabled ? "requested" : "stopped");
    return 0;
}

int product_runtime_get_status(field_bridge_runtime_status_t *out)
{
    if (!out) {
        return -1;
    }

#if defined(PRODUCT_RUNTIME_HAS_P2P)
    if (strcmp(runtime_status.broker_state, "running") == 0 ||
        strcmp(runtime_status.broker_state, "requested") == 0) {
        p2p_peer_snapshot_t peers[P2P_PEER_MAX];
        runtime_status.connected_peers =
            (uint8_t)p2p_peer_snapshot(peers, P2P_PEER_MAX);
        p2p_router_stats_t stats;
        if (p2p_router_stats(&stats) == 0) {
            runtime_status.remote_subscriptions = stats.remote_subs;
        }
    } else {
        runtime_status.connected_peers = 0;
        runtime_status.remote_subscriptions = 0;
    }
#else
    runtime_status.connected_peers = 0;
    runtime_status.remote_subscriptions = 0;
#endif

    *out = runtime_status;
    return 0;
}

static int snapshot_contains_peer(const p2p_peer_snapshot_t *peers, int count,
                                  const field_bridge_peer_t *peer)
{
    uint32_t addr;

    if (RUNTIME_INET_PTON(AF_INET, peer->host, &addr) != 1) {
        return -1;
    }
    for (int i = 0; i < count; i++) {
        if (peers[i].addr == addr && peers[i].p2p_port == peer->p2p_port) {
            return 1;
        }
    }
    return 0;
}

int product_runtime_get_peer_statuses(field_bridge_peer_status_t *out, int max)
{
    int snapshot_count;
    int written = 0;

    if (!out || max <= 0) {
        return -1;
    }

#if defined(PRODUCT_RUNTIME_HAS_P2P)
    p2p_peer_snapshot_t snapshots[P2P_PEER_MAX];
    snapshot_count = p2p_peer_snapshot(snapshots, P2P_PEER_MAX);
#else
    snapshot_count = 0;
#endif
    for (int i = 0; i < FIELD_BRIDGE_PEER_MAX && written < max; i++) {
        field_bridge_peer_t peer;
        field_bridge_peer_status_t *status = &out[written];
        int match;

        if (product_config_get_peer(i, &peer) != 0) {
            continue;
        }

        memset(status, 0, sizeof(*status));
        status->index = (uint8_t)i;
        copy_str(status->name, sizeof(status->name), peer.name);
        copy_str(status->host, sizeof(status->host), peer.host);
        status->p2p_port = peer.p2p_port;
        status->enabled = peer.enabled;

        if (!peer.enabled) {
            copy_str(status->state, sizeof(status->state), "disabled");
            written++;
            continue;
        }
        if (peer.host[0] == '\0' || peer.p2p_port == 0) {
            copy_str(status->state, sizeof(status->state), "unknown");
            copy_str(status->last_error, sizeof(status->last_error),
                     "missing host or p2p port");
            written++;
            continue;
        }

        match = snapshot_contains_peer(snapshots, snapshot_count, &peer);
        if (match == 1) {
            copy_str(status->state, sizeof(status->state), "connected");
        } else if (match == 0) {
            if (strcmp(runtime_status.broker_state, "requested") == 0) {
                copy_str(status->state, sizeof(status->state), "connecting");
                copy_str(status->last_error, sizeof(status->last_error),
                         "waiting for p2p snapshot");
            } else {
                copy_str(status->state, sizeof(status->state), "disconnected");
                copy_str(status->last_error, sizeof(status->last_error),
                         "not present in p2p snapshot");
            }
        } else {
            copy_str(status->state, sizeof(status->state), "unknown");
            copy_str(status->last_error, sizeof(status->last_error),
                     "host is not an IPv4 snapshot key");
        }
        written++;
    }

    return written;
}

int product_runtime_record_publish_test(const field_bridge_publish_test_t *test)
{
    if (!test || test->topic[0] == '\0' || test->payload[0] == '\0' ||
        test->qos > 1 || test->retain > 1) {
        return -1;
    }

    last_publish = *test;
    return 0;
}

int product_runtime_get_last_publish_test(field_bridge_publish_test_t *out)
{
    if (!out) {
        return -1;
    }

    *out = last_publish;
    return 0;
}
