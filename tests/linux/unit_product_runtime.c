/*
 * unit_product_runtime — unit tests for product_runtime.c.
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <arpa/inet.h>

#include "../../app/src/product_runtime.h"
#include "../../deps/mqtt_min_broker/include/p2p.h"

static int tests_run    = 0;
static int tests_passed = 0;
static int tests_failed = 0;
static int fail_before;
static p2p_peer_snapshot_t fake_peers[P2P_PEER_MAX];
static int fake_peer_count;

int p2p_peer_snapshot(p2p_peer_snapshot_t *out, int max)
{
    int n = fake_peer_count;
    if (!out || max <= 0) return 0;
    if (n > max) n = max;
    memcpy(out, fake_peers, (size_t)n * sizeof(out[0]));
    return n;
}

#define CHECK(expr) do {                                                \
    tests_run++;                                                        \
    if (expr) {                                                         \
        tests_passed++;                                                 \
    } else {                                                            \
        tests_failed++;                                                 \
        fprintf(stderr, "FAIL  %s:%d  %s\n", __FILE__, __LINE__, #expr);\
    }                                                                   \
} while (0)

#define RUN(fn) do {                                                    \
    fail_before = tests_failed;                                         \
    printf("  %-50s ", #fn);                                            \
    memset(fake_peers, 0, sizeof(fake_peers));                           \
    fake_peer_count = 0;                                                 \
    product_config_init();                                               \
    product_runtime_init();                                             \
    fn();                                                               \
    printf("%s\n", tests_failed == fail_before ? "ok" : "FAIL");      \
} while (0)

static void test_defaults(void)
{
    field_bridge_runtime_status_t st;
    CHECK(product_runtime_network_ready() == 0);
    CHECK(product_runtime_get_status(&st) == 0);
    CHECK(strcmp(st.network_state, "init") == 0);
    CHECK(strcmp(st.ip_addr, "0.0.0.0") == 0);
    CHECK(strcmp(st.broker_state, "stopped") == 0);
    CHECK(strcmp(st.p2p_role, "unknown") == 0);
    CHECK(st.connected_peers == 0);
    CHECK(st.remote_subscriptions == 0);
}

static void test_network_start_static_default(void)
{
    field_bridge_settings_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    strcpy(cfg.network.device_ip, "192.168.127.4");
    cfg.network.dhcp_enabled = 0;

    product_runtime_network_start(&cfg);
    CHECK(product_runtime_network_ready() == 1);

    field_bridge_runtime_status_t st;
    CHECK(product_runtime_get_status(&st) == 0);
    CHECK(strcmp(st.network_state, "static") == 0);
    CHECK(strcmp(st.ip_addr, "192.168.127.4") == 0);
    CHECK(st.last_error[0] == '\0');
}

static void test_network_start_dhcp(void)
{
    field_bridge_settings_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    strcpy(cfg.network.device_ip, "10.0.0.20");
    cfg.network.dhcp_enabled = 1;

    product_runtime_network_start(&cfg);
    CHECK(product_runtime_network_ready() == 1);

    field_bridge_runtime_status_t st;
    CHECK(product_runtime_get_status(&st) == 0);
    CHECK(strcmp(st.network_state, "dhcp") == 0);
    CHECK(strcmp(st.ip_addr, "10.0.0.20") == 0);
}

static void test_broker_state_transitions(void)
{
    field_bridge_runtime_status_t st;

    CHECK(product_runtime_set_broker_enabled(1) == 0);
    CHECK(product_runtime_get_status(&st) == 0);
    CHECK(strcmp(st.broker_state, "requested") == 0);

    product_runtime_broker_started();
    CHECK(product_runtime_get_status(&st) == 0);
    CHECK(strcmp(st.broker_state, "running") == 0);
    CHECK(strcmp(st.p2p_role, "dynamic") == 0);

    CHECK(product_runtime_set_broker_enabled(0) == 0);
    CHECK(product_runtime_get_status(&st) == 0);
    CHECK(strcmp(st.broker_state, "stopped") == 0);
    CHECK(product_runtime_set_broker_enabled(2) == -1);
}

static void test_broker_failed(void)
{
    field_bridge_runtime_status_t st;

    product_runtime_broker_failed("bind failed");
    CHECK(product_runtime_get_status(&st) == 0);
    CHECK(strcmp(st.broker_state, "error") == 0);
    CHECK(strcmp(st.last_error, "bind failed") == 0);
}

static void test_publish_test_record(void)
{
    field_bridge_publish_test_t in = {
        .topic = "site/field-a/test",
        .payload = "hello",
        .qos = 1,
        .retain = 0,
    };
    field_bridge_publish_test_t out;

    CHECK(product_runtime_record_publish_test(&in) == 0);
    CHECK(product_runtime_get_last_publish_test(&out) == 0);
    CHECK(strcmp(out.topic, "site/field-a/test") == 0);
    CHECK(strcmp(out.payload, "hello") == 0);
    CHECK(out.qos == 1);
}

static void test_publish_test_rejects_invalid(void)
{
    field_bridge_publish_test_t in;
    memset(&in, 0, sizeof(in));
    strcpy(in.payload, "hello");
    CHECK(product_runtime_record_publish_test(&in) == -1);

    strcpy(in.topic, "site/field-a/test");
    in.qos = 2;
    CHECK(product_runtime_record_publish_test(&in) == -1);
}

static void test_peer_status_disabled_and_disconnected(void)
{
    field_bridge_peer_status_t statuses[FIELD_BRIDGE_PEER_MAX];
    int n = product_runtime_get_peer_statuses(statuses, FIELD_BRIDGE_PEER_MAX);

    CHECK(n == FIELD_BRIDGE_PEER_MAX);
    CHECK(strcmp(statuses[0].state, "disabled") == 0);

    field_bridge_peer_t p = {
        .name = "node2", .host = "192.168.1.2",
        .mqtt_port = 1883, .p2p_port = 4884, .enabled = 1,
    };
    CHECK(product_config_set_peer(0, &p) == 0);
    n = product_runtime_get_peer_statuses(statuses, FIELD_BRIDGE_PEER_MAX);
    CHECK(n == FIELD_BRIDGE_PEER_MAX);
    CHECK(strcmp(statuses[0].state, "disconnected") == 0);
    CHECK(strcmp(statuses[0].last_error, "not present in p2p snapshot") == 0);
}

static void test_peer_status_connected(void)
{
    field_bridge_peer_t p = {
        .name = "node2", .host = "192.168.1.2",
        .mqtt_port = 1883, .p2p_port = 4884, .enabled = 1,
    };
    CHECK(product_config_set_peer(0, &p) == 0);
    CHECK(inet_pton(AF_INET, "192.168.1.2", &fake_peers[0].addr) == 1);
    fake_peers[0].p2p_port = 4884;
    fake_peer_count = 1;

    field_bridge_peer_status_t statuses[FIELD_BRIDGE_PEER_MAX];
    int n = product_runtime_get_peer_statuses(statuses, FIELD_BRIDGE_PEER_MAX);
    CHECK(n == FIELD_BRIDGE_PEER_MAX);
    CHECK(strcmp(statuses[0].state, "connected") == 0);
    CHECK(statuses[0].last_error[0] == '\0');
}

static void test_peer_status_connecting(void)
{
    field_bridge_peer_t p = {
        .name = "node2", .host = "192.168.1.2",
        .mqtt_port = 1883, .p2p_port = 4884, .enabled = 1,
    };
    CHECK(product_config_set_peer(0, &p) == 0);
    CHECK(product_runtime_set_broker_enabled(1) == 0);

    field_bridge_peer_status_t statuses[FIELD_BRIDGE_PEER_MAX];
    int n = product_runtime_get_peer_statuses(statuses, FIELD_BRIDGE_PEER_MAX);
    CHECK(n == FIELD_BRIDGE_PEER_MAX);
    CHECK(strcmp(statuses[0].state, "connecting") == 0);
    CHECK(strcmp(statuses[0].last_error, "waiting for p2p snapshot") == 0);
}

static void test_peer_status_hostname_unknown(void)
{
    field_bridge_peer_t p = {
        .name = "node2", .host = "node2.local",
        .mqtt_port = 1883, .p2p_port = 4884, .enabled = 1,
    };
    CHECK(product_config_set_peer(0, &p) == 0);

    field_bridge_peer_status_t statuses[FIELD_BRIDGE_PEER_MAX];
    int n = product_runtime_get_peer_statuses(statuses, FIELD_BRIDGE_PEER_MAX);
    CHECK(n == FIELD_BRIDGE_PEER_MAX);
    CHECK(strcmp(statuses[0].state, "unknown") == 0);
    CHECK(strcmp(statuses[0].last_error, "host is not an IPv4 snapshot key") == 0);
}

int main(void)
{
    printf("=== unit_product_runtime ===\n");
    setenv("BRIDGE_PEERS_FILE", "/dev/null", 1);
    setenv("BRIDGE_SETTINGS_FILE", "/dev/null", 1);

    RUN(test_defaults);
    RUN(test_network_start_static_default);
    RUN(test_network_start_dhcp);
    RUN(test_broker_state_transitions);
    RUN(test_broker_failed);
    RUN(test_publish_test_record);
    RUN(test_publish_test_rejects_invalid);
    RUN(test_peer_status_disabled_and_disconnected);
    RUN(test_peer_status_connected);
    RUN(test_peer_status_connecting);
    RUN(test_peer_status_hostname_unknown);

    printf("\n%d/%d tests passed", tests_passed, tests_run);
    if (tests_failed) printf("  (%d FAILED)", tests_failed);
    printf("\n");
    return tests_failed ? 1 : 0;
}
