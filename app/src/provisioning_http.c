#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef __ZEPHYR__
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/http/service.h>
#include <zephyr/sys/reboot.h>
#else
#include <arpa/inet.h>
#include <pthread.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#include <zephyr/logging/log.h>
#define PH_SOCKET socket
#define PH_SETSOCKOPT setsockopt
#define PH_BIND bind
#define PH_LISTEN listen
#define PH_ACCEPT accept
#define PH_RECV recv
#define PH_SEND send
#define PH_CLOSE close
#endif

#include "provisioning_http.h"

#include "bridge_control.h"
#include "product_config.h"
#include "product_runtime.h"
#include "product_topics.h"

#ifndef __ZEPHYR__
__attribute__((weak)) int bridge_control_apply_peers(void)
{
    return 0;
}
#endif

LOG_MODULE_REGISTER(provisioning_http, LOG_LEVEL_INF);

#if defined(__ZEPHYR__) && !defined(CONFIG_HTTP_SERVER)

int provisioning_http_start(void)
{
    return -ENODEV;
}

#else

#ifdef __ZEPHYR__
#define REQ_BUF_SIZE 768
#define RESP_BUF_SIZE 2304
#else
#define REQ_BUF_SIZE 4096
#define RESP_BUF_SIZE 8192
#endif

static uint8_t started;
#ifdef __ZEPHYR__
struct zephyr_http_buffers {
    char request[REQ_BUF_SIZE];
    char response[RESP_BUF_SIZE];
};

static struct zephyr_http_buffers *zephyr_buffers;
#else
static char request_buf[REQ_BUF_SIZE];
static char response_buf[RESP_BUF_SIZE];
#endif

#ifdef __ZEPHYR__
static void reboot_later_work_fn(struct k_work *work);
K_WORK_DELAYABLE_DEFINE(http_reboot_work, reboot_later_work_fn);

static void reboot_later_work_fn(struct k_work *work)
{
    ARG_UNUSED(work);
    sys_reboot(SYS_REBOOT_COLD);
}
#else
static int listen_fd = -1;
static pthread_t http_thread_data;
#endif

static const char index_html[] =
"<!doctype html><html><head><meta charset=\"utf-8\">"
"<title>Field Bridge Settings</title>"
"<style>body{font:16px sans-serif;margin:16px}table{border-collapse:collapse;width:100%;margin:6px 0 12px}"
"td,th{border:1px solid #ccc;padding:4px}input{width:100%;box-sizing:border-box}"
"select{width:100%;box-sizing:border-box}input[type=checkbox]{width:auto}button{background:#1565c0;color:white;border:0;padding:7px 10px}</style></head>"
"<body><h1>Field Bridge Settings</h1><h2>Network</h2><table><tbody id=n></tbody></table>"
"<h2>Broker</h2><table><tbody id=c></tbody></table><button onclick=s()>Save Settings</button>"
"<h2>Broker Peers</h2><table><thead><tr>"
"<th>#</th><th>On</th><th>Name</th><th>Host</th><th>MQTT</th><th>P2P</th><th></th></tr></thead>"
"<tbody id=p></tbody></table>"
"<script>"
"let C,P,E=['device_ip','gateway','netmask','dns'],"
"A=['broker_ip','mqtt_port','p2p_port','topic_prefix'],G=x=>document.getElementById(x);"
"async function J(u,o){let r=await fetch(u,o),t=await r.text();if(!r.ok)throw t;try{return JSON.parse(t)}catch(e){return t}}"
"async function L(){C=await J('/config');P=await J('/peers');"
"n.innerHTML='<tr><th>Mode</th><td><select id=mode>'+['auto','eth'].map(x=>'<option '+(C.mode==x?'selected':'')+'>'+x+'</option>').join('')+'</select></td></tr>'"
"+E.map(f=>'<tr><th>ETH '+f+'</th><td><input id='+f+' value=\"'+(C[f]||'')+'\"></td></tr>').join('')"
"+'<tr><th>ETH dhcp</th><td><input id=dhcp_enabled type=checkbox '+(C.dhcp_enabled?'checked':'')+'></td></tr>';"
"c.innerHTML=A.map(f=>'<tr><th>'+f+'</th><td><input id='+f+' value=\"'+(C[f]||'')+'\"></td></tr>').join('');"
"p.innerHTML=P.map((x,i)=>'<tr><td>'+i+'</td><td><input id=e'+i+' type=checkbox '+(x.enabled?'checked':'')+'></td>"
"<td><input id=n'+i+' value=\"'+(x.name||'')+'\"></td><td><input id=h'+i+' value=\"'+(x.host||'')+'\"></td>"
"<td><input id=m'+i+' value=\"'+(x.mqtt_port||1883)+'\"></td><td><input id=q'+i+' value=\"'+(x.p2p_port||4884)+'\"></td>"
"<td><button onclick=P'+i+'()>Save</button></td></tr>').join('')}"
"async function s(){try{[...E,...A].map(f=>C[f]=G(f).value);C.mode=G('mode').value;C.dhcp_enabled=G('dhcp_enabled').checked?1:0;"
"C.mqtt_port=+C.mqtt_port||1883;C.p2p_port=+C.p2p_port||4884;"
"await J('/config',{method:'POST',body:JSON.stringify(C)});alert('Save success');L()}catch(e){alert('Save failed')}}"
"async function P0(){return V(0)}async function P1(){return V(1)}async function V(i){try{let x={enabled:G('e'+i).checked?1:0,"
"name:G('n'+i).value,host:G('h'+i).value,mqtt_port:+G('m'+i).value||1883,p2p_port:+G('q'+i).value||4884};"
"await J('/peers/'+i,{method:'POST',body:JSON.stringify(x)});alert('Save success');L()}catch(e){alert('Save failed')}}L()</script></body></html>";

