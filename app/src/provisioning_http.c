#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <errno.h>

#include <zephyr/logging/log.h>

#include "provisioning_http.h"
#include "product_config.h"
#include "bridge_control.h"

LOG_MODULE_REGISTER(provisioning_http, LOG_LEVEL_INF);

#ifndef PROVISIONING_HTTP_PORT
#define PROVISIONING_HTTP_PORT 8080
#endif

/* ── JSON helpers ────────────────────────────────────────────────────────── */

static int json_str_field(const char *json, const char *key,
                          char *out, int cap)
{
    char search[72];
    snprintf(search, sizeof(search), "\"%s\":", key);
    const char *p = strstr(json, search);
    if (!p) return -1;
    p += strlen(search);
    while (*p == ' ') p++;
    if (*p != '"') return -1;
    p++;
    const char *end = strchr(p, '"');
    if (!end) return -1;
    int len = (int)(end - p);
    if (len >= cap) len = cap - 1;
    memcpy(out, p, (size_t)len);
    out[len] = '\0';
    return 0;
}

static int json_uint16_field(const char *json, const char *key, uint16_t *out)
{
    char search[72];
    snprintf(search, sizeof(search), "\"%s\":", key);
    const char *p = strstr(json, search);
    if (!p) return -1;
    p += strlen(search);
    while (*p == ' ') p++;
    char *endp;
    unsigned long v = strtoul(p, &endp, 10);
    if (endp == p || v > 65535UL) return -1;
    *out = (uint16_t)v;
    return 0;
}

static int json_uint8_field(const char *json, const char *key, uint8_t *out)
{
    uint16_t v;
    if (json_uint16_field(json, key, &v) != 0) return -1;
    *out = (uint8_t)v;
    return 0;
}

static int json_decode_peer(const char *json, field_bridge_peer_t *out)
{
    memset(out, 0, sizeof(*out));
    json_str_field(json, "name", out->name, sizeof(out->name));  /* optional */
    if (json_str_field(json, "host", out->host, sizeof(out->host)) != 0) return -1;
    if (json_uint16_field(json, "mqtt_port", &out->mqtt_port) != 0) return -1;
    if (json_uint16_field(json, "p2p_port",  &out->p2p_port)  != 0) return -1;
    if (json_uint8_field(json, "enabled", &out->enabled) != 0) return -1;
    return 0;
}

/* ── HTTP helpers ────────────────────────────────────────────────────────── */

static int parse_request_line(const char *buf,
                              char *method, int mcap,
                              char *path,   int pcap)
{
    const char *p = buf;
    const char *end;
    int len;

    end = strchr(p, ' ');
    if (!end) return -1;
    len = (int)(end - p);
    if (len <= 0 || len >= mcap) return -1;
    memcpy(method, p, (size_t)len);
    method[len] = '\0';
    p = end + 1;

    end = strchr(p, ' ');
    if (!end) { end = strchr(p, '\r'); }
    if (!end) { end = strchr(p, '\n'); }
    if (!end) return -1;
    len = (int)(end - p);
    if (len <= 0 || len >= pcap) return -1;
    memcpy(path, p, (size_t)len);
    path[len] = '\0';
    return 0;
}

static int extract_content_length(const char *headers)
{
    const char *p = strstr(headers, "Content-Length: ");
    if (!p) p = strstr(headers, "content-length: ");
    if (!p) return 0;
    p += strlen("Content-Length: ");
    return atoi(p);
}

/* ── Platform socket macros ──────────────────────────────────────────────── */

#ifndef __ZEPHYR__
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#define PLAT_SEND(fd, buf, len) send((fd), (buf), (size_t)(len), MSG_NOSIGNAL)
#define PLAT_RECV(fd, buf, len) recv((fd), (buf), (size_t)(len), 0)
#define PLAT_CLOSE(fd)          close(fd)
#else
#include <zephyr/net/socket.h>
#define PLAT_SEND(fd, buf, len) zsock_send((fd), (buf), (size_t)(len), 0)
#define PLAT_RECV(fd, buf, len) zsock_recv((fd), (buf), (size_t)(len), 0)
#define PLAT_CLOSE(fd)          zsock_close(fd)
#endif

