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
#include <zephyr/net/socket.h>
#define PLAT_SEND(fd, buf, len) zsock_send((fd), (buf), (size_t)(len), 0)
#define PLAT_RECV(fd, buf, len) zsock_recv((fd), (buf), (size_t)(len), 0)
#define PLAT_CLOSE(fd)          zsock_close(fd)
#endif

#define PEERS_JSON_BUF_SIZE 4096
static char peers_json_buf[PEERS_JSON_BUF_SIZE];

/* ── Response / route handlers ───────────────────────────────────────────── */

static int send_all_bytes(int fd, const char *buf, int len)
{
    int sent = 0;

    while (sent < len) {
        ssize_t n = PLAT_SEND(fd, buf + sent, len - sent);
        if (n <= 0) {
            return -1;
        }
        sent += (int)n;
    }
    return 0;
}

static void send_response_type(int fd, int status, const char *content_type,
                               const char *body)
{
    const char *reason = (status == 200) ? "OK" :
                         (status == 400) ? "Bad Request" :
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

static const char index_html[] =
"<!doctype html><html lang=\"en\"><head><meta charset=\"utf-8\">"
"<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
"<title>Field Bridge Settings</title>"
"<style>"
":root{color-scheme:light dark;--bg:#f6f7f9;--fg:#18202a;--muted:#667085;--line:#d7dce3;--panel:#fff;--accent:#0b6bcb;--ok:#097a43;--bad:#b42318}"
"@media(prefers-color-scheme:dark){:root{--bg:#111418;--fg:#eef2f6;--muted:#a6b0bd;--line:#303946;--panel:#181d24;--accent:#5aa7ff}}"
"*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--fg);font:14px/1.45 system-ui,-apple-system,Segoe UI,sans-serif}"
"header{padding:18px 20px;border-bottom:1px solid var(--line);background:var(--panel)}"
"main{max-width:980px;margin:0 auto;padding:20px}h1{margin:0;font-size:22px}h2{font-size:16px;margin:0 0 12px}.muted{color:var(--muted)}"
".bar{display:flex;gap:10px;align-items:center;justify-content:space-between;flex-wrap:wrap}.grid{display:grid;grid-template-columns:repeat(3,minmax(0,1fr));gap:12px}"
".panel{background:var(--panel);border:1px solid var(--line);border-radius:8px;padding:14px;margin-bottom:14px}"
".peer{display:grid;grid-template-columns:1fr 1.3fr .8fr .8fr auto;gap:8px;align-items:end;border-top:1px solid var(--line);padding-top:12px;margin-top:12px}"
"label{display:grid;gap:4px;font-size:12px;color:var(--muted)}input{width:100%;padding:8px;border:1px solid var(--line);border-radius:6px;background:transparent;color:var(--fg)}"
"input[type=checkbox]{width:20px;height:20px}.actions{display:flex;gap:8px;align-items:center}button{border:1px solid var(--accent);background:var(--accent);color:white;border-radius:6px;padding:9px 12px;cursor:pointer}"
"button.secondary{background:transparent;color:var(--accent)}button:disabled{opacity:.55;cursor:wait}.pill{display:inline-block;border:1px solid var(--line);border-radius:999px;padding:4px 9px}"
".ok{color:var(--ok)}.bad{color:var(--bad)}pre{white-space:pre-wrap;margin:0}.small{font-size:12px}@media(max-width:760px){.grid,.peer{grid-template-columns:1fr}.actions{justify-content:flex-start}}"
"</style></head><body><header><div class=\"bar\"><div><h1>Field Bridge Settings</h1><div class=\"muted\">Local broker peer configuration</div></div><button id=\"refresh\" class=\"secondary\">Refresh</button></div></header>"
"<main><section class=\"grid\"><div class=\"panel\"><h2>Status</h2><div id=\"status\" class=\"pill muted\">Loading</div></div><div class=\"panel\"><h2>Peers</h2><div id=\"peer-count\" class=\"pill muted\">-</div></div><div class=\"panel\"><h2>Last Save</h2><div id=\"save-state\" class=\"pill muted\">No changes</div></div></section>"
"<section class=\"panel\"><div class=\"bar\"><h2>Peer Slots</h2><div class=\"actions\"><button type=\"button\" id=\"save-all\">Save All</button><button type=\"button\" id=\"disable-all\" class=\"secondary\">Disable All</button></div></div><form id=\"peer-form\"></form></section>"
"<section class=\"panel\"><h2>Raw Status</h2><pre id=\"raw\" class=\"small muted\"></pre></section></main>"
"<script>"
"const form=document.getElementById('peer-form'),raw=document.getElementById('raw'),st=document.getElementById('status'),pc=document.getElementById('peer-count'),ss=document.getElementById('save-state');"
"function esc(s){return String(s||'').replace(/[&<>\"]/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','\"':'&quot;'}[c]))}"
"async function json(url,opt){const r=await fetch(url,opt);if(!r.ok)throw new Error(await r.text());return r.json()}"
"function row(p,i){return `<div class=\"peer\" data-i=\"${i}\"><label>Name<input name=\"name\" maxlength=\"31\" value=\"${esc(p.name)}\"></label><label>Host / IP<input name=\"host\" maxlength=\"63\" value=\"${esc(p.host)}\"></label><label>MQTT Port<input name=\"mqtt_port\" type=\"number\" min=\"1\" max=\"65535\" value=\"${p.mqtt_port||1883}\"></label><label>P2P Port<input name=\"p2p_port\" type=\"number\" min=\"1\" max=\"65535\" value=\"${p.p2p_port||4884}\"></label><label>Enabled<input name=\"enabled\" type=\"checkbox\" ${p.enabled?'checked':''}></label><div class=\"actions\"><button type=\"button\" onclick=\"savePeer(${i})\">Save</button><button class=\"secondary\" type=\"button\" onclick=\"disablePeer(${i})\">Disable</button></div></div>`}"
"async function load(){st.textContent='Loading';st.className='pill muted';const s=await json('/status'),p=await json('/peers');raw.textContent=JSON.stringify({status:s,peers:p},null,2);pc.textContent=`${p.length} slots`;form.innerHTML=p.map(row).join('');st.textContent='Online';st.className='pill ok'}"
"function data(i){const e=form.querySelector(`[data-i=\"${i}\"]`);return {name:e.querySelector('[name=name]').value,host:e.querySelector('[name=host]').value,mqtt_port:+e.querySelector('[name=mqtt_port]').value,p2p_port:+e.querySelector('[name=p2p_port]').value,enabled:e.querySelector('[name=enabled]').checked?1:0}}"
"async function savePeer(i){ss.textContent='Saving';ss.className='pill muted';await json(`/peers/${i}`,{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(data(i))});ss.textContent=`Slot ${i} saved`;ss.className='pill ok';await load()}"
"async function disablePeer(i){const p=data(i);p.enabled=0;await json(`/peers/${i}`,{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(p)});ss.textContent=`Slot ${i} disabled`;ss.className='pill ok';await load()}"
"async function saveAll(){ss.textContent='Saving all';ss.className='pill muted';const rows=[...form.querySelectorAll('[data-i]')];for(const r of rows){const i=+r.dataset.i;await json(`/peers/${i}`,{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(data(i))})}ss.textContent=`${rows.length} slots saved`;ss.className='pill ok';await load()}"
"async function disableAll(){for(const e of form.querySelectorAll('[name=enabled]'))e.checked=false;await saveAll()}"
"document.getElementById('refresh').onclick=load;document.getElementById('save-all').onclick=saveAll;document.getElementById('disable-all').onclick=disableAll;load().catch(e=>{st.textContent='Offline';st.className='pill bad';raw.textContent=e.message});"
"</script></body></html>";

static void handle_get_status(int fd)
{
    char buf[128];
    int n = snprintf(buf, sizeof(buf),
                     "{\"status\":\"ok\",\"peers\":%d}",
                     product_config_peer_count());
    send_json(fd, 200, buf);
    (void)n;
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

    if (strcmp(method, "GET") == 0 &&
        (strcmp(path, "/") == 0 || strcmp(path, "/index.html") == 0)) {
        send_html(fd, 200, index_html);
    } else if (strcmp(method, "GET") == 0 && strcmp(path, "/status") == 0) {
        handle_get_status(fd);
    } else if (strcmp(method, "GET") == 0 && strcmp(path, "/peers") == 0) {
        handle_get_peers(fd);
    } else if (strcmp(method, "POST") == 0 && strncmp(path, "/peers/", 7) == 0) {
        int idx = atoi(path + 7);
        handle_post_peer(fd, idx, body_start);
    } else {
        send_json(fd, 404, "{\"error\":\"not found\"}");
    }

done:
    PLAT_CLOSE(fd);
}

static void configure_client_socket(int fd)
{
#ifndef __ZEPHYR__
    struct timeval tv = { .tv_sec = 3, .tv_usec = 0 };
    (void)setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
#else
    struct timeval tv = { .tv_sec = 3, .tv_usec = 0 };
    (void)setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
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