#ifndef __ZEPHYR__
static int send_all(int fd, const char *buf, size_t len)
{
    size_t sent = 0;

    while (sent < len) {
        int n = PH_SEND(fd, buf + sent, len - sent, 0);
        if (n <= 0) {
            return -1;
        }
        sent += (size_t)n;
    }
    return 0;
}

static void http_send(int fd, int code, const char *ctype, const char *body)
{
    char header[192];
    const char *reason = code == 200 ? "OK" :
                         code == 400 ? "Bad Request" :
                         code == 405 ? "Method Not Allowed" : "Not Found";
    size_t body_len = body ? strlen(body) : 0;
    int n = snprintf(header, sizeof(header),
                     "HTTP/1.1 %d %s\r\n"
                     "Connection: close\r\n"
                     "Content-Type: %s\r\n"
                     "Content-Length: %u\r\n\r\n",
                     code, reason, ctype, (unsigned int)body_len);
    if (n > 0) {
        (void)send_all(fd, header, (size_t)n);
    }
    if (body_len) {
        (void)send_all(fd, body, body_len);
    }
}
#endif

static const char *json_find_key(const char *json, const char *key)
{
    static char needle[64];

    snprintf(needle, sizeof(needle), "\"%s\"", key);
    return strstr(json, needle);
}

static int json_get_string(const char *json, const char *key, char *out, size_t cap)
{
    const char *p = json_find_key(json, key);
    size_t n = 0;

    if (!p || !out || cap == 0) {
        return -1;
    }
    p = strchr(p, ':');
    if (!p) {
        return -1;
    }
    p++;
    while (*p && isspace((unsigned char)*p)) {
        p++;
    }
    if (*p != '"') {
        return -1;
    }
    p++;
    while (*p && *p != '"' && n + 1 < cap) {
        if (*p == '\\' && p[1]) {
            p++;
        }
        out[n++] = *p++;
    }
    out[n] = '\0';
    return *p == '"' ? 0 : -1;
}

static int json_get_u16(const char *json, const char *key, uint16_t *out)
{
    const char *p = json_find_key(json, key);
    long value;

    if (!p || !out) {
        return -1;
    }
    p = strchr(p, ':');
    if (!p) {
        return -1;
    }
    value = strtol(p + 1, NULL, 10);
    if (value <= 0 || value > 65535) {
        return -1;
    }
    *out = (uint16_t)value;
    return 0;
}

static int json_get_boolish(const char *json, const char *key, uint8_t *out)
{
    const char *p = json_find_key(json, key);
    long value;

    if (!p || !out) {
        return -1;
    }
    p = strchr(p, ':');
    if (!p) {
        return -1;
    }
    p++;
    while (*p && isspace((unsigned char)*p)) {
        p++;
    }
    if (strncmp(p, "true", 4) == 0) {
        *out = 1;
        return 0;
    }
    if (strncmp(p, "false", 5) == 0) {
        *out = 0;
        return 0;
    }
    value = strtol(p, NULL, 10);
    if (value < 0 || value > 1) {
        return -1;
    }
    *out = (uint8_t)value;
    return 0;
}

