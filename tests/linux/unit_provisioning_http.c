/*
 * unit_provisioning_http - socket-based tests for the Ethernet provisioning API.
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "../../app/src/provisioning_http.h"
#include "../../app/src/product_config.h"
#include "../../app/src/product_runtime.h"

static int tests_run;
static int tests_passed;
static int tests_failed;
static int fail_before;

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
    printf("  %-48s ", #fn);                                                \
    fn();                                                                   \
    printf("%s\n", (tests_failed == fail_before) ? "ok" : "FAIL");         \
} while (0)

#define PORT 8080

static int http_req(const char *req, char *buf, int cap)
{
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_port = htons(PORT),
        .sin_addr.s_addr = inet_addr("127.0.0.1"),
    };
    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(fd);
        return -1;
    }
    send(fd, req, strlen(req), 0);
    int total = 0;
    while (total < cap - 1) {
        int n = recv(fd, buf + total, cap - 1 - total, 0);
        if (n <= 0) break;
        total += n;
    }
    if (total >= 0) buf[total] = '\0';
    close(fd);
    return total;
}

static void test_get_status(void)
{
    char resp[1024];
    int n = http_req("GET /status HTTP/1.0\r\n\r\n", resp, sizeof(resp));
    CHECK(n > 0);
    CHECK(strstr(resp, "200 OK") != NULL);
    CHECK(strstr(resp, "\"status\":\"ok\"") != NULL);
    CHECK(strstr(resp, "\"network_state\":\"static\"") != NULL);
    CHECK(strstr(resp, "\"ip_addr\":\"192.168.127.10\"") != NULL);
    CHECK(strstr(resp, "\"test_topic\":\"site/field-a/test\"") != NULL);
    CHECK(strstr(resp, "wifi_state") == NULL);
}

static void test_get_index_html(void)
{
    char resp[64000];
    int n = http_req("GET / HTTP/1.0\r\n\r\n", resp, sizeof(resp));
    CHECK(n > 0);
    CHECK(strstr(resp, "200 OK") != NULL);
    CHECK(strstr(resp, "Field Bridge Settings") != NULL);
    CHECK(strstr(resp, "ETH '+f") != NULL);
    CHECK(strstr(resp, "WiFi '+f") != NULL);
    CHECK(strstr(resp, "WiFi Scan") == NULL);
    CHECK(strstr(resp, "WiFi Connect") == NULL);
    CHECK(strstr(resp, "Operation Result") == NULL);
    CHECK(strstr(resp, "id=\"operation-result\"") == NULL);
    CHECK(strstr(resp, "Broker") != NULL);
    CHECK(strstr(resp, "Broker Peers") != NULL);
    CHECK(strstr(resp, "<table>") != NULL);
    CHECK(strstr(resp, "Save Settings") != NULL);
    CHECK(strstr(resp, "mqtt_port") != NULL);
    CHECK(strstr(resp, "p2p_port") != NULL);
    CHECK(strstr(resp, "Save success") != NULL);
    CHECK(strstr(resp, "Save failed") != NULL);
    CHECK(strstr(resp, "peer_p2p_") == NULL);
    CHECK(strstr(resp, "id=\"bridge_peer_index\"") == NULL);
    CHECK(strstr(resp, "background:#1565c0") != NULL);
    CHECK(strstr(resp, "login-form") == NULL);
    CHECK(strstr(resp, "X-Auth-Token") == NULL);
    CHECK(strstr(resp, "Bridge WiFi") == NULL);
    CHECK(strstr(resp, "scan-bridge-wifi") == NULL);
    CHECK(strstr(resp, "AP SSID") == NULL);
}

static void test_get_index_aliases(void)
{
    char resp[24000];
    int n = http_req("GET /index.html HTTP/1.0\r\n\r\n", resp, sizeof(resp));
    CHECK(n > 0);
    CHECK(strstr(resp, "200 OK") != NULL);
    n = http_req("GET http://192.168.127.4:8080 HTTP/1.1\r\nHost: 192.168.127.4:8080\r\n\r\n",
                 resp, sizeof(resp));
    CHECK(n > 0);
    CHECK(strstr(resp, "200 OK") != NULL);
}

static void test_config_no_auth(void)
{
    char resp[4096];
    int n = http_req("GET /config HTTP/1.0\r\n\r\n", resp, sizeof(resp));
    CHECK(n > 0);
    CHECK(strstr(resp, "200 OK") != NULL);
    CHECK(strstr(resp, "\"device_name\":\"esp32-min-broker\"") != NULL);
    CHECK(strstr(resp, "\"device_ip\":\"192.168.127.10\"") != NULL);
    CHECK(strstr(resp, "\"broker_ip\":\"192.168.127.10\"") != NULL);
    CHECK(strstr(resp, "\"gateway\":\"192.168.127.5\"") != NULL);
    CHECK(strstr(resp, "\"netmask\":\"255.255.0.0\"") != NULL);
    CHECK(strstr(resp, "\"dns\":\"192.168.127.5\"") != NULL);
    CHECK(strstr(resp, "\"admin_password\"") == NULL);
    CHECK(strstr(resp, "\"ap_ssid\"") == NULL);
    CHECK(strstr(resp, "\"wifi_ssid\"") != NULL);
    CHECK(strstr(resp, "\"wifi_device_ip\":\"10.88.0.2\"") != NULL);
    CHECK(strstr(resp, "\"wifi_gateway\":\"10.88.0.1\"") != NULL);
    CHECK(strstr(resp, "\"wifi_netmask\":\"255.255.255.0\"") != NULL);
    CHECK(strstr(resp, "\"wifi_dns\":\"10.88.0.1\"") != NULL);
    CHECK(strstr(resp, "\"wifi_dhcp_enabled\":0") != NULL);
    CHECK(strstr(resp, "\"wifi_password\"") == NULL);
}

static void test_post_config_valid(void)
{
    const char *body =
        "{\"device_name\":\"node-a\",\"device_ip\":\"192.168.9.10\","
        "\"gateway\":\"192.168.9.1\",\"netmask\":\"255.255.255.0\","
        "\"dns\":\"1.1.1.1\",\"dhcp_enabled\":0,"
        "\"wifi_ssid\":\"LabWiFi\",\"wifi_password\":\"secret\","
        "\"wifi_device_ip\":\"10.88.0.9\",\"wifi_gateway\":\"10.88.0.1\","
        "\"wifi_netmask\":\"255.255.255.0\",\"wifi_dns\":\"10.88.0.1\","
        "\"wifi_dhcp_enabled\":0,\"mode\":\"wifi\",\"site_id\":\"field-b\","
        "\"broker_ip\":\"192.168.9.20\","
        "\"topic_prefix\":\"site/field-b\",\"mqtt_port\":1884,\"p2p_port\":4885,"
        "\"broker_enabled\":1,\"bridge_enabled\":1,\"mesh_enabled\":1}";
    char req[2048];
    char resp[4096];

    snprintf(req, sizeof(req),
             "POST /config HTTP/1.0\r\nContent-Length: %d\r\n\r\n%s",
             (int)strlen(body), body);
    int n = http_req(req, resp, sizeof(resp));
    CHECK(n > 0);
    CHECK(strstr(resp, "200 OK") != NULL);
    CHECK(strstr(resp, "\"status\":\"ok\"") != NULL);
    CHECK(strstr(resp, "\"reboot_required\":true") != NULL);

    n = http_req("GET /config HTTP/1.0\r\n\r\n", resp, sizeof(resp));
    CHECK(n > 0);
    CHECK(strstr(resp, "\"device_name\":\"node-a\"") != NULL);
    CHECK(strstr(resp, "\"device_ip\":\"192.168.9.10\"") != NULL);
    CHECK(strstr(resp, "\"gateway\":\"192.168.9.1\"") != NULL);
    CHECK(strstr(resp, "\"netmask\":\"255.255.255.0\"") != NULL);
    CHECK(strstr(resp, "\"dns\":\"1.1.1.1\"") != NULL);
    CHECK(strstr(resp, "\"mode\":\"wifi\"") != NULL);
    CHECK(strstr(resp, "\"wifi_ssid\":\"LabWiFi\"") != NULL);
    CHECK(strstr(resp, "\"wifi_device_ip\":\"10.88.0.9\"") != NULL);
    CHECK(strstr(resp, "\"wifi_gateway\":\"10.88.0.1\"") != NULL);
    CHECK(strstr(resp, "\"wifi_dhcp_enabled\":0") != NULL);
    CHECK(strstr(resp, "\"wifi_password\"") == NULL);
    CHECK(strstr(resp, "\"site_id\":\"field-b\"") != NULL);
    CHECK(strstr(resp, "\"topic_prefix\":\"site/field-b\"") != NULL);
    CHECK(strstr(resp, "\"broker_ip\":\"192.168.9.20\"") != NULL);
    CHECK(strstr(resp, "\"mqtt_port\":1884") != NULL);
    CHECK(strstr(resp, "\"p2p_port\":4885") != NULL);
    CHECK(strstr(resp, "\"broker_enabled\":1") != NULL);
    CHECK(strstr(resp, "\"bridge_enabled\":1") != NULL);
    CHECK(strstr(resp, "\"mesh_enabled\":1") != NULL);
    CHECK(strstr(resp, "\"dhcp_enabled\":0") != NULL);

    n = http_req("GET /status HTTP/1.0\r\n\r\n", resp, sizeof(resp));
    CHECK(n > 0);
    CHECK(strstr(resp, "\"network_state\":\"static\"") != NULL);
    CHECK(strstr(resp, "\"test_topic\":\"site/field-b/test\"") != NULL);

    snprintf(req, sizeof(req),
             "POST /config HTTP/1.0\r\nContent-Length: %d\r\n\r\n%s",
             (int)strlen(body), body);
    n = http_req(req, resp, sizeof(resp));
    CHECK(n > 0);
    CHECK(strstr(resp, "200 OK") != NULL);
    CHECK(strstr(resp, "\"reboot_required\":false") != NULL);
}

static void test_post_config_invalid(void)
{
    char resp[512];
    int n = http_req("POST /config HTTP/1.0\r\nContent-Length: 2\r\n\r\n{}",
                     resp, sizeof(resp));
    CHECK(n > 0);
    CHECK(strstr(resp, "400") != NULL);
    CHECK(strstr(resp, "invalid config JSON") != NULL);
}

static void test_peer_crud_no_auth(void)
{
    const char *body =
        "{\"name\":\"broker-a\",\"host\":\"192.168.9.20\","
        "\"mqtt_port\":1883,\"enabled\":1}";
    char req[1024];
    char resp[4096];

    snprintf(req, sizeof(req),
             "POST /peers/1 HTTP/1.0\r\nContent-Length: %d\r\n\r\n%s",
             (int)strlen(body), body);
    int n = http_req(req, resp, sizeof(resp));
    CHECK(n > 0);
    CHECK(strstr(resp, "200 OK") != NULL);

    n = http_req("GET /peers HTTP/1.0\r\n\r\n", resp, sizeof(resp));
    CHECK(n > 0);
    CHECK(strstr(resp, "\"name\":\"broker-a\"") != NULL);
    CHECK(strstr(resp, "\"host\":\"192.168.9.20\"") != NULL);
    CHECK(strstr(resp, "\"mqtt_port\":1883") != NULL);
    CHECK(strstr(resp, "\"p2p_port\":4884") != NULL);

    n = http_req("GET /peer-status HTTP/1.0\r\n\r\n", resp, sizeof(resp));
    CHECK(n > 0);
    CHECK(strstr(resp, "\"index\":1") != NULL);
    CHECK(strstr(resp, "\"state\":\"disconnected\"") != NULL);
}

static void test_broker_control_and_publish(void)
{
    char resp[1024];
    int n = http_req("POST /broker/control HTTP/1.0\r\nContent-Length: 13\r\n\r\n{\"enabled\":1}",
                     resp, sizeof(resp));
    CHECK(n > 0);
    CHECK(strstr(resp, "200 OK") != NULL);
    CHECK(strstr(resp, "requested") != NULL);

    const char *body =
        "{\"topic\":\"site/field-b/test\",\"payload\":\"hello\",\"qos\":1,\"retain\":0}";
    char req[512];
    snprintf(req, sizeof(req),
             "POST /publish-test HTTP/1.0\r\nContent-Length: %d\r\n\r\n%s",
             (int)strlen(body), body);
    n = http_req(req, resp, sizeof(resp));
    CHECK(n > 0);
    CHECK(strstr(resp, "200 OK") != NULL);
    CHECK(strstr(resp, "recorded") != NULL);
}

static void test_removed_routes(void)
{
    char resp[1024];
    int n = http_req("POST /login HTTP/1.0\r\nContent-Length: 20\r\n\r\n{\"password\":\"admin\"}",
                     resp, sizeof(resp));
    CHECK(n > 0);
    CHECK(strstr(resp, "404") != NULL);
    n = http_req("GET /wifi/scan HTTP/1.0\r\n\r\n", resp, sizeof(resp));
    CHECK(n > 0);
    CHECK(strstr(resp, "404") != NULL);
    n = http_req("GET /bridge-wifi/current HTTP/1.0\r\n\r\n", resp, sizeof(resp));
    CHECK(n > 0);
    CHECK(strstr(resp, "404") != NULL);
}

static void test_config_reset_valid(void)
{
    char resp[4096];
    int n = http_req("POST /config/reset HTTP/1.0\r\nContent-Length: 0\r\n\r\n",
                     resp, sizeof(resp));
    CHECK(n > 0);
    CHECK(strstr(resp, "200 OK") != NULL);
    CHECK(strstr(resp, "reset") != NULL);

    n = http_req("GET /config HTTP/1.0\r\n\r\n", resp, sizeof(resp));
    CHECK(n > 0);
    CHECK(strstr(resp, "\"device_name\":\"esp32-min-broker\"") != NULL);
    CHECK(strstr(resp, "\"device_ip\":\"192.168.127.10\"") != NULL);
    CHECK(strstr(resp, "\"broker_ip\":\"192.168.127.10\"") != NULL);
}

static void test_reboot_valid(void)
{
    char resp[1024];
    int n = http_req("POST /reboot HTTP/1.0\r\nContent-Length: 0\r\n\r\n",
                     resp, sizeof(resp));
    CHECK(n > 0);
    CHECK(strstr(resp, "200 OK") != NULL);
    CHECK(strstr(resp, "rebooting") != NULL);
}

static void test_unknown_route(void)
{
    char resp[512];
    int n = http_req("GET /unknown HTTP/1.0\r\n\r\n", resp, sizeof(resp));
    CHECK(n > 0);
    CHECK(strstr(resp, "404") != NULL);
}

int main(void)
{
    printf("=== unit_provisioning_http ===\n");

    system("rm -rf /tmp/unit_provisioning_http_config");
    setenv("DEPHY_CONFIG_DIR", "/tmp/unit_provisioning_http_config", 1);
    product_config_init();
    product_runtime_init();
    field_bridge_settings_t settings;
    product_config_get_settings(&settings);
    product_runtime_network_start(&settings);
    provisioning_http_start();
    struct timespec bind_wait = { .tv_sec = 0, .tv_nsec = 100000000L };
    nanosleep(&bind_wait, NULL);

    RUN(test_get_status);
    RUN(test_get_index_html);
    RUN(test_get_index_aliases);
    RUN(test_config_no_auth);
    RUN(test_post_config_valid);
    RUN(test_post_config_invalid);
    RUN(test_peer_crud_no_auth);
    RUN(test_broker_control_and_publish);
    RUN(test_removed_routes);
    RUN(test_config_reset_valid);
    RUN(test_reboot_valid);
    RUN(test_unknown_route);

    printf("\n%d/%d tests passed", tests_passed, tests_run);
    if (tests_failed) printf("  (%d FAILED)", tests_failed);
    printf("\n");
    return tests_failed ? 1 : 0;
}
