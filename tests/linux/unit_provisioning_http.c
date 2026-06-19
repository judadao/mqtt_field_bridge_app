/*
 * unit_provisioning_http — socket-based tests for the HTTP server.
 *
 * Starts the server in a background thread, then exercises each endpoint
 * using raw POSIX sockets.
 *
 * Build:  make -C tests/linux unit_provisioning_http
 * Run:    ./tests/linux/out/unit_provisioning_http
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

static int tests_run    = 0;
static int tests_passed = 0;
static int tests_failed = 0;
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
    printf("  %-55s ", #fn);                                                \
    fn();                                                                   \
    printf("%s\n", (tests_failed == fail_before) ? "ok" : "FAIL");         \
} while (0)

#define PORT 8080
static char auth_token[64];

/* Send a raw HTTP request, return response in buf. Returns bytes received. */
static int http_req(const char *req, char *buf, int cap)
{
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_port   = htons(PORT),
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
    if (total > 0) buf[total] = '\0';
    close(fd);
    return total;
}

static void save_auth_token(const char *resp)
{
    const char *p = strstr(resp, "\"token\":\"");
    if (!p) return;
    p += strlen("\"token\":\"");
    const char *end = strchr(p, '"');
    if (!end) return;
    int len = (int)(end - p);
    if (len <= 0 || len >= (int)sizeof(auth_token)) return;
    memcpy(auth_token, p, (size_t)len);
    auth_token[len] = '\0';
}

static int login_as(const char *password)
{
    char body[128];
    char req[512];
    char resp[512];

    snprintf(body, sizeof(body), "{\"password\":\"%s\"}", password);
    snprintf(req, sizeof(req),
             "POST /login HTTP/1.0\r\nContent-Length: %d\r\n\r\n%s",
             (int)strlen(body), body);
    int n = http_req(req, resp, sizeof(resp));
    if (n <= 0 || !strstr(resp, "200 OK")) return -1;
    save_auth_token(resp);
    return auth_token[0] ? 0 : -1;
}

static void test_get_status(void)
{
    char resp[512];
    int n = http_req("GET /status HTTP/1.0\r\n\r\n", resp, sizeof(resp));
    CHECK(n > 0);
    CHECK(strstr(resp, "200 OK") != NULL);
    CHECK(strstr(resp, "\"status\":\"ok\"") != NULL);
    CHECK(strstr(resp, "\"peers\"") != NULL);
    CHECK(strstr(resp, "\"wifi_state\"") != NULL);
    CHECK(strstr(resp, "\"ip_addr\":\"192.168.4.1\"") != NULL);
    CHECK(strstr(resp, "\"broker_state\"") != NULL);
    CHECK(strstr(resp, "\"p2p_role\"") != NULL);
    CHECK(strstr(resp, "\"connected_peers\"") != NULL);
    CHECK(strstr(resp, "\"remote_subscriptions\"") != NULL);
    char peers_field[32];
    snprintf(peers_field, sizeof(peers_field), "\"peers\":%d", FIELD_BRIDGE_PEER_MAX);
    CHECK(strstr(resp, peers_field) != NULL);
}