static void copy_if_present(char *dst, size_t cap, const char *json, const char *key)
{
    (void)json_get_string(json, key, dst, cap);
}

static int build_status_json(char *out, size_t cap)
{
    field_bridge_settings_t settings;
    field_bridge_runtime_status_t status;
    char topic[FIELD_BRIDGE_TOPIC_FULL_MAX];

    if (product_config_get_settings(&settings) != 0 ||
        product_runtime_get_status(&status) != 0 ||
        product_topic_build(&settings, "test", topic, sizeof(topic)) != 0) {
        return -1;
    }
    return snprintf(out, cap,
                    "{\"status\":\"ok\",\"network_state\":\"%s\","
                    "\"ip_addr\":\"%s\",\"broker_state\":\"%s\","
                    "\"p2p_role\":\"%s\",\"connected_peers\":%u,"
                    "\"remote_subscriptions\":%u,\"test_topic\":\"%s\","
                    "\"last_error\":\"%s\"}",
                    status.network_state, status.ip_addr,
                    status.broker_state, status.p2p_role,
                    status.connected_peers, status.remote_subscriptions,
                    topic, status.last_error);
}

static int build_config_json(char *out, size_t cap)
{
    field_bridge_settings_t s;

    if (product_config_get_settings(&s) != 0) {
        return -1;
    }
    return snprintf(out, cap,
                    "{\"device_name\":\"%s\",\"mode\":\"%s\","
                    "\"device_ip\":\"%s\",\"gateway\":\"%s\","
                    "\"netmask\":\"%s\",\"dns\":\"%s\","
                    "\"dhcp_enabled\":%u,"
                    "\"broker_ip\":\"%s\",\"site_id\":\"%s\","
                    "\"topic_prefix\":\"%s\",\"mqtt_port\":%u,"
                    "\"p2p_port\":%u,\"broker_enabled\":%u,"
                    "\"bridge_enabled\":%u,\"mesh_enabled\":%u}",
                    s.system.device_name,
                    product_config_network_mode_name(s.network.mode),
                    s.network.device_ip, s.network.gateway,
                    s.network.netmask, s.network.dns,
                    s.network.dhcp_enabled,
                    s.broker.broker_ip, s.broker.site_id,
                    s.broker.topic_prefix, s.broker.mqtt_port,
                    s.broker.p2p_port, s.broker.broker_enabled,
                    s.broker.bridge_enabled, s.broker.mesh_enabled);
}

static int build_peers_json(char *out, size_t cap)
{
    size_t off = 0;
    int n = snprintf(out, cap, "[");

    if (n < 0 || (size_t)n >= cap) {
        return -1;
    }
    off = (size_t)n;
    for (int i = 0; i < FIELD_BRIDGE_PEER_MAX; i++) {
        field_bridge_peer_t p;

        if (product_config_get_peer(i, &p) != 0) {
            return -1;
        }
        n = snprintf(out + off, cap - off,
                     "%s{\"index\":%d,\"name\":\"%s\",\"host\":\"%s\","
                     "\"mqtt_port\":%u,\"p2p_port\":%u,\"enabled\":%u}",
                     i ? "," : "", i, p.name, p.host,
                     p.mqtt_port, p.p2p_port, p.enabled);
        if (n < 0 || (size_t)n >= cap - off) {
            return -1;
        }
        off += (size_t)n;
    }
    n = snprintf(out + off, cap - off, "]");
    return n < 0 || (size_t)n >= cap - off ? -1 : (int)(off + (size_t)n);
}