/* ── Response / route handlers ───────────────────────────────────────────── */

static void send_response(int fd, int status, const char *body)
{
    const char *reason = (status == 200) ? "OK" :
                         (status == 400) ? "Bad Request" :
                         (status == 404) ? "Not Found" : "Internal Error";
    int body_len = body ? (int)strlen(body) : 0;
    char out[1536];
    int hlen = snprintf(out, sizeof(out),
                        "HTTP/1.0 %d %s\r\n"
                        "Content-Type: application/json\r\n"
                        "Content-Length: %d\r\n"
                        "\r\n",
                        status, reason, body_len);
    if (body_len > 0 && hlen + body_len < (int)sizeof(out)) {
        memcpy(out + hlen, body, (size_t)body_len);
        PLAT_SEND(fd, out, hlen + body_len);
    } else {
        PLAT_SEND(fd, out, hlen);
        if (body_len > 0) PLAT_SEND(fd, body, body_len);
    }
}

static void handle_get_status(int fd)
{
    char buf[128];
    int n = snprintf(buf, sizeof(buf),
                     "{\"status\":\"ok\",\"peers\":%d}",
                     product_config_peer_count());
    send_response(fd, 200, buf);
    (void)n;
}

static void handle_get_peers(int fd)
{
    char buf[1024];
    int pos = 0;
    buf[pos++] = '[';
    for (int i = 0; i < FIELD_BRIDGE_PEER_MAX; i++) {
        field_bridge_peer_t p;
        if (product_config_get_peer(i, &p) != 0) continue;
        if (i > 0) buf[pos++] = ',';
        int written = snprintf(buf + pos, (size_t)(sizeof(buf) - pos),
                               "{\"name\":\"%s\",\"host\":\"%s\","
                               "\"mqtt_port\":%u,\"p2p_port\":%u,\"enabled\":%u}",
                               p.name, p.host, p.mqtt_port, p.p2p_port, p.enabled);
        if (written < 0 || written >= (int)(sizeof(buf) - pos)) break;
        pos += written;
    }
    buf[pos++] = ']';
    buf[pos]   = '\0';
    send_response(fd, 200, buf);
}

static void handle_post_peer(int fd, int idx, const char *body)
{
    if (idx < 0 || idx >= FIELD_BRIDGE_PEER_MAX) {
        send_response(fd, 404, "{\"error\":\"peer index out of range\"}");
        return;
    }
    field_bridge_peer_t peer;
    if (json_decode_peer(body, &peer) != 0) {
        send_response(fd, 400, "{\"error\":\"invalid JSON\"}");
        return;
    }
    product_config_set_peer(idx, &peer);
    bridge_control_apply_peers();
    send_response(fd, 200, "{\"status\":\"ok\"}");
}

#define HTTP_BUF_SIZE 2048

