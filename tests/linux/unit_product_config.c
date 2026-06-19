/*
 * unit_product_config — unit tests for product_config.c
 *
 * Build:
 *   make -C tests/linux unit_product_config
 *
 * Run:
 *   ./tests/linux/out/unit_product_config
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdint.h>

#include "../../app/src/product_config.h"

/* ── test harness ──────────────────────────────────────────────────────── */

static int tests_run    = 0;
static int tests_passed = 0;
static int tests_failed = 0;
static int fail_before;

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
    printf("  %-50s ", #fn);                                            \
    product_config_init();                                              \
    fn();                                                               \
    printf("%s\n", tests_failed == 0 ? "ok" : "FAIL");                 \
} while (0)

/* ── tests ─────────────────────────────────────────────────────────────── */

static void test_init_zeroes_peers(void)
{
    /* After init, all slots should be zeroed. */
    field_bridge_peer_t p;
    for (int i = 0; i < FIELD_BRIDGE_PEER_MAX; i++) {
        int rc = product_config_get_peer(i, &p);
        CHECK(rc == 0);
        CHECK(p.name[0] == '\0');
        CHECK(p.host[0] == '\0');
        CHECK(p.mqtt_port == 0);
        CHECK(p.p2p_port  == 0);
        CHECK(p.enabled   == 0);
    }
}

static void test_set_and_get_peer(void)
{
    field_bridge_peer_t in = {
        .name      = "note2",
        .host      = "192.168.1.102",
        .mqtt_port = 1883,
        .p2p_port  = 4884,
        .enabled   = 1,
    };
    int rc = product_config_set_peer(0, &in);
    CHECK(rc == 0);

    field_bridge_peer_t out;
    rc = product_config_get_peer(0, &out);
    CHECK(rc == 0);
    CHECK(strcmp(out.name, "note2") == 0);
    CHECK(strcmp(out.host, "192.168.1.102") == 0);
    CHECK(out.mqtt_port == 1883);
    CHECK(out.p2p_port  == 4884);
    CHECK(out.enabled   == 1);
}

static void test_set_all_slots(void)
{
    /* All FIELD_BRIDGE_PEER_MAX slots must accept a write. */
    for (int i = 0; i < FIELD_BRIDGE_PEER_MAX; i++) {
        field_bridge_peer_t in;
        snprintf(in.name, sizeof(in.name), "peer%d", i);
        snprintf(in.host, sizeof(in.host), "10.0.0.%d", i + 1);
        in.mqtt_port = (uint16_t)(1883 + i);
        in.p2p_port  = (uint16_t)(4884 + i);
        in.enabled   = 1;
        CHECK(product_config_set_peer(i, &in) == 0);
    }
    /* Verify all values survive. */
    for (int i = 0; i < FIELD_BRIDGE_PEER_MAX; i++) {
        field_bridge_peer_t out;
        CHECK(product_config_get_peer(i, &out) == 0);
        CHECK(out.mqtt_port == (uint16_t)(1883 + i));
    }
}

static void test_set_overwrite(void)
{
    field_bridge_peer_t a = { .name = "a", .host = "1.1.1.1",
                               .mqtt_port = 1883, .enabled = 1 };
    field_bridge_peer_t b = { .name = "b", .host = "2.2.2.2",
                               .mqtt_port = 1884, .enabled = 0 };
    product_config_set_peer(0, &a);
    product_config_set_peer(0, &b);

    field_bridge_peer_t out;
    product_config_get_peer(0, &out);
    CHECK(strcmp(out.name, "b") == 0);
    CHECK(out.mqtt_port == 1884);
    CHECK(out.enabled   == 0);
}

static void test_get_out_of_range(void)
{
    field_bridge_peer_t p;
    CHECK(product_config_get_peer(-1,                    &p) == -1);
    CHECK(product_config_get_peer(FIELD_BRIDGE_PEER_MAX, &p) == -1);
    CHECK(product_config_get_peer(FIELD_BRIDGE_PEER_MAX + 99, &p) == -1);
}

static void test_set_out_of_range(void)
{
    field_bridge_peer_t p = { .name = "x", .host = "0.0.0.0",
                               .mqtt_port = 1, .enabled = 1 };
    CHECK(product_config_set_peer(-1,                    &p) == -1);
    CHECK(product_config_set_peer(FIELD_BRIDGE_PEER_MAX, &p) == -1);
}

static void test_get_null_out(void)
{
    CHECK(product_config_get_peer(0, NULL) == -1);
}

static void test_set_null_peer(void)
{
    CHECK(product_config_set_peer(0, NULL) == -1);
}

static void test_peer_count(void)
{
    int count = product_config_peer_count();
    CHECK(count == FIELD_BRIDGE_PEER_MAX);
    CHECK(count > 0);
}