static int build_peer_status_json(char *out, size_t cap)
{
    field_bridge_peer_status_t peers[FIELD_BRIDGE_PEER_MAX];
    size_t off = 0;
    int count = product_runtime_get_peer_statuses(peers, FIELD_BRIDGE_PEER_MAX);
    int n;

    if (count < 0) {
        return -1;
    }
    n = snprintf(out, cap, "[");
    if (n < 0 || (size_t)n >= cap) {
        return -1;
    }
    off = (size_t)n;
    for (int i = 0; i < count; i++) {
        n = snprintf(out + off, cap - off,
                     "%s{\"index\":%u,\"name\":\"%s\",\"host\":\"%s\","
                     "\"p2p_port\":%u,\"enabled\":%u,\"state\":\"%s\","
                     "\"last_error\":\"%s\"}",
                     i ? "," : "", peers[i].index, peers[i].name,
                     peers[i].host, peers[i].p2p_port, peers[i].enabled,
                     peers[i].state, peers[i].last_error);
        if (n < 0 || (size_t)n >= cap - off) {
            return -1;
        }
        off += (size_t)n;
    }
    n = snprintf(out + off, cap - off, "]");
    return n < 0 || (size_t)n >= cap - off ? -1 : (int)(off + (size_t)n);
}

static int apply_config_json(const char *body, int *reboot_required)
{
    field_bridge_settings_t old_settings;
    field_bridge_settings_t new_settings;
    char mode[16];
    int seen = 0;

    if (product_config_get_settings(&old_settings) != 0) {
        return -1;
    }
    new_settings = old_settings;
    if (json_get_string(body, "device_name", new_settings.system.device_name,
                        sizeof(new_settings.system.device_name)) == 0) {
        seen = 1;
    }
    if (json_get_string(body, "device_ip", new_settings.network.device_ip,
                        sizeof(new_settings.network.device_ip)) == 0) {
        seen = 1;
    }
    if (json_get_string(body, "gateway", new_settings.network.gateway,
                        sizeof(new_settings.network.gateway)) == 0) {
        seen = 1;
    }
    if (json_get_string(body, "netmask", new_settings.network.netmask,
                        sizeof(new_settings.network.netmask)) == 0) {
        seen = 1;
    }
    if (json_get_string(body, "dns", new_settings.network.dns,
                        sizeof(new_settings.network.dns)) == 0) {
        seen = 1;
    }
    if (json_get_string(body, "broker_ip", new_settings.broker.broker_ip,
                        sizeof(new_settings.broker.broker_ip)) == 0) {
        seen = 1;
    }
    if (json_get_string(body, "site_id", new_settings.broker.site_id,
                        sizeof(new_settings.broker.site_id)) == 0) {
        seen = 1;
    }
    if (json_get_string(body, "topic_prefix", new_settings.broker.topic_prefix,
                        sizeof(new_settings.broker.topic_prefix)) == 0) {
        seen = 1;
    }

    if (json_get_string(body, "mode", mode, sizeof(mode)) == 0 &&
        product_config_network_mode_from_name(mode, &new_settings.network.mode) != 0) {
        return -1;
    } else if (json_find_key(body, "mode")) {
        seen = 1;
    }
    if (json_get_boolish(body, "dhcp_enabled", &new_settings.network.dhcp_enabled) == 0) {
        seen = 1;
    }
    if (json_get_boolish(body, "broker_enabled", &new_settings.broker.broker_enabled) == 0) {
        seen = 1;
    }
    if (json_get_boolish(body, "bridge_enabled", &new_settings.broker.bridge_enabled) == 0) {
        seen = 1;
    }
    if (json_get_boolish(body, "mesh_enabled", &new_settings.broker.mesh_enabled) == 0) {
        seen = 1;
    }
    if (json_get_u16(body, "mqtt_port", &new_settings.broker.mqtt_port) == 0) {
        seen = 1;
    }
    if (json_get_u16(body, "p2p_port", &new_settings.broker.p2p_port) == 0) {
        seen = 1;
    }
    if (!seen) {
        return -1;
    }

    if (memcmp(&old_settings, &new_settings, sizeof(old_settings)) == 0) {
        *reboot_required = 0;
        return 0;
    }
    if (product_config_set_settings(&new_settings) != 0) {
        return -1;
    }
    *reboot_required = 1;
    return 0;
}

static int apply_peer_json(int index, const char *body)
{
    field_bridge_peer_t peer;

    if (index < 0 || index >= FIELD_BRIDGE_PEER_MAX ||
        product_config_get_peer(index, &peer) != 0) {
        return -1;
    }
    copy_if_present(peer.name, sizeof(peer.name), body, "name");
    copy_if_present(peer.host, sizeof(peer.host), body, "host");
    (void)json_get_u16(body, "mqtt_port", &peer.mqtt_port);
    (void)json_get_u16(body, "p2p_port", &peer.p2p_port);
    if (peer.p2p_port == 0) {
        peer.p2p_port = 4884;
    }
    (void)json_get_boolish(body, "enabled", &peer.enabled);
    if (product_config_set_peer(index, &peer) != 0) {
        return -1;
    }
    (void)bridge_control_apply_peers();
    return 0;
}

