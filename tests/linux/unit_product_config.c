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
                               .mqtt_port = 1883, .p2p_port = 4884,
                               .enabled = 1 };
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

static void test_settings_defaults(void)
{
    field_bridge_settings_t s;
    CHECK(product_config_get_settings(&s) == 0);
    CHECK(strcmp(s.system.device_name, "esp32-min-broker") == 0);
    CHECK(strcmp(s.system.admin_password, "admin") == 0);
    CHECK(strcmp(s.network.ap_ssid, "ESP32-Min-Broker") == 0);
    CHECK(strcmp(s.network.device_ip, "192.168.4.1") == 0);
    CHECK(strcmp(s.network.gateway, "192.168.4.1") == 0);
    CHECK(strcmp(s.network.netmask, "255.255.255.0") == 0);
    CHECK(strcmp(s.network.dns, "192.168.4.1") == 0);
    CHECK(s.network.dhcp_enabled == 1);
    CHECK(strcmp(s.broker.site_id, "field-a") == 0);
    CHECK(strcmp(s.broker.topic_prefix, "site/field-a") == 0);
    CHECK(s.broker.mqtt_port == 1883);
    CHECK(s.broker.p2p_port == 4884);
    CHECK(s.broker.mesh_enabled == 1);
    CHECK(product_config_check_admin_password("admin") == 1);
    CHECK(product_config_check_admin_password("bad") == 0);
}

static void test_settings_set_and_get(void)
{
    field_bridge_settings_t in;
    memset(&in, 0, sizeof(in));
    strcpy(in.system.device_name, "field-node-1");
    strcpy(in.system.admin_password, "secret");
    strcpy(in.network.wifi_ssid, "plant-wifi");
    strcpy(in.network.wifi_password, "wifi-pass");
    strcpy(in.network.ap_ssid, "node-setup");
    strcpy(in.network.ap_password, "setup-pass");
    strcpy(in.network.device_ip, "192.168.10.1");
    strcpy(in.network.gateway, "192.168.10.254");
    strcpy(in.network.netmask, "255.255.255.0");
    strcpy(in.network.dns, "8.8.8.8");
    in.network.dhcp_enabled = 0;
    strcpy(in.broker.site_id, "site-b");
    strcpy(in.broker.topic_prefix, "site/site-b");
    in.broker.mqtt_port = 1884;
    in.broker.p2p_port = 4885;
    in.broker.broker_enabled = 1;
    in.broker.bridge_enabled = 0;
    in.broker.mesh_enabled = 1;

    CHECK(product_config_set_settings(&in) == 0);

    field_bridge_settings_t out;
    CHECK(product_config_get_settings(&out) == 0);
    CHECK(strcmp(out.system.device_name, "field-node-1") == 0);
    CHECK(strcmp(out.network.wifi_ssid, "plant-wifi") == 0);
    CHECK(strcmp(out.network.device_ip, "192.168.10.1") == 0);
    CHECK(strcmp(out.network.gateway, "192.168.10.254") == 0);
    CHECK(strcmp(out.network.netmask, "255.255.255.0") == 0);
    CHECK(strcmp(out.network.dns, "8.8.8.8") == 0);
    CHECK(out.network.dhcp_enabled == 0);
    CHECK(strcmp(out.broker.site_id, "site-b") == 0);
    CHECK(out.broker.mqtt_port == 1884);
    CHECK(out.broker.bridge_enabled == 0);
    CHECK(product_config_check_admin_password("secret") == 1);
}

static void test_settings_null_args(void)
{
    CHECK(product_config_get_settings(NULL) == -1);
    CHECK(product_config_set_settings(NULL) == -1);
    CHECK(product_config_check_admin_password(NULL) == 0);
}

static void test_reject_invalid_enabled_peer(void)
{
    field_bridge_peer_t p = { .name = "bad", .host = "1.2.3.4",
                               .mqtt_port = 1883, .p2p_port = 4884,
                               .enabled = 2 };
    CHECK(product_config_set_peer(0, &p) == -1);
}