static void test_get_index_html(void)
{
    char resp[24000];
    int n = http_req("GET / HTTP/1.0\r\n\r\n", resp, sizeof(resp));
    CHECK(n > 0);
    CHECK(strstr(resp, "200 OK") != NULL);
    CHECK(strstr(resp, "Content-Type: text/html") != NULL);
    CHECK(strstr(resp, "Field Bridge Settings") != NULL);
    CHECK(strstr(resp, "ESP32 min broker local setup") != NULL);
    CHECK(strstr(resp, "Login") != NULL);
    CHECK(strstr(resp, "Default password: admin") != NULL);
    CHECK(strstr(resp, "System Setting") != NULL);
    CHECK(strstr(resp, "Network Setting") != NULL);
    CHECK(strstr(resp, "Broker Setting") != NULL);
    CHECK(strstr(resp, "Bridge Mesh Setting") != NULL);
    CHECK(strstr(resp, "Device Default IP") != NULL);
    CHECK(strstr(resp, "Topic Test") != NULL);
    CHECK(strstr(resp, "Start Broker") != NULL);
    CHECK(strstr(resp, "Stop Broker") != NULL);
    CHECK(strstr(resp, "Reset Config") != NULL);
    CHECK(strstr(resp, "publish-test") != NULL);
    CHECK(strstr(resp, "/broker/control") != NULL);
    CHECK(strstr(resp, "192.168.4.1") == NULL);
    CHECK(strstr(resp, "id=\"device_name\"") != NULL);
    CHECK(strstr(resp, "id=\"wifi_ssid\"") != NULL);
    CHECK(strstr(resp, "id=\"cfg_mqtt_port\"") != NULL);
    CHECK(strstr(resp, "id=\"mesh_enabled\"") != NULL);
    CHECK(strstr(resp, "X-Auth-Token") != NULL);
    CHECK(strstr(resp, "token=r.token") != NULL);
    CHECK(strstr(resp, "peer-form") != NULL);
    CHECK(strstr(resp, "save-all") != NULL);
    CHECK(strstr(resp, "disable-all") != NULL);
    CHECK(strstr(resp, "/login") != NULL);
    CHECK(strstr(resp, "/config") != NULL);
    CHECK(strstr(resp, "No slot changes") != NULL);
    CHECK(strstr(resp, "changed slots saved") != NULL);
}

static void test_get_index_html_alias(void)
{
    char resp[24000];
    int n = http_req("GET /index.html HTTP/1.0\r\n\r\n", resp, sizeof(resp));
    CHECK(n > 0);
    CHECK(strstr(resp, "200 OK") != NULL);
    CHECK(strstr(resp, "Field Bridge Settings") != NULL);
}

static void test_login_valid(void)
{
    const char *body = "{\"password\":\"admin\"}";
    char req[256];
    snprintf(req, sizeof(req),
             "POST /login HTTP/1.0\r\nContent-Length: %d\r\n\r\n%s",
             (int)strlen(body), body);
    char resp[512];
    int n = http_req(req, resp, sizeof(resp));
    CHECK(n > 0);
    CHECK(strstr(resp, "200 OK") != NULL);
    CHECK(strstr(resp, "\"status\":\"ok\"") != NULL);
    CHECK(strstr(resp, "\"token\":\"") != NULL);
    save_auth_token(resp);
    CHECK(auth_token[0] != '\0');
}

static void test_login_invalid(void)
{
    const char *body = "{\"password\":\"bad\"}";
    char req[256];
    snprintf(req, sizeof(req),
             "POST /login HTTP/1.0\r\nContent-Length: %d\r\n\r\n%s",
             (int)strlen(body), body);
    char resp[512];
    int n = http_req(req, resp, sizeof(resp));
    CHECK(n > 0);
    CHECK(strstr(resp, "403 Forbidden") != NULL);
    CHECK(strstr(resp, "login failed") != NULL);
}

static void test_get_config_defaults(void)
{
    CHECK(auth_token[0] != '\0');
    char resp[4096];
    char req[256];
    snprintf(req, sizeof(req), "GET /config HTTP/1.0\r\nX-Auth-Token: %s\r\n\r\n",
             auth_token);
    int n = http_req(req, resp, sizeof(resp));
    CHECK(n > 0);
    CHECK(strstr(resp, "200 OK") != NULL);
    CHECK(strstr(resp, "\"device_name\":\"esp32-min-broker\"") != NULL);
    CHECK(strstr(resp, "\"admin_password\":\"admin\"") != NULL);
    CHECK(strstr(resp, "\"ap_ssid\":\"ESP32-Min-Broker\"") != NULL);
    CHECK(strstr(resp, "\"device_ip\":\"192.168.4.1\"") != NULL);
    CHECK(strstr(resp, "\"site_id\":\"field-a\"") != NULL);
    CHECK(strstr(resp, "\"topic_prefix\":\"site/field-a\"") != NULL);
    CHECK(strstr(resp, "\"mesh_enabled\":1") != NULL);
}

static void test_get_config_requires_auth(void)
{
    char resp[512];
    int n = http_req("GET /config HTTP/1.0\r\n\r\n", resp, sizeof(resp));
    CHECK(n > 0);
    CHECK(strstr(resp, "403 Forbidden") != NULL);
    CHECK(strstr(resp, "auth required") != NULL);
}