#ifdef __ZEPHYR__
static const struct http_header zephyr_json_headers[] = {
    { "Content-Type", "application/json" },
};

static void strip_query(char *path)
{
    char *query = strchr(path, '?');

    if (query) {
        *query = '\0';
    }
}

static void zephyr_set_json_response(struct http_response_ctx *response_ctx,
                                     enum http_status status,
                                     const char *body)
{
    response_ctx->status = status;
    response_ctx->headers = zephyr_json_headers;
    response_ctx->header_count = ARRAY_SIZE(zephyr_json_headers);
    response_ctx->body = (const uint8_t *)body;
    response_ctx->body_len = strlen(body);
    response_ctx->final_chunk = true;
}

static struct zephyr_http_buffers *zephyr_acquire_buffers(void)
{
    if (zephyr_buffers) {
        return zephyr_buffers;
    }
    zephyr_buffers = k_malloc(sizeof(*zephyr_buffers));
    if (zephyr_buffers) {
        memset(zephyr_buffers, 0, sizeof(*zephyr_buffers));
    }
    return zephyr_buffers;
}

static void zephyr_release_buffers(void)
{
    if (zephyr_buffers) {
        k_free(zephyr_buffers);
        zephyr_buffers = NULL;
    }
}

static int zephyr_copy_request_body(const struct http_request_ctx *request_ctx,
                                    char *request)
{
    if (!request_ctx || !request_ctx->data) {
        request[0] = '\0';
        return 0;
    }
    if (request_ctx->data_len >= REQ_BUF_SIZE) {
        return -ENOSPC;
    }
    memcpy(request, request_ctx->data, request_ctx->data_len);
    request[request_ctx->data_len] = '\0';
    return 0;
}

