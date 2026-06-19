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

static void test_get_status(void)
{
    char resp[512];
    int n = http_req("GET /status HTTP/1.0\r\n\r\n", resp, sizeof(resp));
    CHECK(n > 0);
    CHECK(strstr(resp, "200 OK") != NULL);
    CHECK(strstr(resp, "\"status\":\"ok\"") != NULL);
    CHECK(strstr(resp, "\"peers\"") != NULL);
    char peers_field[32];
    snprintf(peers_field, sizeof(peers_field), "\"peers\":%d", FIELD_BRIDGE_PEER_MAX);
    CHECK(strstr(resp, peers_field) != NULL);
}

static void test_get_index_html(void)
{
    char resp[8192];
    int n = http_req("GET / HTTP/1.0\r\n\r\n", resp, sizeof(resp));
    CHECK(n > 0);
    CHECK(strstr(resp, "200 OK") != NULL);
    CHECK(strstr(resp, "Content-Type: text/html") != NULL);
    CHECK(strstr(resp, "Field Bridge Settings") != NULL);
    CHECK(strstr(resp, "peer-form") != NULL);
    CHECK(strstr(resp, "save-all") != NULL);
    CHECK(strstr(resp, "disable-all") != NULL);
}

static void test_get_index_html_alias(void)
{
    char resp[8192];
    int n = http_req("GET /index.html HTTP/1.0\r\n\r\n", resp, sizeof(resp));
    CHECK(n > 0);
    CHECK(strstr(resp, "200 OK") != NULL);
    CHECK(strstr(resp, "Field Bridge Settings") != NULL);
}

static void test_get_peers_empty(void)
{
    char resp[4096];
    int n = http_req("GET /peers HTTP/1.0\r\n\r\n", resp, sizeof(resp));
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
             "POST /peers/0 HTTP/1.0\r\nContent-Length: %d\r\n\r\n%s",
             (int)strlen(body), body);
    char resp[512];
    int n = http_req(req, resp, sizeof(resp));
    CHECK(n > 0);
    CHECK(strstr(resp, "200 OK") != NULL);

    /* Verify change via GET /peers */
    char resp2[4096];
    http_req("GET /peers HTTP/1.0\r\n\r\n", resp2, sizeof(resp2));
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
             "POST /peers/1 HTTP/1.0\r\nContent-Length: %d\r\n\r\n%s",
             (int)strlen(body), body);
    char resp[512];
    int n = http_req(req, resp, sizeof(resp));
    CHECK(n > 0);
    CHECK(strstr(resp, "200 OK") != NULL);

    char resp2[4096];
    http_req("GET /peers HTTP/1.0\r\n\r\n", resp2, sizeof(resp2));
    CHECK(strstr(resp2, "node\\\"two") != NULL);
    CHECK(strstr(resp2, "lab\\\\node") != NULL);
}

static void test_post_peer_invalid_json(void)
{
    const char *body = "not-json";
    char req[256];
    snprintf(req, sizeof(req),
             "POST /peers/0 HTTP/1.0\r\nContent-Length: %d\r\n\r\n%s",
             (int)strlen(body), body);
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
             "POST /peers/0 HTTP/1.0\r\nContent-Length: %d\r\n\r\n%s",
             (int)strlen(body), body);
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
             "POST /peers/99 HTTP/1.0\r\nContent-Length: %d\r\n\r\n%s",
             (int)strlen(body), body);
    char resp[512];
    int n = http_req(req, resp, sizeof(resp));
    CHECK(n > 0);
    CHECK(strstr(resp, "404") != NULL);
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
    product_config_init();
    provisioning_http_start();
    struct timespec bind_wait = { .tv_sec = 0, .tv_nsec = 100000000L };
    nanosleep(&bind_wait, NULL);

    RUN(test_get_status);
    RUN(test_get_index_html);
    RUN(test_get_index_html_alias);
    RUN(test_get_peers_empty);
    RUN(test_post_peer_valid);
    RUN(test_post_peer_escapes_json_strings);
    RUN(test_post_peer_invalid_json);
    RUN(test_post_peer_invalid_enabled);
    RUN(test_post_peer_out_of_range);
    RUN(test_unknown_route);

    printf("\n%d/%d tests passed", tests_passed, tests_run);
    if (tests_failed) printf("  (%d FAILED)", tests_failed);
    printf("\n");
    return tests_failed ? 1 : 0;
}