static void test_reject_enabled_peer_with_missing_fields(void)
{
    field_bridge_peer_t p = { .name = "bad", .host = "",
                               .mqtt_port = 1883, .p2p_port = 4884,
                               .enabled = 1 };
    CHECK(product_config_set_peer(0, &p) == -1);
    strcpy(p.host, "1.2.3.4");
    p.mqtt_port = 0;
    CHECK(product_config_set_peer(0, &p) == -1);
    p.mqtt_port = 1883;
    p.p2p_port = 0;
    CHECK(product_config_set_peer(0, &p) == -1);
}

static void test_reject_invalid_settings(void)
{
    field_bridge_settings_t s;
    CHECK(product_config_get_settings(&s) == 0);
    s.system.admin_password[0] = '\0';
    CHECK(product_config_set_settings(&s) == -1);

    CHECK(product_config_get_settings(&s) == 0);
    s.broker.mqtt_port = 0;
    CHECK(product_config_set_settings(&s) == -1);

    CHECK(product_config_get_settings(&s) == 0);
    s.network.dhcp_enabled = 2;
    CHECK(product_config_set_settings(&s) == -1);

    CHECK(product_config_get_settings(&s) == 0);
    s.network.dns[0] = '\0';
    CHECK(product_config_set_settings(&s) == -1);
}

static void test_reset_all_restores_defaults_and_clears_peers(void)
{
    field_bridge_peer_t p = { .name = "peer", .host = "1.2.3.4",
                               .mqtt_port = 1883, .p2p_port = 4884,
                               .enabled = 1 };
    CHECK(product_config_set_peer(0, &p) == 0);

    field_bridge_settings_t s;
    CHECK(product_config_get_settings(&s) == 0);
    strcpy(s.system.device_name, "custom");
    CHECK(product_config_set_settings(&s) == 0);

    CHECK(product_config_reset_all() == 0);

    CHECK(product_config_get_peer(0, &p) == 0);
    CHECK(p.enabled == 0);
    CHECK(p.host[0] == '\0');
    CHECK(product_config_get_settings(&s) == 0);
    CHECK(strcmp(s.system.device_name, "esp32-min-broker") == 0);
    CHECK(strcmp(s.system.admin_password, "admin") == 0);
}

static void test_apply_large_defaults(void)
{
    field_bridge_peer_t p = { .name = "peer", .host = "1.2.3.4",
                               .mqtt_port = 1883, .p2p_port = 4884,
                               .enabled = 1 };
    CHECK(product_config_set_peer(0, &p) == 0);
    CHECK(product_config_apply_defaults(FIELD_BRIDGE_PROFILE_LARGE) == 0);

    field_bridge_settings_t s;
    CHECK(product_config_get_settings(&s) == 0);
    CHECK(strcmp(s.system.device_name, "esp32-field-router") == 0);
    CHECK(strcmp(s.network.ap_ssid, "ESP32-Field-Router") == 0);
    CHECK(strcmp(s.broker.site_id, "field-large") == 0);
    CHECK(strcmp(s.broker.topic_prefix, "site/field-large") == 0);
    CHECK(product_config_get_peer(0, &p) == 0);
    CHECK(p.enabled == 0);
    CHECK(p.host[0] == '\0');
}

static void test_apply_defaults_rejects_unknown_profile(void)
{
    CHECK(product_config_apply_defaults((field_bridge_defaults_profile_t)99) == -1);
}

/* Persistence tests use an isolated temp file and manage their own init. */
#ifndef __ZEPHYR__
#include <stdlib.h>
#define PERSIST_FILE "/tmp/mqtt_bridge_test_peers.bin"
#define SETTINGS_PERSIST_FILE "/tmp/mqtt_bridge_test_settings.bin"
#define BRIDGE_WIFI_PERSIST_FILE "/tmp/mqtt_bridge_test_bridge_wifi.bin"