static void test_post_config_requires_auth(void)
{
    const char *body =
        "{\"device_name\":\"node-a\",\"admin_password\":\"admin\","
        "\"wifi_ssid\":\"plant\",\"wifi_password\":\"wifi-pass\","
        "\"ap_ssid\":\"setup-a\",\"ap_password\":\"setup-pass\","
        "\"device_ip\":\"192.168.9.1\",\"gateway\":\"192.168.9.254\","
        "\"netmask\":\"255.255.255.0\",\"dhcp_enabled\":0,"
        "\"site_id\":\"field-b\",\"topic_prefix\":\"site/field-b\","
        "\"mqtt_port\":1884,\"p2p_port\":4885,"
        "\"broker_enabled\":1,\"bridge_enabled\":1,\"mesh_enabled\":0}";
    char req[1024];
    snprintf(req, sizeof(req),
             "POST /config HTTP/1.0\r\nContent-Length: %d\r\n\r\n%s",
             (int)strlen(body), body);
    char resp[512];
    int n = http_req(req, resp, sizeof(resp));
    CHECK(n > 0);
    CHECK(strstr(resp, "403 Forbidden") != NULL);
    CHECK(strstr(resp, "auth required") != NULL);
}

static void test_post_config_valid(void)
{
    const char *body =
        "{\"device_name\":\"node-a\",\"admin_password\":\"secret\","
        "\"wifi_ssid\":\"plant\",\"wifi_password\":\"wifi-pass\","
        "\"ap_ssid\":\"setup-a\",\"ap_password\":\"setup-pass\","
        "\"device_ip\":\"192.168.9.1\",\"gateway\":\"192.168.9.254\","
        "\"netmask\":\"255.255.255.0\",\"dhcp_enabled\":0,"
        "\"site_id\":\"field-b\",\"topic_prefix\":\"site/field-b\","
        "\"mqtt_port\":1884,\"p2p_port\":4885,"
        "\"broker_enabled\":1,\"bridge_enabled\":1,\"mesh_enabled\":0}";
    char req[1024];
    snprintf(req, sizeof(req),
             "POST /config HTTP/1.0\r\nX-Auth-Token: %s\r\n"
             "Content-Length: %d\r\n\r\n%s",
             auth_token, (int)strlen(body), body);
    char resp[512];
    int n = http_req(req, resp, sizeof(resp));
    CHECK(n > 0);
    CHECK(strstr(resp, "200 OK") != NULL);

    char resp2[4096];
    snprintf(req, sizeof(req), "GET /config HTTP/1.0\r\nX-Auth-Token: %s\r\n\r\n",
             auth_token);
    http_req(req, resp2, sizeof(resp2));
    CHECK(strstr(resp2, "\"device_name\":\"node-a\"") != NULL);
    CHECK(strstr(resp2, "\"device_ip\":\"192.168.9.1\"") != NULL);
    CHECK(strstr(resp2, "\"site_id\":\"field-b\"") != NULL);
    CHECK(strstr(resp2, "\"mesh_enabled\":0") != NULL);

    CHECK(login_as("secret") == 0);
}

static void test_get_peers_requires_auth(void)
{
    char resp[512];
    int n = http_req("GET /peers HTTP/1.0\r\n\r\n", resp, sizeof(resp));
    CHECK(n > 0);
    CHECK(strstr(resp, "403 Forbidden") != NULL);
    CHECK(strstr(resp, "auth required") != NULL);
}

static void test_post_peer_requires_auth(void)
{
    const char *body =
        "{\"name\":\"note2\",\"host\":\"192.168.1.2\","
        "\"mqtt_port\":1883,\"p2p_port\":4884,\"enabled\":1}";
    char req[512];
    snprintf(req, sizeof(req),
             "POST /peers/0 HTTP/1.0\r\nContent-Length: %d\r\n\r\n%s",
             (int)strlen(body), body);
    char resp[512];
    int n = http_req(req, resp, sizeof(resp));
    CHECK(n > 0);
    CHECK(strstr(resp, "403 Forbidden") != NULL);
    CHECK(strstr(resp, "auth required") != NULL);
}

