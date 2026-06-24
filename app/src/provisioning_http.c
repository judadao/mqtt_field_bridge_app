#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <errno.h>

#include <zephyr/logging/log.h>

#include "provisioning_http.h"
#include "product_config.h"
#include "product_runtime.h"
#include "product_topics.h"
#include "product_wifi.h"
#include "bridge_control.h"
#ifndef __ZEPHYR__
#include "generated/provisioning_index.h"
#endif

LOG_MODULE_REGISTER(provisioning_http, LOG_LEVEL_INF);

#ifndef PROVISIONING_HTTP_PORT
#define PROVISIONING_HTTP_PORT 8080
#endif

#ifndef __ZEPHYR__
__attribute__((weak)) int product_wifi_apply_settings(const field_bridge_settings_t *settings)
{
    (void)settings;
    return 0;
}
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
    int len = 0;
    while (*p && *p != '"') {
        char c = *p++;
        if (c == '\\') {
            if (!*p) return -1;
            c = *p++;
            if (c == 'n') c = '\n';
            else if (c == 'r') c = '\r';
            else if (c == 't') c = '\t';
        }
        if (len < cap - 1) {
            out[len++] = c;
        }
    }
    if (*p != '"') return -1;
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
    if (out->enabled > 1) return -1;
    return 0;
}

static int json_decode_settings(const char *json, field_bridge_settings_t *out)
{
    memset(out, 0, sizeof(*out));
    if (json_str_field(json, "device_name", out->system.device_name,
                       sizeof(out->system.device_name)) != 0) return -1;
    if (json_str_field(json, "admin_password", out->system.admin_password,
                       sizeof(out->system.admin_password)) != 0) return -1;
    if (json_str_field(json, "wifi_ssid", out->network.wifi_ssid,
                       sizeof(out->network.wifi_ssid)) != 0) return -1;
    if (json_str_field(json, "wifi_password", out->network.wifi_password,
                       sizeof(out->network.wifi_password)) != 0) return -1;
    if (json_str_field(json, "ap_ssid", out->network.ap_ssid,
                       sizeof(out->network.ap_ssid)) != 0) return -1;
    if (json_str_field(json, "ap_password", out->network.ap_password,
                       sizeof(out->network.ap_password)) != 0) return -1;
    if (json_str_field(json, "device_ip", out->network.device_ip,
                       sizeof(out->network.device_ip)) != 0) return -1;
    if (json_str_field(json, "gateway", out->network.gateway,
                       sizeof(out->network.gateway)) != 0) return -1;
    if (json_str_field(json, "netmask", out->network.netmask,
                       sizeof(out->network.netmask)) != 0) return -1;
    if (json_str_field(json, "dns", out->network.dns,
                       sizeof(out->network.dns)) != 0) return -1;
    if (json_uint8_field(json, "dhcp_enabled", &out->network.dhcp_enabled) != 0) return -1;
    if (json_str_field(json, "site_id", out->broker.site_id,
                       sizeof(out->broker.site_id)) != 0) return -1;
    if (json_str_field(json, "topic_prefix", out->broker.topic_prefix,
                       sizeof(out->broker.topic_prefix)) != 0) return -1;
    if (json_uint16_field(json, "mqtt_port", &out->broker.mqtt_port) != 0) return -1;
    if (json_uint16_field(json, "p2p_port", &out->broker.p2p_port) != 0) return -1;
    if (json_uint8_field(json, "broker_enabled", &out->broker.broker_enabled) != 0) return -1;
    if (json_uint8_field(json, "bridge_enabled", &out->broker.bridge_enabled) != 0) return -1;
    if (json_uint8_field(json, "mesh_enabled", &out->broker.mesh_enabled) != 0) return -1;
    if (out->network.dhcp_enabled > 1 || out->broker.broker_enabled > 1 ||
        out->broker.bridge_enabled > 1 || out->broker.mesh_enabled > 1) return -1;
    return 0;
}

static int json_decode_broker_control(const char *json, uint8_t *enabled)
{
    if (json_uint8_field(json, "enabled", enabled) != 0) return -1;
    if (*enabled > 1) return -1;
    return 0;
}

static int json_decode_publish_test(const char *json, field_bridge_publish_test_t *out)
{
    memset(out, 0, sizeof(*out));
    if (json_str_field(json, "topic", out->topic, sizeof(out->topic)) != 0) return -1;
    if (json_str_field(json, "payload", out->payload, sizeof(out->payload)) != 0) return -1;
    if (json_uint8_field(json, "qos", &out->qos) != 0) return -1;
    if (json_uint8_field(json, "retain", &out->retain) != 0) return -1;
    if (out->qos > 1 || out->retain > 1) return -1;
    return 0;
}

typedef struct {
    field_bridge_wifi_entry_t entry;
    int peer_index;
} bridge_wifi_join_req_t;

static int json_decode_bridge_wifi_join(const char *json,
                                        bridge_wifi_join_req_t *out)
{
    uint16_t peer_index;

    memset(out, 0, sizeof(*out));
    if (json_str_field(json, "ssid", out->entry.ssid,
                       sizeof(out->entry.ssid)) != 0) return -1;
    json_str_field(json, "password", out->entry.password,
                   sizeof(out->entry.password));
    json_str_field(json, "peer_name", out->entry.peer_name,
                   sizeof(out->entry.peer_name));
    if (json_str_field(json, "host", out->entry.host,
                       sizeof(out->entry.host)) != 0) return -1;
    if (json_uint16_field(json, "mqtt_port",
                          &out->entry.mqtt_port) != 0) return -1;
    if (json_uint16_field(json, "p2p_port",
                          &out->entry.p2p_port) != 0) return -1;
    if (json_uint16_field(json, "peer_index", &peer_index) != 0) return -1;
    if (peer_index >= FIELD_BRIDGE_PEER_MAX) return -1;
    out->peer_index = (int)peer_index;
    return 0;
}

