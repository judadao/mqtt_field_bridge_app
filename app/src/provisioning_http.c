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
#define CONFIG_JSON_BUF_SIZE 2048
static char config_json_buf[CONFIG_JSON_BUF_SIZE];
static char session_token[32];
static unsigned session_seq;

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
                         (status == 403) ? "Forbidden" :
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

static const char index_html[] =
"<!doctype html><html lang=\"en\"><head><meta charset=\"utf-8\">"
"<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
"<title>Field Bridge Settings</title>"
"<style>"
":root{color-scheme:light dark;--bg:#f6f7f9;--fg:#18202a;--muted:#667085;--line:#d7dce3;--panel:#fff;--accent:#0b6bcb;--ok:#097a43;--bad:#b42318;--tab:#eef4fb}"
"@media(prefers-color-scheme:dark){:root{--bg:#111418;--fg:#eef2f6;--muted:#a6b0bd;--line:#303946;--panel:#181d24;--accent:#5aa7ff;--tab:#202936}}"
"*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--fg);font:14px/1.45 system-ui,-apple-system,Segoe UI,sans-serif}"
"header{padding:18px 20px;border-bottom:1px solid var(--line);background:var(--panel)}"
".hide{display:none!important}main{max-width:980px;margin:0 auto;padding:20px}h1{margin:0;font-size:22px}h2{font-size:16px;margin:0 0 12px}.muted{color:var(--muted)}"
".bar{display:flex;gap:10px;align-items:center;justify-content:space-between;flex-wrap:wrap}.grid{display:grid;grid-template-columns:repeat(3,minmax(0,1fr));gap:12px}"
".panel{background:var(--panel);border:1px solid var(--line);border-radius:8px;padding:14px;margin-bottom:14px}"
".peer{display:grid;grid-template-columns:1fr 1.3fr .8fr .8fr auto;gap:8px;align-items:end;border-top:1px solid var(--line);padding-top:12px;margin-top:12px}"
"label{display:grid;gap:4px;font-size:12px;color:var(--muted)}input{width:100%;padding:8px;border:1px solid var(--line);border-radius:6px;background:transparent;color:var(--fg)}"
"input[type=checkbox]{width:20px;height:20px}.actions{display:flex;gap:8px;align-items:center}button{border:1px solid var(--accent);background:var(--accent);color:white;border-radius:6px;padding:9px 12px;cursor:pointer}"
".tabs{display:flex;gap:6px;margin:0 0 14px;flex-wrap:wrap}.tab{background:var(--tab);color:var(--fg);border-color:var(--line)}.tab.active{background:var(--accent);color:white;border-color:var(--accent)}"
"button.secondary{background:transparent;color:var(--accent)}button:disabled{opacity:.55;cursor:wait}.pill{display:inline-block;border:1px solid var(--line);border-radius:999px;padding:4px 9px}"
".ok{color:var(--ok)}.bad{color:var(--bad)}pre{white-space:pre-wrap;margin:0}.small{font-size:12px}@media(max-width:760px){.grid,.peer{grid-template-columns:1fr}.actions{justify-content:flex-start}}"
"</style></head><body><header><div class=\"bar\"><div><h1>Field Bridge Settings</h1><div class=\"muted\">ESP32 min broker local setup</div></div><button id=\"refresh\" class=\"secondary hide\">Refresh</button></div></header>"
"<main><section id=\"login\" class=\"panel\"><h2>Login</h2><form id=\"login-form\" class=\"grid\"><label>Admin Password<input id=\"login-password\" type=\"password\" value=\"admin\"></label><div class=\"actions\"><button type=\"submit\">Login</button><span id=\"login-state\" class=\"muted\">Default password: admin</span></div></form></section>"
"<section id=\"app\" class=\"hide\"><section class=\"grid\"><div class=\"panel\"><h2>Status</h2><div id=\"status\" class=\"pill muted\">Loading</div></div><div class=\"panel\"><h2>Device IP</h2><div id=\"ip-state\" class=\"pill muted\">-</div></div><div class=\"panel\"><h2>Last Save</h2><div id=\"save-state\" class=\"pill muted\">No changes</div></div></section>"
"<section class=\"grid\"><div class=\"panel\"><h2>WiFi</h2><div id=\"wifi-state\" class=\"pill muted\">-</div></div><div class=\"panel\"><h2>Broker</h2><div id=\"broker-state\" class=\"pill muted\">-</div></div><div class=\"panel\"><h2>P2P</h2><div id=\"p2p-state\" class=\"pill muted\">-</div></div></section>"
"<nav class=\"tabs\"><button class=\"tab active\" data-tab=\"system\">System Setting</button><button class=\"tab\" data-tab=\"network\">Network Setting</button><button class=\"tab\" data-tab=\"broker\">Broker Setting</button></nav>"
"<section id=\"system\" class=\"panel view\"><h2>System Setting</h2><div class=\"grid\"><label>Device Name<input id=\"device_name\" maxlength=\"31\"></label><label>Admin Password<input id=\"admin_password\" type=\"password\" maxlength=\"63\"></label></div><div class=\"actions\"><button type=\"button\" id=\"save-system\">Save System</button><button type=\"button\" id=\"reset-config\" class=\"secondary\">Reset Config</button></div></section>"
"<section id=\"network\" class=\"panel view hide\"><h2>Network Setting</h2><div class=\"grid\"><label>WiFi SSID<input id=\"wifi_ssid\" maxlength=\"63\"></label><label>WiFi Password<input id=\"wifi_password\" type=\"password\" maxlength=\"63\"></label><label>DHCP Enabled<input id=\"dhcp_enabled\" type=\"checkbox\"></label><label>AP SSID<input id=\"ap_ssid\" maxlength=\"63\"></label><label>AP Password<input id=\"ap_password\" type=\"password\" maxlength=\"63\"></label><label>Device Default IP<input id=\"device_ip\" maxlength=\"63\"></label><label>Gateway<input id=\"gateway\" maxlength=\"63\"></label><label>Netmask<input id=\"netmask\" maxlength=\"63\"></label></div><div class=\"actions\"><button type=\"button\" id=\"save-network\">Save Network</button></div></section>"
"<section id=\"broker\" class=\"panel view hide\"><div class=\"bar\"><h2>Broker Setting / Bridge Mesh Setting</h2><div class=\"actions\"><button type=\"button\" id=\"save-broker\">Save Broker</button><button type=\"button\" id=\"broker-start\" class=\"secondary\">Start Broker</button><button type=\"button\" id=\"broker-stop\" class=\"secondary\">Stop Broker</button><button type=\"button\" id=\"disable-all\" class=\"secondary\">Disable All Peers</button></div></div><div class=\"grid\"><label>Site ID<input id=\"site_id\" maxlength=\"31\"></label><label>Topic Prefix<input id=\"topic_prefix\" maxlength=\"63\"></label><label>MQTT Port<input id=\"cfg_mqtt_port\" type=\"number\" min=\"1\" max=\"65535\"></label><label>P2P Port<input id=\"cfg_p2p_port\" type=\"number\" min=\"1\" max=\"65535\"></label><label>Broker Enabled<input id=\"broker_enabled\" type=\"checkbox\"></label><label>Bridge Enabled<input id=\"bridge_enabled\" type=\"checkbox\"></label><label>Mesh Enabled<input id=\"mesh_enabled\" type=\"checkbox\"></label></div><form id=\"peer-form\"></form><div class=\"actions\"><button type=\"button\" id=\"save-all\">Save All Peers</button></div></section></section>"
"<section id=\"topic-test\" class=\"panel\"><h2>Topic Test</h2><div class=\"grid\"><label>Topic<input id=\"test_topic\" maxlength=\"127\"></label><label>Payload<input id=\"test_payload\" maxlength=\"255\" value=\"hello\"></label><label>QoS<input id=\"test_qos\" type=\"number\" min=\"0\" max=\"1\" value=\"0\"></label><label>Retain<input id=\"test_retain\" type=\"checkbox\"></label></div><div class=\"actions\"><button type=\"button\" id=\"publish-test\">Publish Test</button><span id=\"publish-state\" class=\"muted\">No test publish</span></div></section></section>"
"<section class=\"panel\"><h2>Raw Status</h2><pre id=\"raw\" class=\"small muted\"></pre></section></main>"
"<script>"
"const $=id=>document.getElementById(id),form=$('peer-form'),raw=$('raw'),st=$('status'),ss=$('save-state');let baseline=[],cfg={},token='';"
"function esc(s){return String(s||'').replace(/[&<>\"]/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','\"':'&quot;'}[c]))}"
"async function json(url,opt={}){opt.headers=opt.headers||{};if(token)opt.headers['X-Auth-Token']=token;const r=await fetch(url,opt);if(!r.ok)throw new Error(await r.text());return r.json()}"
"function norm(p){return {name:p.name||'',host:p.host||'',mqtt_port:+(p.mqtt_port||1883),p2p_port:+(p.p2p_port||4884),enabled:p.enabled?1:0}}"
"function row(p,i){p=norm(p);return `<div class=\"peer\" data-i=\"${i}\"><label>Name<input name=\"name\" maxlength=\"31\" value=\"${esc(p.name)}\"></label><label>Host / IP<input name=\"host\" maxlength=\"63\" value=\"${esc(p.host)}\"></label><label>MQTT Port<input name=\"mqtt_port\" type=\"number\" min=\"1\" max=\"65535\" value=\"${p.mqtt_port}\"></label><label>P2P Port<input name=\"p2p_port\" type=\"number\" min=\"1\" max=\"65535\" value=\"${p.p2p_port}\"></label><label>Enabled<input name=\"enabled\" type=\"checkbox\" ${p.enabled?'checked':''}></label><div class=\"actions\"><button type=\"button\" onclick=\"savePeer(${i})\">Save</button><button class=\"secondary\" type=\"button\" onclick=\"disablePeer(${i})\">Disable</button></div></div>`}"
"function put(c){cfg=c;for(const k in c){const e=$(k);if(!e)continue;if(e.type==='checkbox')e.checked=!!c[k];else e.value=c[k]||''}$('ip-state').textContent=c.device_ip||'-'}"
"function collect(){return {device_name:$('device_name').value,admin_password:$('admin_password').value,wifi_ssid:$('wifi_ssid').value,wifi_password:$('wifi_password').value,ap_ssid:$('ap_ssid').value,ap_password:$('ap_password').value,device_ip:$('device_ip').value,gateway:$('gateway').value,netmask:$('netmask').value,dhcp_enabled:$('dhcp_enabled').checked?1:0,site_id:$('site_id').value,topic_prefix:$('topic_prefix').value,mqtt_port:+$('cfg_mqtt_port').value,p2p_port:+$('cfg_p2p_port').value,broker_enabled:$('broker_enabled').checked?1:0,bridge_enabled:$('bridge_enabled').checked?1:0,mesh_enabled:$('mesh_enabled').checked?1:0}}"
"async function load(){st.textContent='Loading';st.className='pill muted';const s=await json('/status'),c=await json('/config'),p=await json('/peers'),ps=await json('/peer-status');put(c);baseline=p.map(x=>JSON.stringify(norm(x)));raw.textContent=JSON.stringify({status:s,config:c,peers:p,peer_status:ps},null,2);form.innerHTML=p.map(row).join('');$('wifi-state').textContent=s.wifi_state||'-';$('broker-state').textContent=s.broker_state||'-';$('p2p-state').textContent=`${s.p2p_role||'-'} / peers ${s.connected_peers||0}`;if(!$('test_topic').value)$('test_topic').value=s.test_topic||((c.topic_prefix||'site/field-a')+'/test');st.textContent='Online';st.className='pill ok'}"
"function data(i){const e=form.querySelector(`[data-i=\"${i}\"]`);return norm({name:e.querySelector('[name=name]').value,host:e.querySelector('[name=host]').value,mqtt_port:e.querySelector('[name=mqtt_port]').value,p2p_port:e.querySelector('[name=p2p_port]').value,enabled:e.querySelector('[name=enabled]').checked?1:0})}"
"async function saveConfig(part){ss.textContent='Saving';ss.className='pill muted';await json('/config',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(collect())});ss.textContent=`${part} saved`;ss.className='pill ok';await load()}"
"async function resetConfig(){ss.textContent='Resetting';ss.className='pill muted';await json('/config/reset',{method:'POST'});token='';$('login').classList.remove('hide');$('app').classList.add('hide');$('refresh').classList.add('hide');$('login-password').value='admin';$('login-state').textContent='Config reset; login again';$('login-state').className='muted'}"
"async function brokerControl(enabled){await json('/broker/control',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({enabled})});await load()}"
"async function publishTest(){const body={topic:$('test_topic').value,payload:$('test_payload').value,qos:+$('test_qos').value,retain:$('test_retain').checked?1:0};await json('/publish-test',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(body)});$('publish-state').textContent='Recorded'}"
"async function savePeer(i){ss.textContent='Saving';ss.className='pill muted';await json(`/peers/${i}`,{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(data(i))});ss.textContent=`Slot ${i} saved`;ss.className='pill ok';await load()}"
"async function disablePeer(i){const p=data(i);p.enabled=0;await json(`/peers/${i}`,{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(p)});ss.textContent=`Slot ${i} disabled`;ss.className='pill ok';await load()}"
"async function saveAll(){ss.textContent='Saving changes';ss.className='pill muted';const rows=[...form.querySelectorAll('[data-i]')];let saved=0;for(const r of rows){const i=+r.dataset.i,p=data(i),next=JSON.stringify(p);if(next===baseline[i])continue;await json(`/peers/${i}`,{method:'POST',headers:{'Content-Type':'application/json'},body:next});saved++}ss.textContent=saved?`${saved} changed slots saved`:'No slot changes';ss.className='pill ok';if(saved)await load()}"
"async function disableAll(){for(const e of form.querySelectorAll('[name=enabled]'))e.checked=false;await saveAll()}"
"$('login-form').onsubmit=async e=>{e.preventDefault();try{const r=await json('/login',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({password:$('login-password').value})});token=r.token;$('login').classList.add('hide');$('app').classList.remove('hide');$('refresh').classList.remove('hide');await load()}catch(x){$('login-state').textContent='Login failed';$('login-state').className='bad'}};"
"for(const b of document.querySelectorAll('.tab'))b.onclick=()=>{for(const x of document.querySelectorAll('.tab'))x.classList.remove('active');for(const v of document.querySelectorAll('.view'))v.classList.add('hide');b.classList.add('active');$(b.dataset.tab).classList.remove('hide')};"
"$('refresh').onclick=load;$('save-system').onclick=()=>saveConfig('System');$('reset-config').onclick=resetConfig;$('save-network').onclick=()=>saveConfig('Network');$('save-broker').onclick=()=>saveConfig('Broker');$('broker-start').onclick=()=>brokerControl(1);$('broker-stop').onclick=()=>brokerControl(0);$('save-all').onclick=saveAll;$('disable-all').onclick=disableAll;$('publish-test').onclick=publishTest;"
"</script></body></html>";

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
        send_html(fd, 200, index_html);
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
    } else if (strcmp(method, "POST") == 0 && strncmp(path, "/peers/", 7) == 0) {
        int idx = atoi(path + 7);
        if (!authed) send_json(fd, 403, "{\"error\":\"auth required\"}");
        else handle_post_peer(fd, idx, body_start);
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
