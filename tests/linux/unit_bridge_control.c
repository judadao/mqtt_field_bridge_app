/*
 * unit_bridge_control — unit tests for bridge_control.c
 *
 * Build:  make -C tests/linux unit_bridge_control
 * Run:    ./tests/linux/out/unit_bridge_control
 */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#include <netinet/in.h>

#include "../../app/src/bridge_control.h"
#include "../../app/src/product_config.h"
#include "../../deps/mqtt_min_broker/include/p2p.h"

static int tests_run    = 0;
static int tests_passed = 0;
static int tests_failed = 0;
static int fail_before;
static int seed_clear_count;
static int seed_add_count;
static uint16_t last_seed_port;

void p2p_static_seed_clear(void)
{
    seed_clear_count++;
    seed_add_count = 0;
    last_seed_port = 0;
}

int p2p_static_seed_add(uint32_t addr, uint16_t p2p_port)
{
    if (addr == 0 || p2p_port == 0) {
        return -1;
    }
    seed_add_count++;
    last_seed_port = p2p_port;
    return 0;
}

#define CHECK(expr) do {                                                    \
    tests_run++;                                                            \
    if (expr) { tests_passed++; }                                           \
    else {                                                                  \
        tests_failed++;                                                     \
        fprintf(stderr, "FAIL  %s:%d  %s\n", __FILE__, __LINE__, #expr);   \
    }                                                                       \
} while (0)

#define RUN(fn) do {                                                        \
    fail_before = tests_failed;                                             \
    printf("  %-55s ", #fn);                                                \
    product_config_init();                                                  \
    fn();                                                                   \
    printf("%s\n", (tests_failed == fail_before) ? "ok" : "FAIL");         \
} while (0)

static void test_apply_all_disabled(void)
{
    /* Default: all peers zeroed, enabled=0 → should return 0 */
    CHECK(bridge_control_apply_peers() == 0);
}

static void test_apply_one_enabled(void)
{
    field_bridge_peer_t p = {
        .name = "note2", .host = "192.168.1.2",
        .mqtt_port = 1883, .p2p_port = 4884, .enabled = 1,
    };
    product_config_set_peer(0, &p);
    CHECK(bridge_control_apply_peers() == 1);
    CHECK(seed_clear_count > 0);
    CHECK(seed_add_count == 1);
    CHECK(last_seed_port == 4884);
}

static void test_apply_hostname_not_static_seed(void)
{
    field_bridge_peer_t p = {
        .name = "dns", .host = "node2.local",
        .mqtt_port = 1883, .p2p_port = 4884, .enabled = 1,
    };
    product_config_set_peer(0, &p);
    CHECK(bridge_control_apply_peers() == 0);
    CHECK(seed_add_count == 0);
}

static void test_apply_empty_host_skipped(void)
{
    field_bridge_peer_t p = {
        .name = "x", .host = "",
        .mqtt_port = 1883, .p2p_port = 4884, .enabled = 1,
    };
    product_config_set_peer(0, &p);
    CHECK(bridge_control_apply_peers() == 0);
}

static void test_apply_zero_mqtt_port_skipped(void)
{
    field_bridge_peer_t p = {
        .name = "y", .host = "10.0.0.1",
        .mqtt_port = 0, .p2p_port = 4884, .enabled = 1,
    };
    product_config_set_peer(0, &p);
    CHECK(bridge_control_apply_peers() == 0);
}

static void test_apply_zero_p2p_port_skipped(void)
{
    field_bridge_peer_t p = {
        .name = "z", .host = "10.0.0.1",
        .mqtt_port = 1883, .p2p_port = 0, .enabled = 1,
    };
    product_config_set_peer(0, &p);
    CHECK(bridge_control_apply_peers() == 0);
}

static void test_apply_max_enabled(void)
{
    for (int i = 0; i < FIELD_BRIDGE_PEER_MAX; i++) {
        field_bridge_peer_t p;
        snprintf(p.name, sizeof(p.name), "peer%d", i);
        snprintf(p.host, sizeof(p.host), "10.0.0.%d", i + 1);
        p.mqtt_port = 1883;
        p.p2p_port  = 4884;
        p.enabled   = 1;
        product_config_set_peer(i, &p);
    }
    CHECK(bridge_control_apply_peers() == FIELD_BRIDGE_PEER_MAX);
}

static void test_apply_disabled_not_counted(void)
{
    field_bridge_peer_t a = { .name="a", .host="1.1.1.1",
                               .mqtt_port=1883, .p2p_port=4884, .enabled=1 };
    field_bridge_peer_t b = { .name="b", .host="2.2.2.2",
                               .mqtt_port=1883, .p2p_port=4884, .enabled=0 };
    product_config_set_peer(0, &a);
    product_config_set_peer(1, &b);
    CHECK(bridge_control_apply_peers() == 1);
}

int main(void)
{
    printf("=== unit_bridge_control ===\n");
#ifndef __ZEPHYR__
    /* Isolate tests from any leftover persist file. */
    setenv("BRIDGE_PEERS_FILE", "/dev/null", 1);
#endif
    RUN(test_apply_all_disabled);
    RUN(test_apply_one_enabled);
    RUN(test_apply_hostname_not_static_seed);
    RUN(test_apply_empty_host_skipped);
    RUN(test_apply_zero_mqtt_port_skipped);
    RUN(test_apply_zero_p2p_port_skipped);
    RUN(test_apply_max_enabled);
    RUN(test_apply_disabled_not_counted);

    printf("\n%d/%d tests passed", tests_passed, tests_run);
    if (tests_failed) printf("  (%d FAILED)", tests_failed);
    printf("\n");
    return tests_failed ? 1 : 0;
}