static void resolve_bridge_wifi_mock_ips(const field_bridge_wifi_entry_t *entry,
                                         char *local_sta_ip, size_t local_cap,
                                         char *gateway_ip, size_t gateway_cap)
{
#ifndef __ZEPHYR__
    snprintf(local_sta_ip, local_cap, "%s", "127.0.0.10");
    snprintf(gateway_ip, gateway_cap, "%s", entry->host);
#else
    /*
     * ESP32 implementation should fill these from the STA netif after DHCP:
     * local_sta_ip = this node's STA address, gateway_ip = selected AP address.
     */
    snprintf(local_sta_ip, local_cap, "%s", "");
    snprintf(gateway_ip, gateway_cap, "%s", entry->host);
#endif
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
#include <sys/time.h>
#include <netinet/in.h>
#define PLAT_SEND(fd, buf, len) send((fd), (buf), (size_t)(len), MSG_NOSIGNAL)
#define PLAT_RECV(fd, buf, len) recv((fd), (buf), (size_t)(len), 0)
#define PLAT_CLOSE(fd)          close(fd)
#else
#include <zephyr/kernel.h>
#include <zephyr/net/socket.h>
#define PLAT_SEND(fd, buf, len) zsock_send((fd), (buf), (size_t)(len), 0)
#define PLAT_RECV(fd, buf, len) zsock_recv((fd), (buf), (size_t)(len), 0)
#define PLAT_CLOSE(fd)          zsock_close(fd)
#endif

#define PEERS_JSON_BUF_SIZE 4096
static char peers_json_buf[PEERS_JSON_BUF_SIZE];
#define CONFIG_JSON_BUF_SIZE 2048
static char config_json_buf[CONFIG_JSON_BUF_SIZE];
#define HTTP_SEND_CHUNK_SIZE 512
#define HTTP_COMBINED_RESPONSE_SIZE 1536
static char combined_response_buf[HTTP_COMBINED_RESPONSE_SIZE];
static char session_token[32];
static unsigned session_seq;

#ifdef __ZEPHYR__
static const char index_lite_html[] =
"<!doctype html><html><head><meta name=viewport content=\"width=device-width,initial-scale=1\">"
"<title>Field Bridge Settings</title><style>"
":root{--b:#eef2f5;--f:#17212b;--m:#60717d;--p:#fff;--l:#cfd9e2;--a:#006a6a;--d:#b3261e}"
"@media(prefers-color-scheme:dark){:root{--b:#101417;--f:#eef3f7;--m:#a7b3bb;--p:#171c20;--l:#33424a;--a:#80cbc4;--d:#ffb4ab}}"
"*{box-sizing:border-box}body{margin:0;background:var(--b);color:var(--f);font:14px/1.45 system-ui,Segoe UI,sans-serif}"
"header{background:var(--p);border-bottom:1px solid var(--l);padding:14px 16px}main{max-width:760px;margin:auto;padding:14px}"
"h1{margin:0;font-size:22px}.sub{color:var(--m);font-size:12px}.card{background:var(--p);border:1px solid var(--l);border-radius:8px;padding:12px;margin:0 0 12px}"
"table{width:100%;border-collapse:collapse}th,td{text-align:left;border-bottom:1px solid var(--l);padding:7px}th{width:38%;color:var(--m);font-size:12px}"
"tr:last-child th,tr:last-child td{border:0}.row{display:flex;gap:8px;flex-wrap:wrap;align-items:center}input,textarea{width:100%;border:1px solid var(--l);border-radius:6px;background:var(--b);color:var(--f);padding:8px}"
"input{max-width:220px}button{border:1px solid var(--a);background:var(--a);color:white;border-radius:6px;padding:8px 12px;font-weight:700}button.alt{background:transparent;color:var(--a)}"
"textarea{min-height:150px;font:12px ui-monospace,monospace}pre{white-space:pre-wrap;word-break:break-word;background:var(--b);border:1px solid var(--l);border-radius:6px;padding:10px;min-height:84px}"
".pill{display:inline-block;border:1px solid var(--l);border-radius:99px;padding:4px 9px;color:var(--m)}.ok{color:#2e7d32}.err{color:var(--d)}"
"@media(max-width:520px){main{padding:10px}.row button,.row input{width:100%;max-width:none}}</style></head>"
"<body><header><h1>Field Bridge Settings</h1><div class=sub>ESP32 min broker local console</div></header><main>"
"<section class=card><div class=row><input id=p type=password value=admin placeholder=\"admin password\">"
"<button onclick=L()>Login</button><button class=alt onclick=S()>Refresh</button><span id=s class=pill>booting</span></div></section>"
"<section class=card><table><tbody id=t><tr><th>Status</th><td>Loading</td></tr></tbody></table></section>"
"<section class=card><div class=row><button onclick=C()>Load Config</button><button onclick=V()>Save Config</button>"
"<button class=alt onclick=W()>Scan WiFi</button></div><p class=sub>JSON config stays editable for debug.</p><textarea id=c spellcheck=false></textarea></section>"
"<section class=card><b>Output</b><pre id=o></pre></section>"
"<script>let T='',E=id=>document.getElementById(id);"
"async function R(u,m,b){let h=T?{'X-Auth-Token':T}:{};if(b)h['Content-Type']='application/json';"
"let r=await fetch(u,{method:m||'GET',headers:h,body:b}),x=await r.text();if(!r.ok)throw x;return x}"
"function O(x,k){E('o').textContent=x;E('s').textContent=k||'OK';E('s').className='pill '+(k=='ERR'?'err':'ok')}"
"function P(x){x.then(v=>O(v)).catch(e=>O(e,'ERR'))}"
"function TBL(j){let r=JSON.parse(j);E('t').innerHTML=['wifi_state','ip_addr','broker_state','p2p_role','connected_peers','remote_subscriptions','last_error'].map(k=>'<tr><th>'+k+'</th><td>'+(r[k]||'-')+'</td></tr>').join('')}"
"function S(){P(R('/status').then(x=>(TBL(x),x)))}"
"function L(){P(R('/login','POST',JSON.stringify({password:E('p').value})).then(x=>(T=JSON.parse(x).token,'logged in')))}"
"function C(){P(R('/config').then(x=>(E('c').value=JSON.stringify(JSON.parse(x),null,2),x)))}"
"function V(){P(R('/config','POST',E('c').value))}"
"function W(){P(R('/wifi/scan'))}S()</script></main></body></html>";
#endif

/* ── Response / route handlers ───────────────────────────────────────────── */

static int send_all_bytes(int fd, const char *buf, int len)
{
    int sent = 0;

    while (sent < len) {
        int chunk = len - sent;
        ssize_t n;

        if (chunk > HTTP_SEND_CHUNK_SIZE) {
            chunk = HTTP_SEND_CHUNK_SIZE;
        }
        n = PLAT_SEND(fd, buf + sent, chunk);
        if (n <= 0) {
            LOG_WRN("HTTP send failed after %d/%d bytes: %d", sent, len, errno);
            return -1;
        }
        sent += (int)n;
#ifdef __ZEPHYR__
        k_sleep(K_MSEC(2));
#endif
    }
    return 0;
}

static void send_response_type(int fd, int status, const char *content_type,
                               const char *body)
{
    const char *reason = (status == 200) ? "OK" :
                         (status == 400) ? "Bad Request" :
                         (status == 403) ? "Forbidden" :
                         (status == 409) ? "Conflict" :
                         (status == 404) ? "Not Found" : "Internal Error";
    int body_len = body ? (int)strlen(body) : 0;
    char hdr[256];
    int hlen = snprintf(hdr, sizeof(hdr),
                        "HTTP/1.0 %d %s\r\n"
                        "Content-Type: %s\r\n"
                        "Content-Length: %d\r\n"
                        "Cache-Control: no-store\r\n"
                        "\r\n",
                        status, reason, content_type, body_len);
    if (hlen <= 0 || hlen >= (int)sizeof(hdr)) {
        return;
    }

    if (body_len > 0 &&
        hlen + body_len <= (int)sizeof(combined_response_buf)) {
        memcpy(combined_response_buf, hdr, (size_t)hlen);
        memcpy(combined_response_buf + hlen, body, (size_t)body_len);
        (void)send_all_bytes(fd, combined_response_buf, hlen + body_len);
        return;
    }

    if (send_all_bytes(fd, hdr, hlen) == 0 && body_len > 0) {
        (void)send_all_bytes(fd, body, body_len);
    }
}

static void send_json(int fd, int status, const char *body)
{
    send_response_type(fd, status, "application/json", body);
}

static void send_html(int fd, int status, const char *body)
{
    send_response_type(fd, status, "text/html; charset=utf-8", body);
}

static void send_index_page(int fd)
{
#ifdef __ZEPHYR__
    send_html(fd, 200, index_lite_html);
#else
    send_html(fd, 200, index_html);
#endif
}

static int append_json_str(char *buf, int cap, int *pos, const char *s);

static int request_has_auth(const char *headers)
{
    char search[64];

    if (session_token[0] == '\0') {
        return 0;
    }
    snprintf(search, sizeof(search), "X-Auth-Token: %s", session_token);
    return strstr(headers, search) != NULL;
}

static void issue_session_token(const char *password)
{
    unsigned mix = 2166136261u;

    while (*password) {
        mix ^= (unsigned char)*password++;
        mix *= 16777619u;
    }
    session_seq++;
    snprintf(session_token, sizeof(session_token), "%08x%08x", session_seq, mix);
}

static void handle_get_status(int fd)
{
    field_bridge_runtime_status_t status;
    char buf[512];

    if (product_runtime_get_status(&status) != 0) {
        send_json(fd, 500, "{\"error\":\"status unavailable\"}");
        return;
    }
    int n = snprintf(buf, sizeof(buf),
                     "{\"status\":\"ok\",\"peers\":%d,"
                     "\"wifi_state\":",
                     product_config_peer_count());
    if (n <= 0 || n >= (int)sizeof(buf)) {
        send_json(fd, 500, "{\"error\":\"status too large\"}");
        return;
    }
    int pos = n;
    if (append_json_str(buf, sizeof(buf), &pos, status.wifi_state) != 0) goto overflow;
    n = snprintf(buf + pos, sizeof(buf) - (size_t)pos, ",\"ip_addr\":");
    if (n <= 0 || n >= (int)(sizeof(buf) - (size_t)pos)) goto overflow;
    pos += n;
    if (append_json_str(buf, sizeof(buf), &pos, status.ip_addr) != 0) goto overflow;
    n = snprintf(buf + pos, sizeof(buf) - (size_t)pos, ",\"broker_state\":");
    if (n <= 0 || n >= (int)(sizeof(buf) - (size_t)pos)) goto overflow;
    pos += n;
    if (append_json_str(buf, sizeof(buf), &pos, status.broker_state) != 0) goto overflow;
    n = snprintf(buf + pos, sizeof(buf) - (size_t)pos, ",\"p2p_role\":");
    if (n <= 0 || n >= (int)(sizeof(buf) - (size_t)pos)) goto overflow;
    pos += n;
    if (append_json_str(buf, sizeof(buf), &pos, status.p2p_role) != 0) goto overflow;
    n = snprintf(buf + pos, sizeof(buf) - (size_t)pos,
                 ",\"connected_peers\":%u,\"remote_subscriptions\":%u,"
                 "\"last_error\":",
                 status.connected_peers, status.remote_subscriptions);
    if (n <= 0 || n >= (int)(sizeof(buf) - (size_t)pos)) goto overflow;
    pos += n;
    if (append_json_str(buf, sizeof(buf), &pos, status.last_error) != 0) goto overflow;
    field_bridge_settings_t settings;
    char test_topic[FIELD_BRIDGE_TOPIC_FULL_MAX];
    if (product_config_get_settings(&settings) == 0 &&
        product_topic_test(&settings, test_topic, sizeof(test_topic)) == 0) {
        n = snprintf(buf + pos, sizeof(buf) - (size_t)pos, ",\"test_topic\":");
        if (n <= 0 || n >= (int)(sizeof(buf) - (size_t)pos)) goto overflow;
        pos += n;
        if (append_json_str(buf, sizeof(buf), &pos, test_topic) != 0) goto overflow;
    }
    n = snprintf(buf + pos, sizeof(buf) - (size_t)pos, "}");
    if (n <= 0 || n >= (int)(sizeof(buf) - (size_t)pos)) goto overflow;
    send_json(fd, 200, buf);
    return;

overflow:
    send_json(fd, 500, "{\"error\":\"status too large\"}");
}

static int append_json_str(char *buf, int cap, int *pos, const char *s)
{
    if (*pos >= cap) return -1;
    buf[(*pos)++] = '"';
    while (*s && *pos < cap - 2) {
        unsigned char c = (unsigned char)*s++;
        if (c == '"' || c == '\\') {
            if (*pos >= cap - 3) return -1;
            buf[(*pos)++] = '\\';
            buf[(*pos)++] = (char)c;
        } else if (c >= 0x20) {
            buf[(*pos)++] = (char)c;
        }
    }
    if (*pos >= cap) return -1;
    buf[(*pos)++] = '"';
    buf[*pos] = '\0';
    return 0;
}

static void handle_login(int fd, const char *body)
{
    char password[FIELD_BRIDGE_PASSWORD_MAX];

    if (json_str_field(body, "password", password, sizeof(password)) != 0) {
        send_json(fd, 400, "{\"error\":\"missing password\"}");
        return;
    }
    if (!product_config_check_admin_password(password)) {
        send_json(fd, 403, "{\"error\":\"login failed\"}");
        return;
    }
    issue_session_token(password);
    char resp_body[80];
    snprintf(resp_body, sizeof(resp_body),
             "{\"status\":\"ok\",\"token\":\"%s\"}", session_token);
    send_json(fd, 200, resp_body);
}

static void handle_get_config(int fd)
{
    field_bridge_settings_t s;
    char *buf = config_json_buf;
    const int cap = CONFIG_JSON_BUF_SIZE;
    int pos = 0;
    int written;

    if (product_config_get_settings(&s) != 0) {
        send_json(fd, 500, "{\"error\":\"config unavailable\"}");
        return;
    }

    written = snprintf(buf + pos, (size_t)(cap - pos), "{\"device_name\":");
    if (written < 0 || written >= cap - pos) goto overflow;
    pos += written;
    if (append_json_str(buf, cap, &pos, s.system.device_name) != 0) goto overflow;
    written = snprintf(buf + pos, (size_t)(cap - pos), ",\"admin_password\":");
    if (written < 0 || written >= cap - pos) goto overflow;
    pos += written;
    if (append_json_str(buf, cap, &pos, s.system.admin_password) != 0) goto overflow;
    written = snprintf(buf + pos, (size_t)(cap - pos), ",\"wifi_ssid\":");
    if (written < 0 || written >= cap - pos) goto overflow;
    pos += written;
    if (append_json_str(buf, cap, &pos, s.network.wifi_ssid) != 0) goto overflow;
    written = snprintf(buf + pos, (size_t)(cap - pos), ",\"wifi_password\":");
    if (written < 0 || written >= cap - pos) goto overflow;
    pos += written;
    if (append_json_str(buf, cap, &pos, s.network.wifi_password) != 0) goto overflow;
    written = snprintf(buf + pos, (size_t)(cap - pos), ",\"ap_ssid\":");
    if (written < 0 || written >= cap - pos) goto overflow;
    pos += written;
    if (append_json_str(buf, cap, &pos, s.network.ap_ssid) != 0) goto overflow;
    written = snprintf(buf + pos, (size_t)(cap - pos), ",\"ap_password\":");
    if (written < 0 || written >= cap - pos) goto overflow;
    pos += written;
    if (append_json_str(buf, cap, &pos, s.network.ap_password) != 0) goto overflow;
    written = snprintf(buf + pos, (size_t)(cap - pos), ",\"device_ip\":");
    if (written < 0 || written >= cap - pos) goto overflow;
    pos += written;
    if (append_json_str(buf, cap, &pos, s.network.device_ip) != 0) goto overflow;
    written = snprintf(buf + pos, (size_t)(cap - pos), ",\"gateway\":");
    if (written < 0 || written >= cap - pos) goto overflow;
    pos += written;
    if (append_json_str(buf, cap, &pos, s.network.gateway) != 0) goto overflow;
    written = snprintf(buf + pos, (size_t)(cap - pos), ",\"netmask\":");
    if (written < 0 || written >= cap - pos) goto overflow;
    pos += written;
    if (append_json_str(buf, cap, &pos, s.network.netmask) != 0) goto overflow;
    written = snprintf(buf + pos, (size_t)(cap - pos), ",\"dns\":");
    if (written < 0 || written >= cap - pos) goto overflow;
    pos += written;
    if (append_json_str(buf, cap, &pos, s.network.dns) != 0) goto overflow;
    written = snprintf(buf + pos, (size_t)(cap - pos),
                       ",\"dhcp_enabled\":%u,\"site_id\":",
                       s.network.dhcp_enabled);
    if (written < 0 || written >= cap - pos) goto overflow;
    pos += written;
    if (append_json_str(buf, cap, &pos, s.broker.site_id) != 0) goto overflow;
    written = snprintf(buf + pos, (size_t)(cap - pos), ",\"topic_prefix\":");
    if (written < 0 || written >= cap - pos) goto overflow;
    pos += written;
    if (append_json_str(buf, cap, &pos, s.broker.topic_prefix) != 0) goto overflow;
    written = snprintf(buf + pos, (size_t)(cap - pos),
                       ",\"mqtt_port\":%u,\"p2p_port\":%u,"
                       "\"broker_enabled\":%u,\"bridge_enabled\":%u,"
                       "\"mesh_enabled\":%u}",
                       s.broker.mqtt_port, s.broker.p2p_port,
                       s.broker.broker_enabled, s.broker.bridge_enabled,
                       s.broker.mesh_enabled);
    if (written < 0 || written >= cap - pos) goto overflow;
    send_json(fd, 200, buf);
    return;

overflow:
    send_json(fd, 500, "{\"error\":\"config too large\"}");
}

static void handle_post_config(int fd, const char *body)
{
    field_bridge_settings_t settings;

    if (json_decode_settings(body, &settings) != 0) {
        send_json(fd, 400, "{\"error\":\"invalid config JSON\"}");
        return;
    }
    if (product_config_set_settings(&settings) != 0) {
        send_json(fd, 500, "{\"error\":\"persist failed\"}");
        return;
    }
    (void)product_wifi_apply_settings(&settings);
    product_runtime_network_start(&settings);
    bridge_control_apply_peers();
    send_json(fd, 200, "{\"status\":\"ok\"}");
}

static void handle_config_reset(int fd)
{
    if (product_config_reset_all() != 0) {
        send_json(fd, 500, "{\"error\":\"reset failed\"}");
        return;
    }
    session_token[0] = '\0';
    product_runtime_init();
    field_bridge_settings_t settings;
    if (product_config_get_settings(&settings) == 0) {
        product_runtime_network_start(&settings);
    }
    send_json(fd, 200, "{\"status\":\"reset\"}");
}

static void handle_broker_control(int fd, const char *body)
{
    uint8_t enabled;

    if (json_decode_broker_control(body, &enabled) != 0) {
        send_json(fd, 400, "{\"error\":\"invalid broker control JSON\"}");
        return;
    }
    if (product_runtime_set_broker_enabled(enabled) != 0) {
        send_json(fd, 400, "{\"error\":\"invalid broker state\"}");
        return;
    }
    send_json(fd, 200, enabled ? "{\"status\":\"requested\"}" : "{\"status\":\"stopped\"}");
}

static void handle_publish_test(int fd, const char *body)
{
    field_bridge_publish_test_t test;
    field_bridge_settings_t settings;

    if (json_decode_publish_test(body, &test) != 0) {
        send_json(fd, 400, "{\"error\":\"invalid publish test JSON\"}");
        return;
    }
    if (product_config_get_settings(&settings) != 0 ||
        !product_topic_matches_prefix(&settings, test.topic)) {
        send_json(fd, 400, "{\"error\":\"topic outside configured prefix\"}");
        return;
    }
    if (product_runtime_record_publish_test(&test) != 0) {
        send_json(fd, 400, "{\"error\":\"publish test rejected\"}");
        return;
    }
    send_json(fd, 200, "{\"status\":\"recorded\"}");
}

static void handle_get_peers(int fd)
{
    char *buf = peers_json_buf;
    const int cap = PEERS_JSON_BUF_SIZE;
    int pos = 0;
    buf[pos++] = '[';
    for (int i = 0; i < FIELD_BRIDGE_PEER_MAX; i++) {
        field_bridge_peer_t p;
        if (product_config_get_peer(i, &p) != 0) continue;
        if (i > 0) buf[pos++] = ',';
        int written = snprintf(buf + pos, (size_t)(cap - pos),
                               "{\"name\":");
        if (written < 0 || written >= cap - pos) break;
        pos += written;
        if (append_json_str(buf, cap, &pos, p.name) != 0) break;
        written = snprintf(buf + pos, (size_t)(cap - pos),
                           ",\"host\":");
        if (written < 0 || written >= cap - pos) break;
        pos += written;
        if (append_json_str(buf, cap, &pos, p.host) != 0) break;
        written = snprintf(buf + pos, (size_t)(cap - pos),
                           ",\"mqtt_port\":%u,\"p2p_port\":%u,\"enabled\":%u}",
                           p.mqtt_port, p.p2p_port, p.enabled);
        if (written < 0 || written >= cap - pos) break;
        pos += written;
    }
    buf[pos++] = ']';
    buf[pos]   = '\0';
    send_json(fd, 200, buf);
}

static void handle_get_peer_status(int fd)
{
    field_bridge_peer_status_t statuses[FIELD_BRIDGE_PEER_MAX];
    char *buf = peers_json_buf;
    const int cap = PEERS_JSON_BUF_SIZE;
    int count = product_runtime_get_peer_statuses(statuses, FIELD_BRIDGE_PEER_MAX);
    int pos = 0;

    if (count < 0) {
        send_json(fd, 500, "{\"error\":\"peer status unavailable\"}");
        return;
    }

    buf[pos++] = '[';
    for (int i = 0; i < count; i++) {
        int written;
        if (i > 0) buf[pos++] = ',';
        written = snprintf(buf + pos, (size_t)(cap - pos),
                           "{\"index\":%u,\"name\":",
                           statuses[i].index);
        if (written < 0 || written >= cap - pos) goto overflow;
        pos += written;
        if (append_json_str(buf, cap, &pos, statuses[i].name) != 0) goto overflow;
        written = snprintf(buf + pos, (size_t)(cap - pos), ",\"host\":");
        if (written < 0 || written >= cap - pos) goto overflow;
        pos += written;
        if (append_json_str(buf, cap, &pos, statuses[i].host) != 0) goto overflow;
        written = snprintf(buf + pos, (size_t)(cap - pos),
                           ",\"p2p_port\":%u,\"enabled\":%u,\"state\":",
                           statuses[i].p2p_port, statuses[i].enabled);
        if (written < 0 || written >= cap - pos) goto overflow;
        pos += written;
        if (append_json_str(buf, cap, &pos, statuses[i].state) != 0) goto overflow;
        written = snprintf(buf + pos, (size_t)(cap - pos), ",\"last_error\":");
        if (written < 0 || written >= cap - pos) goto overflow;
        pos += written;
        if (append_json_str(buf, cap, &pos, statuses[i].last_error) != 0) goto overflow;
        if (pos >= cap - 2) goto overflow;
        buf[pos++] = '}';
        buf[pos] = '\0';
    }
    if (pos >= cap - 2) goto overflow;
    buf[pos++] = ']';
    buf[pos] = '\0';
    send_json(fd, 200, buf);
    return;

overflow:
    send_json(fd, 500, "{\"error\":\"peer status too large\"}");
}

static int append_bridge_wifi_entry(char *buf, int cap, int *pos,
                                    const field_bridge_wifi_entry_t *entry)
{
    int written = snprintf(buf + *pos, (size_t)(cap - *pos),
                           "{\"ssid\":");
    if (written < 0 || written >= cap - *pos) return -1;
    *pos += written;
    if (append_json_str(buf, cap, pos, entry->ssid) != 0) return -1;
    written = snprintf(buf + *pos, (size_t)(cap - *pos), ",\"peer_name\":");
    if (written < 0 || written >= cap - *pos) return -1;
    *pos += written;
    if (append_json_str(buf, cap, pos, entry->peer_name) != 0) return -1;
    written = snprintf(buf + *pos, (size_t)(cap - *pos), ",\"host\":");
    if (written < 0 || written >= cap - *pos) return -1;
    *pos += written;
    if (append_json_str(buf, cap, pos, entry->host) != 0) return -1;
    written = snprintf(buf + *pos, (size_t)(cap - *pos),
                       ",\"mqtt_port\":%u,\"p2p_port\":%u}",
                       entry->mqtt_port, entry->p2p_port);
    if (written < 0 || written >= cap - *pos) return -1;
    *pos += written;
    return 0;
}

static void handle_wifi_scan(int fd)
{
    field_bridge_wifi_state_t state;
    if (product_config_get_bridge_wifi(&state) != 0) {
        send_json(fd, 500, "{\"error\":\"bridge wifi unavailable\"}");
        return;
    }
    if (!state.enabled) {
        send_json(fd, 409, "{\"error\":\"bridge wifi disabled\"}");
        return;
    }
#ifndef __ZEPHYR__
    send_json(fd, 200,
              "[{\"ssid\":\"MQTT-BRIDGE-node1\",\"peer_name\":\"node1\","
              "\"host\":\"127.0.0.2\",\"mqtt_port\":11883,"
              "\"p2p_port\":14884,\"rssi\":-51,\"channel\":1,"
              "\"security\":\"wpa2\"},"
              "{\"ssid\":\"MQTT-BRIDGE-node2\",\"peer_name\":\"node2\","
              "\"host\":\"127.0.0.3\",\"mqtt_port\":11884,"
              "\"p2p_port\":14886,\"rssi\":-63,\"channel\":6,"
              "\"security\":\"wpa2\"},"
              "{\"ssid\":\"MQTT-BRIDGE-node3\",\"peer_name\":\"node3\","
              "\"host\":\"127.0.0.4\",\"mqtt_port\":11885,"
              "\"p2p_port\":14888,\"rssi\":-70,\"channel\":11,"
              "\"security\":\"wpa2\"},"
              "{\"ssid\":\"MQTT-BRIDGE-node4\",\"peer_name\":\"node4\","
              "\"host\":\"127.0.0.5\",\"mqtt_port\":11886,"
              "\"p2p_port\":14890,\"rssi\":-72,\"channel\":3,"
              "\"security\":\"wpa2\"},"
              "{\"ssid\":\"MQTT-BRIDGE-node5\",\"peer_name\":\"node5\","
              "\"host\":\"127.0.0.6\",\"mqtt_port\":11887,"
              "\"p2p_port\":14892,\"rssi\":-74,\"channel\":4,"
              "\"security\":\"wpa2\"},"
              "{\"ssid\":\"MQTT-BRIDGE-node6\",\"peer_name\":\"node6\","
              "\"host\":\"127.0.0.7\",\"mqtt_port\":11888,"
              "\"p2p_port\":14894,\"rssi\":-76,\"channel\":5,"
              "\"security\":\"wpa2\"},"
              "{\"ssid\":\"MQTT-BRIDGE-node7\",\"peer_name\":\"node7\","
              "\"host\":\"127.0.0.8\",\"mqtt_port\":11889,"
              "\"p2p_port\":14896,\"rssi\":-78,\"channel\":7,"
              "\"security\":\"wpa2\"},"
              "{\"ssid\":\"MQTT-BRIDGE-node8\",\"peer_name\":\"node8\","
              "\"host\":\"127.0.0.9\",\"mqtt_port\":11890,"
              "\"p2p_port\":14898,\"rssi\":-80,\"channel\":8,"
              "\"security\":\"wpa2\"},"
              "{\"ssid\":\"MQTT-BRIDGE-node9\",\"peer_name\":\"node9\","
              "\"host\":\"127.0.0.10\",\"mqtt_port\":11891,"
              "\"p2p_port\":14900,\"rssi\":-82,\"channel\":9,"
              "\"security\":\"wpa2\"},"
              "{\"ssid\":\"MQTT-BRIDGE-node10\",\"peer_name\":\"node10\","
              "\"host\":\"127.0.0.11\",\"mqtt_port\":11892,"
              "\"p2p_port\":14902,\"rssi\":-84,\"channel\":10,"
              "\"security\":\"wpa2\"}]");
#else
    char *buf = peers_json_buf;
    int rc = product_wifi_scan_json(buf, PEERS_JSON_BUF_SIZE);
    if (rc != 0) {
        char err[80];

        snprintf(err, sizeof(err),
                 "{\"error\":\"wifi scan failed\",\"code\":%d}", rc);
        send_json(fd, 500, err);
        return;
    }
    send_json(fd, 200, buf);
#endif
}

static void handle_bridge_wifi_current(int fd)
{
    field_bridge_wifi_state_t state;
    char *buf = config_json_buf;
    const int cap = CONFIG_JSON_BUF_SIZE;
    int pos = 0;
    int written;

    if (product_config_get_bridge_wifi(&state) != 0) {
        send_json(fd, 500, "{\"error\":\"bridge wifi unavailable\"}");
        return;
    }

    written = snprintf(buf + pos, (size_t)(cap - pos),
                       "{\"enabled\":%u,\"connected\":%u,\"status\":\"%s\","
                       "\"current\":",
                       state.enabled, state.connected,
                       state.connected ? "connected" : "disconnected");
    if (written < 0 || written >= cap - pos) goto overflow;
    pos += written;
    if (append_bridge_wifi_entry(buf, cap, &pos, &state.current) != 0) goto overflow;
    written = snprintf(buf + pos, (size_t)(cap - pos),
                       ",\"local_sta_ip\":");
    if (written < 0 || written >= cap - pos) goto overflow;
    pos += written;
    if (append_json_str(buf, cap, &pos, state.local_sta_ip) != 0) goto overflow;
    written = snprintf(buf + pos, (size_t)(cap - pos),
                       ",\"gateway_ip\":");
    if (written < 0 || written >= cap - pos) goto overflow;
    pos += written;
    if (append_json_str(buf, cap, &pos, state.gateway_ip) != 0) goto overflow;
    written = snprintf(buf + pos, (size_t)(cap - pos),
                       ",\"peer_broker_ip\":");
    if (written < 0 || written >= cap - pos) goto overflow;
    pos += written;
    if (append_json_str(buf, cap, &pos, state.current.host) != 0) goto overflow;
    written = snprintf(buf + pos, (size_t)(cap - pos), ",\"last_error\":");
    if (written < 0 || written >= cap - pos) goto overflow;
    pos += written;
    if (append_json_str(buf, cap, &pos, state.last_error) != 0) goto overflow;
    written = snprintf(buf + pos, (size_t)(cap - pos), ",\"last_event\":");
    if (written < 0 || written >= cap - pos) goto overflow;
    pos += written;
    if (append_json_str(buf, cap, &pos, state.last_event) != 0) goto overflow;
    if (pos >= cap - 2) goto overflow;
    buf[pos++] = '}';
    buf[pos] = '\0';
    send_json(fd, 200, buf);
    return;

overflow:
    send_json(fd, 500, "{\"error\":\"bridge wifi current too large\"}");
}

static void handle_bridge_wifi_enabled(int fd, const char *body)
{
    field_bridge_wifi_state_t state;
    uint8_t enabled;

    if (json_decode_broker_control(body, &enabled) != 0) {
        send_json(fd, 400, "{\"error\":\"invalid bridge wifi enabled JSON\"}");
        return;
    }
    if (product_config_get_bridge_wifi(&state) != 0) {
        send_json(fd, 500, "{\"error\":\"bridge wifi unavailable\"}");
        return;
    }
    state.enabled = enabled;
    state.connected = 0;
    state.last_error[0] = '\0';
    snprintf(state.last_event, sizeof(state.last_event),
             enabled ? "bridge wifi enabled" : "bridge wifi disabled");
    if (product_config_set_bridge_wifi(&state) != 0) {
        send_json(fd, 500, "{\"error\":\"bridge wifi persist failed\"}");
        return;
    }
    send_json(fd, 200, enabled ? "{\"status\":\"enabled\"}" :
              "{\"status\":\"disabled\"}");
}

static void handle_bridge_wifi_disconnect(int fd)
{
    field_bridge_wifi_state_t state;
    field_bridge_peer_t peer;

    if (product_config_get_bridge_wifi(&state) != 0) {
        send_json(fd, 500, "{\"error\":\"bridge wifi unavailable\"}");
        return;
    }

    for (int i = 0; i < FIELD_BRIDGE_PEER_MAX; i++) {
        if (product_config_get_peer(i, &peer) != 0) {
            continue;
        }
        if (peer.enabled &&
            strcmp(peer.host, state.current.host) == 0 &&
            peer.mqtt_port == state.current.mqtt_port &&
            peer.p2p_port == state.current.p2p_port) {
            memset(&peer, 0, sizeof(peer));
            (void)product_config_set_peer(i, &peer);
        }
    }

    state.connected = 0;
    state.local_sta_ip[0] = '\0';
    state.gateway_ip[0] = '\0';
    state.last_error[0] = '\0';
    snprintf(state.last_event, sizeof(state.last_event), "disconnected");
    if (product_config_set_bridge_wifi(&state) != 0) {
        send_json(fd, 500, "{\"error\":\"bridge wifi persist failed\"}");
        return;
    }
    bridge_control_apply_peers();
    send_json(fd, 200, "{\"status\":\"disconnected\"}");
}

static void handle_bridge_wifi_recent(int fd)
{
    field_bridge_wifi_state_t state;
    char *buf = peers_json_buf;
    const int cap = PEERS_JSON_BUF_SIZE;
    int pos = 0;
    int written_count = 0;

    if (product_config_get_bridge_wifi(&state) != 0) {
        send_json(fd, 500, "{\"error\":\"bridge wifi unavailable\"}");
        return;
    }

    buf[pos++] = '[';
    for (int i = 0; i < FIELD_BRIDGE_RECENT_WIFI_MAX; i++) {
        if (state.recent[i].ssid[0] == '\0') continue;
        if (written_count++ > 0) buf[pos++] = ',';
        if (append_bridge_wifi_entry(buf, cap, &pos, &state.recent[i]) != 0) {
            send_json(fd, 500, "{\"error\":\"bridge wifi recent too large\"}");
            return;
        }
    }
    buf[pos++] = ']';
    buf[pos] = '\0';
    send_json(fd, 200, buf);
}

static void handle_bridge_wifi_recent_delete(int fd, int idx)
{
    if (product_config_remove_recent_bridge_wifi(idx) != 0) {
        send_json(fd, 404, "{\"error\":\"recent bridge wifi index not found\"}");
        return;
    }
    send_json(fd, 200, "{\"status\":\"deleted\"}");
}

static void handle_bridge_wifi_join(int fd, const char *body)
{
    bridge_wifi_join_req_t req;
    field_bridge_wifi_state_t state;
    field_bridge_peer_t peer;

    if (json_decode_bridge_wifi_join(body, &req) != 0) {
        send_json(fd, 400, "{\"error\":\"invalid bridge wifi join JSON\"}");
        return;
    }
    if (product_config_get_bridge_wifi(&state) != 0) {
        send_json(fd, 500, "{\"error\":\"bridge wifi unavailable\"}");
        return;
    }
    if (!state.enabled) {
        state.connected = 0;
        state.last_error[0] = '\0';
        snprintf(state.last_event, sizeof(state.last_event),
                 "bridge wifi disabled");
        (void)product_config_set_bridge_wifi(&state);
        send_json(fd, 409, "{\"error\":\"bridge wifi disabled\"}");
        return;
    }

    state.current = req.entry;
    state.enabled = 1;
    state.connected = 1;
    resolve_bridge_wifi_mock_ips(&req.entry,
                                 state.local_sta_ip,
                                 sizeof(state.local_sta_ip),
                                 state.gateway_ip,
                                 sizeof(state.gateway_ip));
    state.last_error[0] = '\0';
    snprintf(state.last_event, sizeof(state.last_event),
             "joined peer index %d", req.peer_index);
    if (product_config_set_bridge_wifi(&state) != 0 ||
        product_config_add_recent_bridge_wifi(&req.entry) != 0) {
        send_json(fd, 500, "{\"error\":\"bridge wifi persist failed\"}");
        return;
    }

    memset(&peer, 0, sizeof(peer));
    strncpy(peer.name,
            req.entry.peer_name[0] ? req.entry.peer_name : req.entry.ssid,
            sizeof(peer.name) - 1);
    strncpy(peer.host, req.entry.host, sizeof(peer.host) - 1);
    peer.mqtt_port = req.entry.mqtt_port;
    peer.p2p_port = req.entry.p2p_port;
    peer.enabled = 1;
    if (product_config_set_peer(req.peer_index, &peer) != 0) {
        send_json(fd, 500, "{\"error\":\"peer persist failed\"}");
        return;
    }
    bridge_control_apply_peers();
    send_json(fd, 200, "{\"status\":\"joined\"}");
}

static void handle_post_peer(int fd, int idx, const char *body)
{
    if (idx < 0 || idx >= FIELD_BRIDGE_PEER_MAX) {
        send_json(fd, 404, "{\"error\":\"peer index out of range\"}");
        return;
    }
    field_bridge_peer_t peer;
    if (json_decode_peer(body, &peer) != 0) {
        send_json(fd, 400, "{\"error\":\"invalid JSON\"}");
        return;
    }
    if (product_config_set_peer(idx, &peer) != 0) {
        send_json(fd, 500, "{\"error\":\"persist failed\"}");
        return;
    }
    bridge_control_apply_peers();
    send_json(fd, 200, "{\"status\":\"ok\"}");
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
        send_json(fd, 400, "{\"error\":\"bad request\"}");
        goto done;
    }

    const char *hdr_end = strstr(buf, "\r\n\r\n");
    if (!hdr_end) { send_json(fd, 400, "{\"error\":\"bad request\"}"); goto done; }

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
    int authed = request_has_auth(buf);

    if (strcmp(method, "GET") == 0 &&
        (strcmp(path, "/") == 0 || strcmp(path, "/index.html") == 0)) {
        send_index_page(fd);
    } else if (strcmp(method, "GET") == 0 && strcmp(path, "/status") == 0) {
        handle_get_status(fd);
    } else if (strcmp(method, "POST") == 0 && strcmp(path, "/login") == 0) {
        handle_login(fd, body_start);
    } else if (strcmp(method, "GET") == 0 && strcmp(path, "/config") == 0) {
        if (!authed) send_json(fd, 403, "{\"error\":\"auth required\"}");
        else handle_get_config(fd);
    } else if (strcmp(method, "POST") == 0 && strcmp(path, "/config") == 0) {
        if (!authed) send_json(fd, 403, "{\"error\":\"auth required\"}");
        else handle_post_config(fd, body_start);
    } else if (strcmp(method, "POST") == 0 && strcmp(path, "/config/reset") == 0) {
        if (!authed) send_json(fd, 403, "{\"error\":\"auth required\"}");
        else handle_config_reset(fd);
    } else if (strcmp(method, "POST") == 0 && strcmp(path, "/broker/control") == 0) {
        if (!authed) send_json(fd, 403, "{\"error\":\"auth required\"}");
        else handle_broker_control(fd, body_start);
    } else if (strcmp(method, "POST") == 0 && strcmp(path, "/publish-test") == 0) {
        if (!authed) send_json(fd, 403, "{\"error\":\"auth required\"}");
        else handle_publish_test(fd, body_start);
    } else if (strcmp(method, "GET") == 0 && strcmp(path, "/peers") == 0) {
        if (!authed) send_json(fd, 403, "{\"error\":\"auth required\"}");
        else handle_get_peers(fd);
    } else if (strcmp(method, "GET") == 0 && strcmp(path, "/peer-status") == 0) {
        if (!authed) send_json(fd, 403, "{\"error\":\"auth required\"}");
        else handle_get_peer_status(fd);
    } else if (strcmp(method, "GET") == 0 && strcmp(path, "/wifi/scan") == 0) {
        if (!authed) send_json(fd, 403, "{\"error\":\"auth required\"}");
        else handle_wifi_scan(fd);
    } else if (strcmp(method, "GET") == 0 &&
               strcmp(path, "/bridge-wifi/current") == 0) {
        if (!authed) send_json(fd, 403, "{\"error\":\"auth required\"}");
        else handle_bridge_wifi_current(fd);
    } else if (strcmp(method, "GET") == 0 &&
               strcmp(path, "/bridge-wifi/recent") == 0) {
        if (!authed) send_json(fd, 403, "{\"error\":\"auth required\"}");
        else handle_bridge_wifi_recent(fd);
    } else if (strcmp(method, "DELETE") == 0 &&
               strncmp(path, "/bridge-wifi/recent/", 20) == 0) {
        if (!authed) send_json(fd, 403, "{\"error\":\"auth required\"}");
        else {
            int idx = atoi(path + 20);
            handle_bridge_wifi_recent_delete(fd, idx);
        }
    } else if (strcmp(method, "POST") == 0 &&
               strcmp(path, "/bridge-wifi/join") == 0) {
        if (!authed) send_json(fd, 403, "{\"error\":\"auth required\"}");
        else handle_bridge_wifi_join(fd, body_start);
    } else if (strcmp(method, "POST") == 0 &&
               strcmp(path, "/bridge-wifi/enabled") == 0) {
        if (!authed) send_json(fd, 403, "{\"error\":\"auth required\"}");
        else handle_bridge_wifi_enabled(fd, body_start);
    } else if (strcmp(method, "POST") == 0 &&
               strcmp(path, "/bridge-wifi/disconnect") == 0) {
        if (!authed) send_json(fd, 403, "{\"error\":\"auth required\"}");
        else handle_bridge_wifi_disconnect(fd);
    } else if (strcmp(method, "POST") == 0 && strncmp(path, "/peers/", 7) == 0) {
        if (!authed) send_json(fd, 403, "{\"error\":\"auth required\"}");
        else {
            int idx = atoi(path + 7);
            handle_post_peer(fd, idx, body_start);
        }
    } else {
        send_json(fd, 404, "{\"error\":\"not found\"}");
    }

done:
    PLAT_CLOSE(fd);
}

static void configure_client_socket(int fd)
{
#ifndef __ZEPHYR__
    struct timeval tv = { .tv_sec = 10, .tv_usec = 0 };
    (void)setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    (void)setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
#else
    struct timeval tv = { .tv_sec = 10, .tv_usec = 0 };
    (void)zsock_setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    (void)zsock_setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
#endif
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
        configure_client_socket(client_fd);
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

    g_server_fd = zsock_socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (g_server_fd < 0) {
        LOG_ERR("provisioning HTTP socket failed: %d", errno);
        return;
    }
    zsock_setsockopt(g_server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons(PROVISIONING_HTTP_PORT);
    addr.sin_addr.s_addr = INADDR_ANY;

    if (zsock_bind(g_server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0 ||
        zsock_listen(g_server_fd, 4) < 0) {
        LOG_ERR("provisioning HTTP bind/listen failed: %d", errno);
        zsock_close(g_server_fd);
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