static int zephyr_dynamic_handler(struct http_client_ctx *client,
                                  enum http_transaction_status status,
                                  const struct http_request_ctx *request_ctx,
                                  struct http_response_ctx *response_ctx,
                                  void *user_data)
{
    char path[128];
    struct zephyr_http_buffers *buffers;
    char *request_buf;
    char *response_buf;
    int n = 0;

    (void)user_data;

    if (status == HTTP_SERVER_TRANSACTION_ABORTED ||
        status == HTTP_SERVER_TRANSACTION_COMPLETE) {
        zephyr_release_buffers();
        return 0;
    }
    if (status != HTTP_SERVER_REQUEST_DATA_FINAL) {
        return 0;
    }

    snprintf(path, sizeof(path), "%s", client->url_buffer);
    strip_query(path);

    if (client->method == HTTP_GET &&
        (strcmp(path, "/") == 0 || strcmp(path, "/index.html") == 0)) {
        response_ctx->status = HTTP_200_OK;
        response_ctx->body = (const uint8_t *)index_html;
        response_ctx->body_len = strlen(index_html);
        response_ctx->final_chunk = true;
        return 0;
    }
    buffers = zephyr_acquire_buffers();
    if (!buffers) {
        zephyr_set_json_response(response_ctx, HTTP_503_SERVICE_UNAVAILABLE,
                                 "{\"status\":\"error\",\"error\":\"low memory\"}");
        return 0;
    }
    request_buf = buffers->request;
    response_buf = buffers->response;
    if (client->method == HTTP_GET && strcmp(path, "/status") == 0) {
        n = build_status_json(response_buf, RESP_BUF_SIZE);
    } else if (client->method == HTTP_GET && strcmp(path, "/config") == 0) {
        n = build_config_json(response_buf, RESP_BUF_SIZE);
    } else if (client->method == HTTP_GET && strcmp(path, "/peers") == 0) {
        n = build_peers_json(response_buf, RESP_BUF_SIZE);
    } else if (client->method == HTTP_GET && strcmp(path, "/peer-status") == 0) {
        n = build_peer_status_json(response_buf, RESP_BUF_SIZE);
    }
    if (n > 0) {
        zephyr_set_json_response(response_ctx, HTTP_200_OK, response_buf);
        return 0;
    }

    if (client->method == HTTP_POST) {
        if (zephyr_copy_request_body(request_ctx, request_buf) != 0) {
            zephyr_set_json_response(response_ctx, HTTP_400_BAD_REQUEST,
                                     "{\"status\":\"error\",\"error\":\"request too large\"}");
            return 0;
        }
        if (strcmp(path, "/config") == 0) {
            int reboot_required = 0;

            if (apply_config_json(request_buf, &reboot_required) != 0) {
                zephyr_set_json_response(response_ctx, HTTP_400_BAD_REQUEST,
                                         "{\"status\":\"error\",\"error\":\"invalid config JSON\"}");
                return 0;
            }
            snprintf(response_buf, RESP_BUF_SIZE,
                     "{\"status\":\"ok\",\"reboot_required\":%s}",
                     reboot_required ? "true" : "false");
            zephyr_set_json_response(response_ctx, HTTP_200_OK, response_buf);
            return 0;
        }
        if (strncmp(path, "/peers/", 7) == 0) {
            int index = atoi(path + 7);

            if (apply_peer_json(index, request_buf) != 0) {
                zephyr_set_json_response(response_ctx, HTTP_400_BAD_REQUEST,
                                         "{\"status\":\"error\",\"error\":\"invalid peer JSON\"}");
                return 0;
            }
            zephyr_set_json_response(response_ctx, HTTP_200_OK, "{\"status\":\"ok\"}");
            return 0;
        }
        if (strcmp(path, "/broker/control") == 0) {
            uint8_t enabled = 0;

            if (json_get_boolish(request_buf, "enabled", &enabled) != 0 ||
                product_runtime_set_broker_enabled(enabled) != 0) {
                zephyr_set_json_response(response_ctx, HTTP_400_BAD_REQUEST,
                                         "{\"status\":\"error\",\"error\":\"invalid broker control\"}");
                return 0;
            }
            zephyr_set_json_response(response_ctx, HTTP_200_OK, "{\"status\":\"requested\"}");
            return 0;
        }
        if (strcmp(path, "/publish-test") == 0) {
            field_bridge_publish_test_t test;

            memset(&test, 0, sizeof(test));
            if (json_get_string(request_buf, "topic", test.topic, sizeof(test.topic)) != 0 ||
                json_get_string(request_buf, "payload", test.payload, sizeof(test.payload)) != 0) {
                zephyr_set_json_response(response_ctx, HTTP_400_BAD_REQUEST,
                                         "{\"status\":\"error\",\"error\":\"invalid publish test\"}");
                return 0;
            }
            (void)json_get_boolish(request_buf, "retain", &test.retain);
            (void)json_get_boolish(request_buf, "qos", &test.qos);
            if (product_runtime_record_publish_test(&test) != 0) {
                zephyr_set_json_response(response_ctx, HTTP_400_BAD_REQUEST,
                                         "{\"status\":\"error\",\"error\":\"invalid publish test\"}");
                return 0;
            }
            zephyr_set_json_response(response_ctx, HTTP_200_OK, "{\"status\":\"recorded\"}");
            return 0;
        }
        if (strcmp(path, "/config/reset") == 0) {
            if (product_config_reset_all() != 0) {
                zephyr_set_json_response(response_ctx, HTTP_400_BAD_REQUEST,
                                         "{\"status\":\"error\",\"error\":\"reset failed\"}");
                return 0;
            }
            zephyr_set_json_response(response_ctx, HTTP_200_OK, "{\"status\":\"reset\"}");
            return 0;
        }
        if (strcmp(path, "/reboot") == 0) {
            zephyr_set_json_response(response_ctx, HTTP_200_OK, "{\"status\":\"rebooting\"}");
            k_work_schedule(&http_reboot_work, K_MSEC(250));
            return 0;
        }
    }

    zephyr_set_json_response(response_ctx, HTTP_404_NOT_FOUND, "{\"error\":\"not found\"}");
    return 0;
}

static struct http_resource_detail_dynamic zephyr_fallback_detail = {
    .common = {
        .type = HTTP_RESOURCE_TYPE_DYNAMIC,
        .bitmask_of_supported_http_methods = BIT(HTTP_GET) | BIT(HTTP_POST),
        .content_type = "text/html",
    },
    .cb = zephyr_dynamic_handler,
};