static void test_broker_control_requires_auth(void)
{
    const char *body = "{\"enabled\":1}";
    char req[256];
    snprintf(req, sizeof(req),
             "POST /broker/control HTTP/1.0\r\nContent-Length: %d\r\n\r\n%s",
             (int)strlen(body), body);
    char resp[512];
    int n = http_req(req, resp, sizeof(resp));
    CHECK(n > 0);
    CHECK(strstr(resp, "403 Forbidden") != NULL);
    CHECK(strstr(resp, "auth required") != NULL);
}

static void test_broker_control_valid(void)
{
    const char *body = "{\"enabled\":1}";
    char req[512];
    snprintf(req, sizeof(req),
             "POST /broker/control HTTP/1.0\r\nX-Auth-Token: %s\r\n"
             "Content-Length: %d\r\n\r\n%s",
             auth_token, (int)strlen(body), body);
    char resp[512];
    int n = http_req(req, resp, sizeof(resp));
    CHECK(n > 0);
    CHECK(strstr(resp, "200 OK") != NULL);
    CHECK(strstr(resp, "requested") != NULL);

    n = http_req("GET /status HTTP/1.0\r\n\r\n", resp, sizeof(resp));
    CHECK(n > 0);
    CHECK(strstr(resp, "\"broker_state\":\"requested\"") != NULL);
}

static void test_broker_control_invalid(void)
{
    const char *body = "{\"enabled\":2}";
    char req[512];
    snprintf(req, sizeof(req),
             "POST /broker/control HTTP/1.0\r\nX-Auth-Token: %s\r\n"
             "Content-Length: %d\r\n\r\n%s",
             auth_token, (int)strlen(body), body);
    char resp[512];
    int n = http_req(req, resp, sizeof(resp));
    CHECK(n > 0);
    CHECK(strstr(resp, "400") != NULL);
    CHECK(strstr(resp, "invalid broker control JSON") != NULL);
}

static void test_publish_test_requires_auth(void)
{
    const char *body =
        "{\"topic\":\"site/field-b/test\",\"payload\":\"hello\",\"qos\":0,\"retain\":0}";
    char req[512];
    snprintf(req, sizeof(req),
             "POST /publish-test HTTP/1.0\r\nContent-Length: %d\r\n\r\n%s",
             (int)strlen(body), body);
    char resp[512];
    int n = http_req(req, resp, sizeof(resp));
    CHECK(n > 0);
    CHECK(strstr(resp, "403 Forbidden") != NULL);
    CHECK(strstr(resp, "auth required") != NULL);
}

static void test_publish_test_valid(void)
{
    const char *body =
        "{\"topic\":\"site/field-b/test\",\"payload\":\"hello\",\"qos\":1,\"retain\":0}";
    char req[512];
    snprintf(req, sizeof(req),
             "POST /publish-test HTTP/1.0\r\nX-Auth-Token: %s\r\n"
             "Content-Length: %d\r\n\r\n%s",
             auth_token, (int)strlen(body), body);
    char resp[512];
    int n = http_req(req, resp, sizeof(resp));
    CHECK(n > 0);
    CHECK(strstr(resp, "200 OK") != NULL);
    CHECK(strstr(resp, "recorded") != NULL);

    field_bridge_publish_test_t last;
    CHECK(product_runtime_get_last_publish_test(&last) == 0);
    CHECK(strcmp(last.topic, "site/field-b/test") == 0);
    CHECK(strcmp(last.payload, "hello") == 0);
    CHECK(last.qos == 1);
}

static void test_publish_test_invalid(void)
{
    const char *body =
        "{\"topic\":\"site/field-b/test\",\"payload\":\"hello\",\"qos\":2,\"retain\":0}";
    char req[512];
    snprintf(req, sizeof(req),
             "POST /publish-test HTTP/1.0\r\nX-Auth-Token: %s\r\n"
             "Content-Length: %d\r\n\r\n%s",
             auth_token, (int)strlen(body), body);
    char resp[512];
    int n = http_req(req, resp, sizeof(resp));
    CHECK(n > 0);
    CHECK(strstr(resp, "400") != NULL);
    CHECK(strstr(resp, "invalid publish test JSON") != NULL);
}

