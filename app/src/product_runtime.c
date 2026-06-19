#include <string.h>
#include <stdio.h>

#include <zephyr/logging/log.h>

#include "product_runtime.h"

#include "p2p.h"

LOG_MODULE_REGISTER(product_runtime, LOG_LEVEL_INF);

static field_bridge_runtime_status_t runtime_status;
static field_bridge_publish_test_t last_publish;

#ifndef __ZEPHYR__
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
    copy_str(runtime_status.wifi_state, sizeof(runtime_status.wifi_state), "init");
    copy_str(runtime_status.ip_addr, sizeof(runtime_status.ip_addr), "0.0.0.0");
    copy_str(runtime_status.broker_state, sizeof(runtime_status.broker_state), "stopped");
    copy_str(runtime_status.p2p_role, sizeof(runtime_status.p2p_role), "unknown");
    LOG_INF("product runtime initialized");
}

void product_runtime_network_start(const field_bridge_settings_t *settings)
{
    if (!settings) {
        copy_str(runtime_status.wifi_state, sizeof(runtime_status.wifi_state), "error");
        copy_str(runtime_status.last_error, sizeof(runtime_status.last_error),
                 "network settings missing");
        return;
    }

    if (settings->network.wifi_ssid[0] != '\0') {
        copy_str(runtime_status.wifi_state, sizeof(runtime_status.wifi_state), "configured");
    } else {
        copy_str(runtime_status.wifi_state, sizeof(runtime_status.wifi_state), "ap");
    }
    copy_str(runtime_status.ip_addr, sizeof(runtime_status.ip_addr),
             settings->network.device_ip[0] ? settings->network.device_ip : "192.168.4.1");
    runtime_status.last_error[0] = '\0';
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

    p2p_peer_snapshot_t peers[P2P_PEER_MAX];
    runtime_status.connected_peers =
        (uint8_t)p2p_peer_snapshot(peers, P2P_PEER_MAX);
    p2p_router_stats_t stats;
    if (p2p_router_stats(&stats) == 0) {
        runtime_status.remote_subscriptions = stats.remote_subs;
    }

    *out = runtime_status;
    return 0;
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
