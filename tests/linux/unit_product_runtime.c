/*
 * unit_product_runtime — unit tests for product_runtime.c.
 */
#include <stdio.h>
#include <string.h>

#include "../../app/src/product_runtime.h"

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
    fail_before = tests_failed;                                         \
    printf("  %-50s ", #fn);                                            \
    product_runtime_init();                                             \
    fn();                                                               \
    printf("%s\n", tests_failed == fail_before ? "ok" : "FAIL");      \
} while (0)

static void test_defaults(void)
{
    field_bridge_runtime_status_t st;
    CHECK(product_runtime_get_status(&st) == 0);
    CHECK(strcmp(st.wifi_state, "init") == 0);
    CHECK(strcmp(st.ip_addr, "0.0.0.0") == 0);
    CHECK(strcmp(st.broker_state, "stopped") == 0);
    CHECK(strcmp(st.p2p_role, "unknown") == 0);
    CHECK(st.connected_peers == 0);
    CHECK(st.remote_subscriptions == 0);
}

static void test_network_start_ap_default(void)
{
    field_bridge_settings_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    strcpy(cfg.network.device_ip, "192.168.4.1");

    product_runtime_network_start(&cfg);

    field_bridge_runtime_status_t st;
    CHECK(product_runtime_get_status(&st) == 0);
    CHECK(strcmp(st.wifi_state, "ap") == 0);
    CHECK(strcmp(st.ip_addr, "192.168.4.1") == 0);
    CHECK(st.last_error[0] == '\0');
}

static void test_network_start_wifi_configured(void)
{
    field_bridge_settings_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    strcpy(cfg.network.wifi_ssid, "plant");
    strcpy(cfg.network.device_ip, "10.0.0.20");

    product_runtime_network_start(&cfg);

    field_bridge_runtime_status_t st;
    CHECK(product_runtime_get_status(&st) == 0);
    CHECK(strcmp(st.wifi_state, "configured") == 0);
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

int main(void)
{
    printf("=== unit_product_runtime ===\n");

    RUN(test_defaults);
    RUN(test_network_start_ap_default);
    RUN(test_network_start_wifi_configured);
    RUN(test_broker_state_transitions);
    RUN(test_broker_failed);
    RUN(test_publish_test_record);
    RUN(test_publish_test_rejects_invalid);

    printf("\n%d/%d tests passed", tests_passed, tests_run);
    if (tests_failed) printf("  (%d FAILED)", tests_failed);
    printf("\n");
    return tests_failed ? 1 : 0;
}