#define RUN_PERSIST(fn) do {                                                \
    fail_before = tests_failed;                                             \
    printf("  %-50s ", #fn);                                                \
    unsetenv("BRIDGE_PEERS_FILE");                                          \
    unsetenv("BRIDGE_SETTINGS_FILE");                                       \
    unsetenv("BRIDGE_WIFI_FILE");                                           \
    remove(PERSIST_FILE);                                                   \
    remove(SETTINGS_PERSIST_FILE);                                          \
    remove(BRIDGE_WIFI_PERSIST_FILE);                                       \
    setenv("BRIDGE_PEERS_FILE", PERSIST_FILE, 1);                          \
    setenv("BRIDGE_SETTINGS_FILE", SETTINGS_PERSIST_FILE, 1);              \
    setenv("BRIDGE_WIFI_FILE", BRIDGE_WIFI_PERSIST_FILE, 1);               \
    product_config_init();                                                  \
    fn();                                                                   \
    remove(PERSIST_FILE);                                                   \
    remove(SETTINGS_PERSIST_FILE);                                          \
    remove(BRIDGE_WIFI_PERSIST_FILE);                                       \
    unsetenv("BRIDGE_PEERS_FILE");                                          \
    unsetenv("BRIDGE_SETTINGS_FILE");                                       \
    unsetenv("BRIDGE_WIFI_FILE");                                           \
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

static void test_settings_persist_survives_reinit(void)
{
    field_bridge_settings_t in;
    memset(&in, 0, sizeof(in));
    strcpy(in.system.device_name, "persist-node");
    strcpy(in.system.admin_password, "persist-pass");
    strcpy(in.network.wifi_ssid, "persist-wifi");
    strcpy(in.network.wifi_password, "persist-wifi-pass");
    strcpy(in.network.ap_ssid, "persist-ap");
    strcpy(in.network.ap_password, "persist-ap-pass");
    strcpy(in.network.device_ip, "10.10.10.1");
    strcpy(in.network.gateway, "10.10.10.254");
    strcpy(in.network.netmask, "255.255.255.0");
    strcpy(in.network.dns, "10.10.10.53");
    in.network.dhcp_enabled = 0;
    strcpy(in.broker.site_id, "persist-site");
    strcpy(in.broker.topic_prefix, "site/persist-site");
    in.broker.mqtt_port = 2883;
    in.broker.p2p_port = 5884;
    in.broker.broker_enabled = 1;
    in.broker.bridge_enabled = 1;
    in.broker.mesh_enabled = 0;

    CHECK(product_config_set_settings(&in) == 0);

    product_config_init();

    field_bridge_settings_t out;
    CHECK(product_config_get_settings(&out) == 0);
    CHECK(strcmp(out.system.device_name, "persist-node") == 0);
    CHECK(strcmp(out.network.wifi_ssid, "persist-wifi") == 0);
    CHECK(strcmp(out.network.device_ip, "10.10.10.1") == 0);
    CHECK(strcmp(out.network.dns, "10.10.10.53") == 0);
    CHECK(strcmp(out.broker.site_id, "persist-site") == 0);
    CHECK(out.broker.p2p_port == 5884);
    CHECK(out.broker.mesh_enabled == 0);
    CHECK(product_config_check_admin_password("persist-pass") == 1);
}

static void test_bridge_wifi_persist_survives_reinit(void)
{
    field_bridge_wifi_state_t in;
    memset(&in, 0, sizeof(in));
    strcpy(in.current.ssid, "MQTT-BRIDGE-node1");
    strcpy(in.current.password, "12345678");
    strcpy(in.current.peer_name, "node1");
    strcpy(in.current.host, "127.0.0.1");
    in.current.mqtt_port = 11883;
    in.current.p2p_port = 14884;
    strcpy(in.local_sta_ip, "127.0.0.2");
    strcpy(in.gateway_ip, "127.0.0.1");
    in.enabled = 1;
    in.connected = 1;
    strcpy(in.last_event, "joined peer index 0");
    CHECK(product_config_set_bridge_wifi(&in) == 0);
    CHECK(product_config_add_recent_bridge_wifi(&in.current) == 0);

    product_config_init();

    field_bridge_wifi_state_t out;
    CHECK(product_config_get_bridge_wifi(&out) == 0);
    CHECK(out.enabled == 1);
    CHECK(out.connected == 1);
    CHECK(strcmp(out.current.ssid, "MQTT-BRIDGE-node1") == 0);
    CHECK(strcmp(out.current.host, "127.0.0.1") == 0);
    CHECK(strcmp(out.local_sta_ip, "127.0.0.2") == 0);
    CHECK(strcmp(out.gateway_ip, "127.0.0.1") == 0);
    CHECK(out.current.p2p_port == 14884);
    CHECK(strcmp(out.recent[0].ssid, "MQTT-BRIDGE-node1") == 0);
}

static void test_bridge_wifi_remove_recent(void)
{
    field_bridge_wifi_entry_t first;
    field_bridge_wifi_entry_t second;
    field_bridge_wifi_state_t out;

    memset(&first, 0, sizeof(first));
    strcpy(first.ssid, "MQTT-BRIDGE-node1");
    strcpy(first.peer_name, "node1");
    strcpy(first.host, "127.0.0.1");
    first.mqtt_port = 11883;
    first.p2p_port = 14884;

    second = first;
    strcpy(second.ssid, "MQTT-BRIDGE-node2");
    strcpy(second.peer_name, "node2");
    strcpy(second.host, "127.0.0.2");

    CHECK(product_config_add_recent_bridge_wifi(&first) == 0);
    CHECK(product_config_add_recent_bridge_wifi(&second) == 0);
    CHECK(product_config_remove_recent_bridge_wifi(0) == 0);
    CHECK(product_config_get_bridge_wifi(&out) == 0);
    CHECK(strcmp(out.recent[0].ssid, "MQTT-BRIDGE-node1") == 0);
    CHECK(out.recent[1].ssid[0] == '\0');
    CHECK(product_config_remove_recent_bridge_wifi(1) == -1);
    CHECK(product_config_remove_recent_bridge_wifi(-1) == -1);
}
#endif /* !__ZEPHYR__ */

/* ── main ───────────────────────────────────────────────────────────────── */

int main(void)
{
#ifndef __ZEPHYR__
    /* Isolate existing tests from any leftover persist file. */
    setenv("BRIDGE_PEERS_FILE", "/dev/null", 1);
    setenv("BRIDGE_SETTINGS_FILE", "/dev/null", 1);
    setenv("BRIDGE_WIFI_FILE", "/dev/null", 1);
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
    RUN(test_settings_defaults);
    RUN(test_settings_set_and_get);
    RUN(test_settings_null_args);
    RUN(test_reject_invalid_enabled_peer);
    RUN(test_reject_enabled_peer_with_missing_fields);
    RUN(test_reject_invalid_settings);
    RUN(test_reset_all_restores_defaults_and_clears_peers);
    RUN(test_apply_large_defaults);
    RUN(test_apply_defaults_rejects_unknown_profile);

#ifndef __ZEPHYR__
    RUN_PERSIST(test_persist_survives_reinit);
    RUN_PERSIST(test_persist_all_slots_survive_reinit);
    RUN_PERSIST(test_settings_persist_survives_reinit);
    RUN_PERSIST(test_bridge_wifi_persist_survives_reinit);
    RUN(test_bridge_wifi_remove_recent);
#endif

    printf("\n%d/%d tests passed", tests_passed, tests_run);
    if (tests_failed)
        printf("  (%d FAILED)", tests_failed);
    printf("\n");

    return tests_failed ? 1 : 0;
}