static void test_config_reset_requires_auth(void)
{
    char resp[512];
    int n = http_req("POST /config/reset HTTP/1.0\r\nContent-Length: 0\r\n\r\n",
                     resp, sizeof(resp));
    CHECK(n > 0);
    CHECK(strstr(resp, "403 Forbidden") != NULL);
    CHECK(strstr(resp, "auth required") != NULL);
}

static void test_post_config_invalid(void)
{
    const char *body = "{\"device_name\":\"bad\"}";
    char req[256];
    snprintf(req, sizeof(req),
             "POST /config HTTP/1.0\r\nX-Auth-Token: %s\r\n"
             "Content-Length: %d\r\n\r\n%s",
             auth_token, (int)strlen(body), body);
    char resp[512];
    int n = http_req(req, resp, sizeof(resp));
    CHECK(n > 0);
    CHECK(strstr(resp, "400") != NULL);
    CHECK(strstr(resp, "invalid config JSON") != NULL);
}

static void test_get_peers_empty(void)
{
    char resp[4096];
    char req[256];
    snprintf(req, sizeof(req), "GET /peers HTTP/1.0\r\nX-Auth-Token: %s\r\n\r\n",
             auth_token);
    int n = http_req(req, resp, sizeof(resp));
    CHECK(n > 0);
    CHECK(strstr(resp, "200 OK") != NULL);
    CHECK(strstr(resp, "[") != NULL);
    CHECK(strstr(resp, "]") != NULL);
}

static void test_post_peer_valid(void)
{
    const char *body =
        "{\"name\":\"note2\",\"host\":\"192.168.1.2\","
        "\"mqtt_port\":1883,\"p2p_port\":4884,\"enabled\":1}";
    char req[512];
    snprintf(req, sizeof(req),
             "POST /peers/0 HTTP/1.0\r\nX-Auth-Token: %s\r\n"
             "Content-Length: %d\r\n\r\n%s",
             auth_token, (int)strlen(body), body);
    char resp[512];
    int n = http_req(req, resp, sizeof(resp));
    CHECK(n > 0);
    CHECK(strstr(resp, "200 OK") != NULL);

    /* Verify change via GET /peers */
    char resp2[4096];
    snprintf(req, sizeof(req), "GET /peers HTTP/1.0\r\nX-Auth-Token: %s\r\n\r\n",
             auth_token);
    http_req(req, resp2, sizeof(resp2));
    CHECK(strstr(resp2, "192.168.1.2") != NULL);
    CHECK(strstr(resp2, "1883") != NULL);
}

static void test_post_peer_escapes_json_strings(void)
{
    const char *body =
        "{\"name\":\"node\\\"two\",\"host\":\"lab\\\\node\","
        "\"mqtt_port\":1883,\"p2p_port\":4884,\"enabled\":1}";
    char req[512];
    snprintf(req, sizeof(req),
             "POST /peers/1 HTTP/1.0\r\nX-Auth-Token: %s\r\n"
             "Content-Length: %d\r\n\r\n%s",
             auth_token, (int)strlen(body), body);
    char resp[512];
    int n = http_req(req, resp, sizeof(resp));
    CHECK(n > 0);
    CHECK(strstr(resp, "200 OK") != NULL);

    char resp2[4096];
    snprintf(req, sizeof(req), "GET /peers HTTP/1.0\r\nX-Auth-Token: %s\r\n\r\n",
             auth_token);
    http_req(req, resp2, sizeof(resp2));
    CHECK(strstr(resp2, "node\\\"two") != NULL);
    CHECK(strstr(resp2, "lab\\\\node") != NULL);
}

static void test_post_peer_invalid_json(void)
{
    const char *body = "not-json";
    char req[256];
    snprintf(req, sizeof(req),
             "POST /peers/0 HTTP/1.0\r\nX-Auth-Token: %s\r\n"
             "Content-Length: %d\r\n\r\n%s",
             auth_token, (int)strlen(body), body);
    char resp[512];
    int n = http_req(req, resp, sizeof(resp));
    CHECK(n > 0);
    CHECK(strstr(resp, "400") != NULL);
}