static void handle_client(int fd)
{
    char buf[HTTP_BUF_SIZE];
    int total = 0;
    ssize_t n;

    /* Read until end-of-headers */
    while (total < HTTP_BUF_SIZE - 1) {
        n = PLAT_RECV(fd, buf + total, HTTP_BUF_SIZE - 1 - total);
        if (n <= 0) goto done;
        total += (int)n;
        buf[total] = '\0';
        if (strstr(buf, "\r\n\r\n")) break;
    }
    buf[total] = '\0';

    char method[16], path[128];
    if (parse_request_line(buf, method, sizeof(method), path, sizeof(path)) != 0) {
        send_response(fd, 400, "{\"error\":\"bad request\"}");
        goto done;
    }

    const char *hdr_end = strstr(buf, "\r\n\r\n");
    if (!hdr_end) { send_response(fd, 400, "{\"error\":\"bad request\"}"); goto done; }

    int content_length = extract_content_length(buf);
    const char *body_start = hdr_end + 4;
    int body_received = (int)(buf + total - body_start);

    /* Read remaining body bytes if needed */
    while (body_received < content_length && total < HTTP_BUF_SIZE - 1) {
        n = PLAT_RECV(fd, buf + total, HTTP_BUF_SIZE - 1 - total);
        if (n <= 0) break;
        total += (int)n;
        body_received += (int)n;
    }
    buf[total] = '\0';
    body_start = strstr(buf, "\r\n\r\n") + 4;

    if (strcmp(method, "GET") == 0 && strcmp(path, "/status") == 0) {
        handle_get_status(fd);
    } else if (strcmp(method, "GET") == 0 && strcmp(path, "/peers") == 0) {
        handle_get_peers(fd);
    } else if (strcmp(method, "POST") == 0 && strncmp(path, "/peers/", 7) == 0) {
        int idx = atoi(path + 7);
        handle_post_peer(fd, idx, body_start);
    } else {
        send_response(fd, 404, "{\"error\":\"not found\"}");
    }

done:
    PLAT_CLOSE(fd);
}

/* ── Server thread ───────────────────────────────────────────────────────── */

static int g_server_fd = -1;

static void server_loop(void)
{
    while (1) {
        int client_fd;
#ifndef __ZEPHYR__
        client_fd = (int)accept(g_server_fd, NULL, NULL);
#else
        client_fd = (int)zsock_accept(g_server_fd, NULL, NULL);
#endif
        if (client_fd < 0) continue;
        handle_client(client_fd);
    }
}

#ifndef __ZEPHYR__

#include <pthread.h>

static void *server_thread_entry(void *arg)
{
    (void)arg;
    server_loop();
    return NULL;
}

void provisioning_http_start(void)
{
    struct sockaddr_in addr;
    int opt = 1;

    g_server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (g_server_fd < 0) {
        LOG_ERR("provisioning HTTP socket failed");
        return;
    }
    setsockopt(g_server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons(PROVISIONING_HTTP_PORT);
    addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(g_server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0 ||
        listen(g_server_fd, 4) < 0) {
        LOG_ERR("provisioning HTTP bind/listen failed");
        close(g_server_fd);
        g_server_fd = -1;
        return;
    }

    pthread_t t;
    pthread_create(&t, NULL, server_thread_entry, NULL);
    pthread_detach(t);
    LOG_INF("provisioning HTTP listening on port %d", PROVISIONING_HTTP_PORT);
}

#else  /* __ZEPHYR__ */

#include <zephyr/kernel.h>

#define PROV_HTTP_STACK_SIZE 4096
K_THREAD_STACK_DEFINE(prov_http_stack, PROV_HTTP_STACK_SIZE);
static struct k_thread prov_http_thread;

static void server_thread_entry(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1); ARG_UNUSED(p2); ARG_UNUSED(p3);
    server_loop();
}

void provisioning_http_start(void)
{
    struct sockaddr_in addr;
    int opt = 1;

    g_server_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (g_server_fd < 0) {
        LOG_ERR("provisioning HTTP socket failed: %d", errno);
        return;
    }
    setsockopt(g_server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons(PROVISIONING_HTTP_PORT);
    addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(g_server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0 ||
        listen(g_server_fd, 4) < 0) {
        LOG_ERR("provisioning HTTP bind/listen failed: %d", errno);
        close(g_server_fd);
        g_server_fd = -1;
        return;
    }

    k_thread_create(&prov_http_thread,
                    prov_http_stack,
                    K_THREAD_STACK_SIZEOF(prov_http_stack),
                    server_thread_entry,
                    NULL, NULL, NULL,
                    7, 0, K_NO_WAIT);
    k_thread_name_set(&prov_http_thread, "prov_http");
    LOG_INF("provisioning HTTP listening on port %d", PROVISIONING_HTTP_PORT);
}

#endif /* __ZEPHYR__ */