static uint16_t zephyr_http_port = PROVISIONING_HTTP_PORT;
static const struct http_service_config zephyr_http_config = {
    .http_ver = HTTP_VERSION_1,
};

HTTP_SERVICE_DEFINE_EMPTY(provisioning_http_service, NULL, &zephyr_http_port,
                          1, 1, NULL, &zephyr_fallback_detail.common,
                          &zephyr_http_config);
#else
static void handle_request(int fd, char *req)
{
    char method[8];
    char raw_path[128];
    char path[128];
    char *body_resp = response_buf;
    char *body = strstr(req, "\r\n\r\n");
    char *query;
    int n;

    if (sscanf(req, "%7s %127s", method, raw_path) != 2) {
        http_send(fd, 400, "application/json", "{\"error\":\"bad request\"}");
        return;
    }
    if (strncmp(raw_path, "http://", 7) == 0) {
        char *slash = strchr(raw_path + 7, '/');
        snprintf(path, sizeof(path), "%s", slash ? slash : "/");
    } else {
        snprintf(path, sizeof(path), "%s", raw_path);
    }
    query = strchr(path, '?');
    if (query) {
        *query = '\0';
    }
    body = body ? body + 4 : (char *)"";

    if (strcmp(method, "GET") == 0 &&
        (strcmp(path, "/") == 0 || strcmp(path, "/index.html") == 0)) {
        http_send(fd, 200, "text/html; charset=utf-8", index_html);
        return;
    }
    if (strcmp(method, "GET") == 0 && strcmp(path, "/status") == 0) {
        n = build_status_json(body_resp, RESP_BUF_SIZE);
        http_send(fd, n > 0 ? 200 : 400, "application/json",
                  n > 0 ? body_resp : "{\"error\":\"status unavailable\"}");
        return;
    }
    if (strcmp(method, "GET") == 0 && strcmp(path, "/config") == 0) {
        n = build_config_json(body_resp, RESP_BUF_SIZE);
        http_send(fd, n > 0 ? 200 : 400, "application/json",
                  n > 0 ? body_resp : "{\"error\":\"config unavailable\"}");
        return;
    }
    if (strcmp(method, "GET") == 0 && strcmp(path, "/peers") == 0) {
        n = build_peers_json(body_resp, RESP_BUF_SIZE);
        http_send(fd, n > 0 ? 200 : 400, "application/json",
                  n > 0 ? body_resp : "{\"error\":\"peers unavailable\"}");
        return;
    }
    if (strcmp(method, "GET") == 0 && strcmp(path, "/peer-status") == 0) {
        n = build_peer_status_json(body_resp, RESP_BUF_SIZE);
        http_send(fd, n > 0 ? 200 : 400, "application/json",
                  n > 0 ? body_resp : "{\"error\":\"peer status unavailable\"}");
        return;
    }
    if (strcmp(method, "POST") == 0 && strcmp(path, "/config") == 0) {
        int reboot_required = 0;

        if (apply_config_json(body, &reboot_required) != 0) {
            http_send(fd, 400, "application/json",
                      "{\"status\":\"error\",\"error\":\"invalid config JSON\"}");
            return;
        }
        snprintf(body_resp, RESP_BUF_SIZE,
                 "{\"status\":\"ok\",\"reboot_required\":%s}",
                 reboot_required ? "true" : "false");
        http_send(fd, 200, "application/json", body_resp);
        return;
    }
    if (strcmp(method, "POST") == 0 && strncmp(path, "/peers/", 7) == 0) {
        int index = atoi(path + 7);

        if (apply_peer_json(index, body) != 0) {
            http_send(fd, 400, "application/json",
                      "{\"status\":\"error\",\"error\":\"invalid peer JSON\"}");
            return;
        }
        http_send(fd, 200, "application/json", "{\"status\":\"ok\"}");
        return;
    }
    if (strcmp(method, "POST") == 0 && strcmp(path, "/broker/control") == 0) {
        uint8_t enabled = 0;

        if (json_get_boolish(body, "enabled", &enabled) != 0 ||
            product_runtime_set_broker_enabled(enabled) != 0) {
            http_send(fd, 400, "application/json",
                      "{\"status\":\"error\",\"error\":\"invalid broker control\"}");
            return;
        }
        http_send(fd, 200, "application/json", "{\"status\":\"requested\"}");
        return;
    }
    if (strcmp(method, "POST") == 0 && strcmp(path, "/publish-test") == 0) {
        field_bridge_publish_test_t test;

        memset(&test, 0, sizeof(test));
        if (json_get_string(body, "topic", test.topic, sizeof(test.topic)) != 0 ||
            json_get_string(body, "payload", test.payload, sizeof(test.payload)) != 0) {
            http_send(fd, 400, "application/json",
                      "{\"status\":\"error\",\"error\":\"invalid publish test\"}");
            return;
        }
        (void)json_get_boolish(body, "retain", &test.retain);
        (void)json_get_boolish(body, "qos", &test.qos);
        if (product_runtime_record_publish_test(&test) != 0) {
            http_send(fd, 400, "application/json",
                      "{\"status\":\"error\",\"error\":\"invalid publish test\"}");
            return;
        }
        http_send(fd, 200, "application/json", "{\"status\":\"recorded\"}");
        return;
    }
    if (strcmp(method, "POST") == 0 && strcmp(path, "/config/reset") == 0) {
        if (product_config_reset_all() != 0) {
            http_send(fd, 400, "application/json",
                      "{\"status\":\"error\",\"error\":\"reset failed\"}");
            return;
        }
        http_send(fd, 200, "application/json", "{\"status\":\"reset\"}");
        return;
    }
    if (strcmp(method, "POST") == 0 && strcmp(path, "/reboot") == 0) {
        http_send(fd, 200, "application/json", "{\"status\":\"rebooting\"}");
        return;
    }

    http_send(fd, 404, "application/json", "{\"error\":\"not found\"}");
}