static void test_post_peer_invalid_enabled(void)
{
    const char *body =
        "{\"host\":\"1.2.3.4\",\"mqtt_port\":1883,\"p2p_port\":4884,\"enabled\":2}";
    char req[512];
    snprintf(req, sizeof(req),
             "POST /peers/0 HTTP/1.0\r\nX-Auth-Token: %s\r\n"
             "Content-Length: %d\r\n\r\n%s",
             auth_token, (int)strlen(body), body);
    char resp[512];
    int n = http_req(req, resp, sizeof(resp));
    CHECK(n > 0);
    CHECK(strstr(resp, "400") != NULL);
}

static void test_post_peer_out_of_range(void)
{
    const char *body =
        "{\"host\":\"1.2.3.4\",\"mqtt_port\":1883,\"p2p_port\":4884,\"enabled\":1}";
    char req[512];
    snprintf(req, sizeof(req),
             "POST /peers/99 HTTP/1.0\r\nX-Auth-Token: %s\r\n"
             "Content-Length: %d\r\n\r\n%s",
             auth_token, (int)strlen(body), body);
    char resp[512];
    int n = http_req(req, resp, sizeof(resp));
    CHECK(n > 0);
    CHECK(strstr(resp, "404") != NULL);
}

static void test_config_reset_valid(void)
{
    char req[512];
    char resp[4096];

    snprintf(req, sizeof(req),
             "POST /config/reset HTTP/1.0\r\nX-Auth-Token: %s\r\n"
             "Content-Length: 0\r\n\r\n",
             auth_token);
    int n = http_req(req, resp, sizeof(resp));
    CHECK(n > 0);
    CHECK(strstr(resp, "200 OK") != NULL);
    CHECK(strstr(resp, "reset") != NULL);

    snprintf(req, sizeof(req), "GET /config HTTP/1.0\r\nX-Auth-Token: %s\r\n\r\n",
             auth_token);
    n = http_req(req, resp, sizeof(resp));
    CHECK(n > 0);
    CHECK(strstr(resp, "403 Forbidden") != NULL);

    CHECK(login_as("admin") == 0);

    snprintf(req, sizeof(req), "GET /config HTTP/1.0\r\nX-Auth-Token: %s\r\n\r\n",
             auth_token);
    n = http_req(req, resp, sizeof(resp));
    CHECK(n > 0);
    CHECK(strstr(resp, "\"device_name\":\"esp32-min-broker\"") != NULL);
    CHECK(strstr(resp, "\"admin_password\":\"admin\"") != NULL);
    CHECK(strstr(resp, "\"device_ip\":\"192.168.4.1\"") != NULL);

    snprintf(req, sizeof(req), "GET /peers HTTP/1.0\r\nX-Auth-Token: %s\r\n\r\n",
             auth_token);
    n = http_req(req, resp, sizeof(resp));
    CHECK(n > 0);
    CHECK(strstr(resp, "192.168.1.2") == NULL);
    CHECK(strstr(resp, "node\\\"two") == NULL);
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

    setenv("BRIDGE_PEERS_FILE", "/dev/null", 1);
    setenv("BRIDGE_SETTINGS_FILE", "/dev/null", 1);
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
    RUN(test_get_index_html_alias);
    RUN(test_login_valid);
    RUN(test_login_invalid);
    RUN(test_get_config_requires_auth);
    RUN(test_post_config_requires_auth);
    RUN(test_get_config_defaults);
    RUN(test_post_config_valid);
    RUN(test_post_config_invalid);
    RUN(test_get_peers_requires_auth);
    RUN(test_post_peer_requires_auth);
    RUN(test_broker_control_requires_auth);
    RUN(test_broker_control_valid);
    RUN(test_broker_control_invalid);
    RUN(test_publish_test_requires_auth);
    RUN(test_publish_test_valid);
    RUN(test_publish_test_invalid);
    RUN(test_config_reset_requires_auth);
    RUN(test_get_peers_empty);
    RUN(test_post_peer_valid);
    RUN(test_post_peer_escapes_json_strings);
    RUN(test_post_peer_invalid_json);
    RUN(test_post_peer_invalid_enabled);
    RUN(test_post_peer_out_of_range);
    RUN(test_config_reset_valid);
    RUN(test_unknown_route);

    printf("\n%d/%d tests passed", tests_passed, tests_run);
    if (tests_failed) printf("  (%d FAILED)", tests_failed);
    printf("\n");
    return tests_failed ? 1 : 0;
}