static void test_name_boundary(void)
{
    /* A name that exactly fills the buffer (31 chars + NUL) should round-trip. */
    field_bridge_peer_t in;
    memset(in.name, 'A', FIELD_BRIDGE_NAME_MAX - 1);
    in.name[FIELD_BRIDGE_NAME_MAX - 1] = '\0';
    strcpy(in.host, "1.2.3.4");
    in.mqtt_port = 1883;
    in.p2p_port  = 4884;
    in.enabled   = 1;

    CHECK(product_config_set_peer(0, &in) == 0);
    field_bridge_peer_t out;
    CHECK(product_config_get_peer(0, &out) == 0);
    CHECK(strcmp(out.name, in.name) == 0);
}

static void test_disable_peer(void)
{
    field_bridge_peer_t in = { .name = "n3", .host = "10.0.0.3",
                                .mqtt_port = 1883, .p2p_port = 4884,
                                .enabled = 1 };
    product_config_set_peer(2, &in);
    in.enabled = 0;
    product_config_set_peer(2, &in);

    field_bridge_peer_t out;
    product_config_get_peer(2, &out);
    CHECK(out.enabled == 0);
    /* Other fields should still be intact. */
    CHECK(strcmp(out.host, "10.0.0.3") == 0);
}

/* Persistence tests use an isolated temp file and manage their own init. */
#ifndef __ZEPHYR__
#include <stdlib.h>
#define PERSIST_FILE "/tmp/mqtt_bridge_test_peers.bin"

#define RUN_PERSIST(fn) do {                                                \
    fail_before = tests_failed;                                             \
    printf("  %-50s ", #fn);                                                \
    unsetenv("BRIDGE_PEERS_FILE");                                          \
    remove(PERSIST_FILE);                                                   \
    setenv("BRIDGE_PEERS_FILE", PERSIST_FILE, 1);                          \
    product_config_init();                                                  \
    fn();                                                                   \
    remove(PERSIST_FILE);                                                   \
    unsetenv("BRIDGE_PEERS_FILE");                                          \
    printf("%s\n", (tests_failed == fail_before) ? "ok" : "FAIL");         \
} while (0)

static void test_persist_survives_reinit(void)
{
    /* Set a peer, re-init (simulating reboot), verify it survived. */
    field_bridge_peer_t in = {
        .name = "persist_test", .host = "172.16.0.1",
        .mqtt_port = 1883, .p2p_port = 4884, .enabled = 1,
    };
    CHECK(product_config_set_peer(0, &in) == 0);

    product_config_init();   /* simulate reboot */

    field_bridge_peer_t out;
    CHECK(product_config_get_peer(0, &out) == 0);
    CHECK(strcmp(out.name, "persist_test") == 0);
    CHECK(strcmp(out.host, "172.16.0.1") == 0);
    CHECK(out.mqtt_port == 1883);
    CHECK(out.p2p_port  == 4884);
    CHECK(out.enabled   == 1);
}

static void test_persist_all_slots_survive_reinit(void)
{
    for (int i = 0; i < FIELD_BRIDGE_PEER_MAX; i++) {
        field_bridge_peer_t in;
        snprintf(in.name, sizeof(in.name), "node%d", i);
        snprintf(in.host, sizeof(in.host), "10.0.0.%d", i + 1);
        in.mqtt_port = (uint16_t)(1883 + i);
        in.p2p_port  = 4884;
        in.enabled   = 1;
        CHECK(product_config_set_peer(i, &in) == 0);
    }

    product_config_init();   /* simulate reboot */

    for (int i = 0; i < FIELD_BRIDGE_PEER_MAX; i++) {
        field_bridge_peer_t out;
        CHECK(product_config_get_peer(i, &out) == 0);
        CHECK(out.mqtt_port == (uint16_t)(1883 + i));
        CHECK(out.enabled == 1);
    }
}
#endif /* !__ZEPHYR__ */

/* ── main ───────────────────────────────────────────────────────────────── */

int main(void)
{
#ifndef __ZEPHYR__
    /* Isolate existing tests from any leftover persist file. */
    setenv("BRIDGE_PEERS_FILE", "/dev/null", 1);
#endif
    printf("=== unit_product_config ===\n");

    RUN(test_init_zeroes_peers);
    RUN(test_set_and_get_peer);
    RUN(test_set_all_slots);
    RUN(test_set_overwrite);
    RUN(test_get_out_of_range);
    RUN(test_set_out_of_range);
    RUN(test_get_null_out);
    RUN(test_set_null_peer);
    RUN(test_peer_count);
    RUN(test_name_boundary);
    RUN(test_disable_peer);

#ifndef __ZEPHYR__
    RUN_PERSIST(test_persist_survives_reinit);
    RUN_PERSIST(test_persist_all_slots_survive_reinit);
#endif

    printf("\n%d/%d tests passed", tests_passed, tests_run);
    if (tests_failed)
        printf("  (%d FAILED)", tests_failed);
    printf("\n");

    return tests_failed ? 1 : 0;
}