static void serve_once(int fd)
{
    char *req = request_buf;
    int total = 0;
    struct timeval timeout = {
        .tv_sec = 2,
        .tv_usec = 0,
    };

    (void)PH_SETSOCKOPT(fd, SOL_SOCKET, SO_RCVTIMEO,
                        &timeout, sizeof(timeout));

    while (total < REQ_BUF_SIZE - 1) {
        int n = PH_RECV(fd, req + total, REQ_BUF_SIZE - 1 - (size_t)total, 0);

        if (n <= 0) {
            break;
        }
        total += n;
        req[total] = '\0';
        if (strstr(req, "\r\n\r\n")) {
            char *cl = strstr(req, "Content-Length:");
            char *body = strstr(req, "\r\n\r\n");
            int need = 0;

            if (cl) {
                need = atoi(cl + strlen("Content-Length:"));
            }
            if (!body || total >= (int)((body + 4 - req) + need)) {
                break;
            }
        }
    }
    req[total] = '\0';
    handle_request(fd, req);
}

static void *http_thread(void *arg)
{
    (void)arg;

    while (1) {
        struct sockaddr_in client;
        socklen_t len = sizeof(client);
        int fd = PH_ACCEPT(listen_fd, (struct sockaddr *)&client, &len);

        if (fd < 0) {
            continue;
        }
        serve_once(fd);
        PH_CLOSE(fd);
    }
    return NULL;
}
#endif

int provisioning_http_start(void)
{
#ifdef __ZEPHYR__
    int rc;

    if (started) {
        return 0;
    }
    rc = http_server_start();
    if (rc != 0) {
        return rc;
    }
    started = 1;
    LOG_INF("provisioning HTTP listening on port %d", PROVISIONING_HTTP_PORT);
    return 0;
#else
    struct sockaddr_in addr;
    int opt = 1;

    if (started) {
        return 0;
    }

    listen_fd = PH_SOCKET(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listen_fd < 0) {
        return -errno;
    }
    (void)PH_SETSOCKOPT(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(PROVISIONING_HTTP_PORT);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (PH_BIND(listen_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0 ||
        PH_LISTEN(listen_fd, 4) < 0) {
        int err = errno;
        PH_CLOSE(listen_fd);
        listen_fd = -1;
        return -err;
    }

    if (pthread_create(&http_thread_data, NULL, http_thread, NULL) != 0) {
        PH_CLOSE(listen_fd);
        listen_fd = -1;
        return -1;
    }
    pthread_detach(http_thread_data);
    started = 1;
    LOG_INF("provisioning HTTP listening on port %d", PROVISIONING_HTTP_PORT);
    return 0;
#endif
}

#endif
