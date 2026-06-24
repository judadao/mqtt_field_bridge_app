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
#ifndef __ZEPHYR__
#include "generated/provisioning_index.h"
#endif

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

static void normalize_request_path(char *path, int cap)
{
    const char *absolute;
    const char *slash;

    if (!path || cap <= 0) {
        return;
    }

    absolute = NULL;
    if (strncmp(path, "http://", 7) == 0) {
        absolute = path + 7;
    } else if (strncmp(path, "https://", 8) == 0) {
        absolute = path + 8;
    }

    if (absolute) {
        slash = strchr(absolute, '/');
        if (slash) {
            memmove(path, slash, strlen(slash) + 1);
        } else {
            snprintf(path, (size_t)cap, "%s", "/");
        }
    }

    char *query = strchr(path, '?');
    if (query) {
        *query = '\0';
    }
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
#ifdef __ZEPHYR__
#define HTTP_SEND_CHUNK_SIZE 256
#else
#define HTTP_SEND_CHUNK_SIZE 512
#endif
#define HTTP_COMBINED_RESPONSE_SIZE 2048
static char combined_response_buf[HTTP_COMBINED_RESPONSE_SIZE];

#ifdef __ZEPHYR__
static const char index_lite_html[] =
"<!doctype html><html><head><meta name=viewport content=\"width=device-width,initial-scale=1\">"
"<title>Field Bridge Settings</title><style>"
":root{--b:#eef2f5;--f:#17212b;--m:#60717d;--p:#fff;--l:#cfd9e2;--a:#1565c0;--btn:#1565c0;--btnh:#0d47a1;--d:#b3261e}"
"@media(prefers-color-scheme:dark){:root{--b:#101417;--f:#eef3f7;--m:#a7b3bb;--p:#171c20;--l:#33424a;--a:#90caf9;--btn:#1565c0;--btnh:#0d47a1;--d:#ffb4ab}}"
"*{box-sizing:border-box}body{margin:0;background:var(--b);color:var(--f);font:14px/1.45 system-ui,Segoe UI,sans-serif}"
"header{background:var(--p);border-bottom:1px solid var(--l);padding:14px 16px}main{max-width:760px;margin:auto;padding:14px}"
"h1{margin:0;font-size:22px}.sub{color:var(--m);font-size:12px}.card{background:var(--p);border:1px solid var(--l);border-radius:8px;padding:12px;margin:0 0 12px}"
"h2{font-size:13px;text-transform:uppercase;color:var(--m);margin:0 0 10px}table{width:100%;border-collapse:collapse}th,td{text-align:left;border-bottom:1px solid var(--l);padding:7px}th{width:38%;color:var(--m);font-size:12px}"
"tr:last-child th,tr:last-child td{border:0}.row{display:flex;gap:8px;flex-wrap:wrap;align-items:center}.grid{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:10px}"
"label{display:grid;gap:4px;color:var(--m);font-size:12px;font-weight:700}input,select{width:100%;border:1px solid var(--l);border-radius:6px;background:var(--b);color:var(--f);padding:8px;min-height:36px}"
"input[type=checkbox]{width:22px;min-height:22px;accent-color:var(--a)}button{border:1px solid var(--btn);background:var(--btn);color:white;border-radius:6px;padding:8px 12px;font-weight:700}button:hover{background:var(--btnh);border-color:var(--btnh)}button.alt{background:transparent;color:var(--a);border-color:var(--a)}button.alt:hover{background:transparent;color:var(--a);border-color:var(--a)}"
".pill{display:inline-block;border:1px solid var(--l);border-radius:99px;padding:4px 9px;color:var(--m)}.ok{color:#2e7d32}.err{color:var(--d)}"
"@media(max-width:560px){main{padding:10px}.grid{grid-template-columns:1fr}.row button{width:100%}}</style></head>"
"<body><header><h1>Field Bridge Settings</h1><div class=sub>Ethernet broker bridge</div></header><main>"
"<section class=card><div class=row><button class=alt onclick=S()>Refresh</button><span id=s class=pill>booting</span></div></section>"
"<section class=card><table><tbody id=t><tr><th>Status</th><td>Loading</td></tr></tbody></table></section>"
"<section class=card><h2>Network</h2><div class=grid><label>DHCP Enabled<input id=dhcp_enabled type=checkbox></label><label>Ethernet IP<input id=device_ip></label><label>Gateway<input id=gateway></label><label>Netmask<input id=netmask></label><label>DNS<input id=dns></label></div><p><button onclick=saveConfig()>Save Network</button></p></section>"
"<section class=card><h2>Broker</h2><div class=grid><label>Broker Enabled<input id=broker_enabled type=checkbox></label><label>Bridge Enabled<input id=bridge_enabled type=checkbox></label><label>Site ID<input id=site_id></label><label>Topic Prefix<input id=topic_prefix></label><label>MQTT Port<input id=mqtt_port type=number></label><label>P2P Port<input id=p2p_port type=number></label></div><p><button onclick=saveConfig()>Save Broker</button></p></section>"
"<section class=card><h2>Bridge Peer</h2><div class=grid><label>Broker Slot<select id=peer_slot onchange=loadPeer()></select></label><label>Enabled<input id=peer_enabled type=checkbox></label><label>Name<input id=peer_name></label><label>Host / IP<input id=peer_host></label><label>MQTT Port<input id=peer_mqtt type=number></label><label>P2P Port<input id=peer_p2p type=number></label></div><p class=row><button onclick=savePeer()>Save Peer</button><button class=alt onclick=loadPeers()>Reload Peers</button></p></section>"
"<script>let E=id=>document.getElementById(id),cfg={},peers=[];"
"async function J(u,m,b){let o={method:m||'GET',headers:{}};if(b){o.headers['Content-Type']='application/json';o.body=JSON.stringify(b)}let r=await fetch(u,o),x=await r.text();if(!r.ok)throw x;return x?JSON.parse(x):{}}"
"function O(k){E('s').textContent=k||'OK';E('s').className='pill '+(k=='ERR'?'err':'ok')}"
"function P(p){p.then(()=>O('OK')).catch(()=>O('ERR'))}"
"function T(r){E('t').innerHTML=['network_state','ip_addr','broker_state','p2p_role','connected_peers','remote_subscriptions','last_error'].map(k=>'<tr><th>'+k+'</th><td>'+(r[k]||'-')+'</td></tr>').join('')}"
"function put(c){cfg=c;['device_ip','gateway','netmask','dns','site_id','topic_prefix'].forEach(k=>E(k).value=c[k]||'');['dhcp_enabled','broker_enabled','bridge_enabled'].forEach(k=>E(k).checked=!!c[k]);E('mqtt_port').value=c.mqtt_port||1883;E('p2p_port').value=c.p2p_port||4884}"
"function getCfg(){return{device_name:cfg.device_name||'esp32-min-broker',device_ip:E('device_ip').value,gateway:E('gateway').value,netmask:E('netmask').value,dns:E('dns').value,dhcp_enabled:E('dhcp_enabled').checked?1:0,site_id:E('site_id').value,topic_prefix:E('topic_prefix').value,mqtt_port:+E('mqtt_port').value,p2p_port:+E('p2p_port').value,broker_enabled:E('broker_enabled').checked?1:0,bridge_enabled:E('bridge_enabled').checked?1:0,mesh_enabled:E('bridge_enabled').checked?1:0}}"
"function S(){P(J('/status').then(r=>(T(r),r)))}"
"function C(){return J('/config').then(c=>(put(c),c))}"
"function saveConfig(){P(J('/config','POST',getCfg()).then(x=>C().then(()=>x)))}"
"function norm(p){return{name:p&&p.name||'',host:p&&p.host||'',mqtt_port:p&&p.mqtt_port||1883,p2p_port:p&&p.p2p_port||4884,enabled:p&&p.enabled?1:0}}"
"function drawSlots(){let s=E('peer_slot');s.innerHTML=peers.map((p,i)=>'<option value='+i+'>Broker '+i+(p.enabled?' - '+(p.name||p.host):' - empty')+'</option>').join('');loadPeer()}"
"function loadPeer(){let p=norm(peers[+E('peer_slot').value]);E('peer_name').value=p.name;E('peer_host').value=p.host;E('peer_mqtt').value=p.mqtt_port;E('peer_p2p').value=p.p2p_port;E('peer_enabled').checked=!!p.enabled}"
"function loadPeers(){P(J('/peers').then(p=>(peers=p.map(norm),drawSlots(),p)))}"
"function savePeer(){let i=+E('peer_slot').value,p={name:E('peer_name').value,host:E('peer_host').value,mqtt_port:+E('peer_mqtt').value,p2p_port:+E('peer_p2p').value,enabled:E('peer_enabled').checked?1:0};P(J('/peers/'+i,'POST',p).then(x=>J('/peers').then(p=>(peers=p.map(norm),drawSlots(),E('peer_slot').value=i,loadPeer(),x))))}"
"S();C().then(loadPeers).catch(e=>O(e,'ERR'))</script></main></body></html>";

static const unsigned char index_lite_gz[] =
"\x1f\x8b\x08\x00\x00\x00\x00\x00\x02\x03\x95\x57\x6d\x73\x9b\x46\x10\xfe\x2b\x24\x9e\x0e\xd0\x80"
"\x24\xb0\x63\x25\x48\xa8\x6d\x32\xee\xab\x5b\x67\x6c\xb7\xe9\x4c\x26\xe3\x39\xb8\x45\x5c\x04\x77"
"\xcc\x71\x58\xa8\x84\xff\xde\x3d\x40\x32\xb6\x93\x34\xfd\x82\xee\x75\x9f\x67\x9f\xdb\xdd\x3b\x2d"
"\x9f\x50\x11\xab\x5d\x01\x46\xaa\xf2\x6c\xb5\x1c\xbe\x40\xe8\x6a\x99\x83\x22\x06\x27\x39\x84\xb7"
"\x0c\xb6\x85\x90\xca\x88\x05\x57\xc0\x55\xf8\x74\xcb\xa8\x4a\x43\x0a\xb7\x2c\x06\xb7\xeb\x38\x8c"
"\x33\xc5\x48\xe6\x96\x31\xc9\x20\xf4\x9e\xae\x96\x8a\xa9\x0c\x56\x3f\x32\xc8\xa8\xf1\x4a\x32\xba"
"\x06\xe3\x0a\x94\x62\x7c\x5d\x2e\xa7\xfd\xe4\xb2\x54\x3b\xfc\x09\xa4\x10\xaa\x71\xdd\x28\x38\x02"
"\x48\xfc\xe4\xf9\xc2\x75\x93\xe0\xc8\x9b\xfb\x9e\x1f\x61\x3b\x0f\x8e\x4e\x67\x73\x6f\x4e\xb1\x5d"
"\x04\x47\x49\x92\x60\x23\x0b\x8e\xe2\x84\xbe\x04\x1f\xdb\x24\x38\x9a\xcd\x4e\xc9\x29\xc1\x36\x0d"
"\x8e\xa2\x63\xff\xd4\x83\xf6\xfb\x1c\x28\x23\x56\x21\x21\x01\x59\xba\xb1\xc8\x84\x44\x7e\x29\xe4"
"\x10\x50\x22\x37\x76\x33\x02\xf6\x66\xde\x89\x37\xef\x81\x91\xc4\x71\x32\xef\x81\xc9\x3c\x3a\x8e"
"\xa2\x1e\xd8\x9b\x7b\xb1\x3f\xeb\xb1\x8f\x8f\x4f\xfc\x13\xd2\x63\xbf\x98\xc5\x51\x7c\xd2\x63\x27"
"\x49\x74\x42\xa2\xb6\xfd\xb6\x89\x44\xed\x96\xec\x1f\xf4\x37\x88\x84\xa4\x20\x5d\x1c\x69\x23\x41"
"\x77\x4d\x4e\xe4\x9a\xf1\x60\xb6\x88\x48\xbc\x59\x4b\x51\x71\x1a\xdc\x12\x69\x21\x13\x7b\xd1\xf1"
"\x1c\xba\x89\xbd\x48\x50\xf4\xc0\x3b\x29\xea\xa9\x37\x39\x79\x6e\x94\xbb\x52\x41\xee\x56\xcc\xb9"
"\x82\xb5\x00\xe3\xcf\x5f\x9c\x92\xf0\xd2\x2d\x41\xb2\xa4\xd5\x47\x07\xb2\x79\x64\xb6\xb0\x17\x07"
"\x0e\x4a\x89\x3c\xf0\x8a\xda\x28\x45\xc6\xa8\xd1\x2f\xc8\xec\x45\x41\x28\xd5\x64\x35\x96\xe1\x9d"
"\x16\x75\x9b\x13\xc6\x91\x6b\xdd\x1f\x71\x30\x3f\x9d\x15\xf5\x62\xe0\x4e\x2a\x25\xee\x6d\x69\x53"
"\xef\xce\x2f\x4d\x5a\x3b\x0f\x81\xef\xe3\xd4\xa4\xac\xa2\x66\xec\x57\x6e\x8f\x96\x78\xdd\x92\x98"
"\x48\xfa\x05\xe2\x9f\x62\x3c\xb8\x24\x09\x65\x55\x19\xbc\x40\x72\x07\x42\xfe\x1d\xd3\x99\x31\x33"
"\x3a\x08\x45\xa2\x0c\x9a\xde\x17\x6f\x36\xfb\x66\xbf\x1f\x89\x65\xa4\x28\x21\xd8\x37\x5a\x8c\x67"
"\x45\x1b\x05\xb5\x72\x49\xc6\xd6\x3c\xc8\x20\x51\x5f\xaf\xe0\x5c\x83\xa5\x03\xd2\xf1\x8b\x6f\x16"
"\x5f\x76\x5d\xc9\x20\x23\xa5\x72\xe3\x94\x61\xaa\x68\xec\xfb\x03\x28\x4b\x2f\xc1\xac\x9d\x48\xb1"
"\x6d\x28\x2b\x8b\x8c\xec\x82\x24\x83\x7a\xb1\x26\x45\xe7\xb9\xee\xb8\x5b\x89\x3d\xfd\x59\x74\xb4"
"\x5d\x86\xa1\x52\x06\x31\xe6\x2c\xc8\x96\xf1\xa2\x52\x8e\x76\x8a\x48\x20\x8f\x75\xf8\x6f\x85\x31"
"\x26\xbe\x26\x62\xf7\x32\x20\xad\x1e\x74\x14\x43\xbe\x8f\x31\xd4\x46\x15\x4a\xc8\x9b\xcf\xe0\x12"
"\xfb\x31\x0a\xd9\xa3\x6c\x53\x74\xea\x13\xbc\x46\xa0\xdd\x69\xf7\x22\x6f\x81\xad\x53\x15\xcc\x67"
"\xb3\x01\x73\x42\x32\x35\x8e\x32\x25\x31\x79\x0a\x14\x84\xab\x7b\x6e\x10\xbb\x3d\x28\x95\x33\xee"
"\xa6\xbd\x21\xef\xf9\x6c\x30\xdd\x1d\x9d\x51\x31\x37\x17\x5c\xa0\x85\x18\x9c\x43\xab\xc5\x92\xd3"
"\x74\x44\xdd\xae\x1f\x60\xbf\x3b\x9b\xc5\x16\x79\xbb\x11\x1a\xdd\x04\xdd\xd7\xd5\x03\x9f\xd4\xf4"
"\xff\x9c\xc9\x21\xea\xbb\xfc\xbc\x63\xfb\x42\xa7\xe5\xa4\x60\x59\x76\x88\x19\xc6\x33\xc6\xc1\x8d"
"\x32\x11\x6f\xbe\x1a\xe3\xe5\xcb\x11\x88\xae\x0e\xba\x7f\x3f\xa6\xdb\x89\xd8\x0c\x19\x7e\xe4\xc3"
"\x9c\x1e\xfb\xed\x04\xa4\xbc\x97\xf4\xd4\xde\x97\xe4\xbb\x78\x78\xae\xe3\xc1\x6e\xba\x4a\x33\x76"
"\xa3\x8b\x74\xa3\x3f\x33\xa7\x6b\xf7\x91\x34\x8a\xda\x3b\x23\x5c\x70\x68\xdb\xe5\xb4\xbf\x4d\x96"
"\xd3\xfe\x02\xd3\x95\xb6\xbf\xcc\x40\xe2\xaf\xf7\xb9\xab\x08\x67\x96\x94\xdd\x1a\x31\xe6\x5c\x19"
"\x62\xa5\x5a\x9d\x5d\xbd\x39\xf6\x0d\x14\xd2\x88\xa4\xd8\x80\x34\x50\x2d\x92\xe9\xeb\x0f\x65\x82"
"\xe5\x14\x57\x0f\x28\xda\xb2\xe6\x8e\x37\x19\xc4\x8a\x09\x3e\x58\xd1\xc5\x6c\x6c\x15\x1d\x58\x2d"
"\x3b\x0f\x0c\x46\xc3\xc2\xd0\x77\x6e\x58\xe0\x8c\x3e\x7f\x94\x3d\xab\x20\x24\x54\x23\xe2\x29\xc5"
"\x90\x8a\x0c\x4d\x87\x4f\x87\xa1\x61\x1d\xde\xa9\xbd\x20\x86\xe0\x71\xc6\xe2\x4d\x78\x6e\xd9\xab"
"\x73\x81\x45\x6e\x39\xed\x67\x0e\x2b\x7a\x58\x0c\xf6\xc3\xda\x2b\x5c\x7b\x09\x89\x84\x32\xbd\x5b"
"\x8d\xe1\xc9\x35\xa3\x72\xd8\xa0\x63\x65\x15\xe1\xa5\x88\xd2\xa0\x9e\x38\xbb\xda\xbb\x3b\x38\xf8"
"\x69\x4f\xbb\xd2\x8a\x3f\x5a\x73\x6d\x4f\x61\x1b\xa5\x51\xe9\xea\x4a\x11\x55\xe9\x0b\x3f\xc5\x2e"
"\x45\xb6\x84\x76\xb6\xb1\x8d\x1f\xa9\x3f\xfd\x41\x4d\x07\x1b\x5f\x06\x7a\x20\xe9\x03\x3d\x5e\x77"
"\x7a\x10\x6a\xbc\x16\x3c\x61\xeb\x47\xaa\xec\xd7\xfd\x85\xeb\xae\xc8\x2d\x7c\x6e\xdd\x63\xf5\xde"
"\xea\x1d\x31\x8a\xf5\x96\xfd\xc8\xee\xd6\xf7\xda\x14\xa3\xd8\xf9\xf5\xea\xe2\x0f\x1d\x29\x68\xd6"
"\x28\x15\xd9\x95\x06\x46\x7c\xe7\x9a\x91\x08\x69\x50\x88\xaa\xf5\x64\x39\x2d\x50\x8d\xa1\xbe\x68"
"\xbd\x62\xa3\x2c\x20\xcb\xf0\x6d\x82\x58\x09\xc9\x4a\x2d\xc4\x7e\xc1\x7f\x69\x12\xad\x2e\x2a\x85"
"\xa1\x85\xb4\x90\x8b\x04\x6d\x50\xe0\x26\x6c\xde\xdb\x1a\x4b\x56\xa8\x55\x06\xca\xb8\x0e\x4d\xd3"
"\x39\x0b\x71\xdd\x0a\x1f\x80\x55\x8e\xd5\x6f\xb2\x06\x75\x96\x81\x6e\xbe\xda\xfd\x42\x2d\x46\xed"
"\x05\x29\x77\x3c\x36\x92\x8a\xf7\x98\x97\x56\xe5\xe4\x4e\x64\x37\xda\x44\x1a\x5e\x7f\xd7\x98\x7f"
"\xbb\x3f\x54\x2a\x75\xaf\x31\x4b\xb8\x19\x5c\xb7\x41\xd3\x2e\x58\x62\x45\x76\xfa\xce\x7c\xdd\xbf"
"\x16\xdd\x6b\x8c\x75\xf3\x7d\x68\x92\xa2\x40\x29\x89\x36\x35\xfd\x50\x0a\x6e\x2e\xb4\x1d\x19\x92"
"\x2d\x61\xca\x48\x40\xc5\x29\x02\x34\xf8\xee\x4c\x05\x0d\xf2\x8f\x1f\xcd\x9f\xce\xae\x4d\xa7\x4f"
"\xb3\x32\x48\x1d\x1d\x27\x41\xd4\xda\x4e\x3d\xec\x91\x13\x2d\x91\x65\x6b\xc8\x27\x12\x2b\x90\xad"
"\x52\x5d\x2a\xea\x85\x04\x55\x49\x6e\xd4\xed\x81\xfc\x85\x55\x3b\xf8\xe2\x3b\xb3\x4c\x61\xda\xdd"
"\xbe\x81\x5f\x58\x2f\x70\xb0\x7c\x30\xb8\x41\xf8\x8b\xdf\xcc\xfd\x54\x27\xf7\x1f\xfa\x31\x6c\xea"
"\x14\x31\xcc\x67\xd6\x26\x0c\xcd\xb3\xcb\x4b\xf3\x3b\x13\xeb\x9c\x19\x98\x62\x63\xda\x77\x70\x6f"
"\x2c\xac\x6a\xf5\x44\xa5\xc0\xad\xdb\x70\x75\x61\xdd\xda\x68\x85\x68\x1f\x41\x77\xc1\xe9\x36\xdb"
"\xa3\x2d\xd7\xaf\xce\xad\x0f\xbd\xb8\x32\xd4\x61\x34\xc1\x7b\xa9\x04\x1c\xd3\x2c\x14\xb2\x60\x9c"
"\x83\xfc\xf9\xfa\xf7\xf3\xf0\x9d\xb9\x65\x09\xbb\xc1\x00\x53\x60\x3a\x26\x2b\x6e\xb0\x76\x4a\x6c"
"\xf5\x05\xeb\x30\x51\xf8\xc5\x8d\xc4\x9a\x85\x4d\x8c\x49\x8e\xa1\x00\xf4\xa6\x00\xd4\x13\x47\x24"
"\xe4\x42\xc1\x0d\x46\x6d\x1f\x1a\x48\x42\x0f\xeb\xa7\xc7\x0d\xfa\x24\xa4\xf9\x7e\x92\x93\x02\x3d"
"\x5d\x99\xfb\x74\x36\x9f\x6d\x9e\x99\x87\x6c\x46\x19\xe4\xbb\xcd\x7b\xd4\xca\x35\xed\x6e\x7c\xc8"
"\x6a\x24\xfb\x41\x30\x6e\x99\x63\x4d\xb0\x02\x35\x6f\xac\x4b\xcb\x9c\x96\x5d\x51\xd0\x92\x6b\x7d"
"\xea\x70\x65\x69\xe7\x6b\x3c\x59\x7b\xac\xc8\xf9\x61\x43\xa6\x8b\x1c\x72\x7b\x73\x71\x85\x21\xd1"
"\x89\x53\x2a\x89\x85\x84\x25\x3b\xab\xd9\x17\xc8\x00\x75\x2a\xd0\x6a\x57\x4f\x5b\x7b\x6c\x7e\x2c"
"\x68\x8d\x13\x3a\x60\xd1\x55\xb1\x5e\x03\xc5\xab\xc5\xbc\x87\xfb\xfa\x80\xdb\x27\xf2\x98\x28\x42"
"\xc4\x7b\x88\xf0\x01\x91\x7b\x18\x0e\xaf\xb2\xcc\xf1\x1f\xfa\xf4\xd7\x43\xdb\x7b\xa7\xc6\x86\xc7"
"\x1b\xde\x1e\x36\xe8\x23\x9f\xe2\xff\x2b\xcd\xb6\x45\x31\x31\xb7\xfb\x94\x5e\x4e\xfb\x3b\x68\x3a"
"\x54\xd2\xee\x6f\xdc\xbf\xdb\x3f\x39\x14\xdc\x0d\x00\x00";
static const int index_lite_gz_len = (int)sizeof(index_lite_gz) - 1;
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
        k_sleep(K_MSEC(10));
#endif
    }
    return 0;
}

static void send_response_bytes(int fd, int status, const char *content_type,
                                const char *content_encoding,
                                const void *body, int body_len)
{
    const char *reason = (status == 200) ? "OK" :
                         (status == 400) ? "Bad Request" :
                         (status == 403) ? "Forbidden" :
                         (status == 409) ? "Conflict" :
                         (status == 404) ? "Not Found" : "Internal Error";
    char hdr[256];
    int hlen;

    if (!body) {
        body_len = 0;
    }

    if (content_encoding) {
        hlen = snprintf(hdr, sizeof(hdr),
                        "HTTP/1.0 %d %s\r\n"
                        "Content-Type: %s\r\n"
                        "Content-Encoding: %s\r\n"
                        "Content-Length: %d\r\n"
                        "Cache-Control: no-store\r\n"
                        "Connection: close\r\n"
                        "\r\n",
                        status, reason, content_type, content_encoding, body_len);
    } else {
        hlen = snprintf(hdr, sizeof(hdr),
                        "HTTP/1.0 %d %s\r\n"
                        "Content-Type: %s\r\n"
                        "Content-Length: %d\r\n"
                        "Cache-Control: no-store\r\n"
                        "Connection: close\r\n"
                        "\r\n",
                        status, reason, content_type, body_len);
    }
    if (hlen <= 0 || hlen >= (int)sizeof(hdr)) {
        return;
    }

    if (!content_encoding && body_len > 0 &&
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

static void send_response_type(int fd, int status, const char *content_type,
                               const char *body)
{
    send_response_bytes(fd, status, content_type, NULL, body,
                        body ? (int)strlen(body) : 0);
}

static void send_json(int fd, int status, const char *body)
{
    send_response_type(fd, status, "application/json", body);
}

static void send_index_page(int fd)
{
#ifdef __ZEPHYR__
    (void)index_lite_gz;
    (void)index_lite_gz_len;
    send_response_type(fd, 200, "text/html; charset=utf-8", index_lite_html);
#else
    send_response_type(fd, 200, "text/html; charset=utf-8", index_html);
#endif
}

static int append_json_str(char *buf, int cap, int *pos, const char *s);

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
                     "\"network_state\":",
                     product_config_peer_count());
    if (n <= 0 || n >= (int)sizeof(buf)) {
        send_json(fd, 500, "{\"error\":\"status too large\"}");
        return;
    }
    int pos = n;
    if (append_json_str(buf, sizeof(buf), &pos, status.network_state) != 0) goto overflow;
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
    normalize_request_path(path, sizeof(path));

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
        send_index_page(fd);
    } else if (strcmp(method, "GET") == 0 && strcmp(path, "/status") == 0) {
        handle_get_status(fd);
    } else if (strcmp(method, "GET") == 0 && strcmp(path, "/config") == 0) {
        handle_get_config(fd);
    } else if (strcmp(method, "POST") == 0 && strcmp(path, "/config") == 0) {
        handle_post_config(fd, body_start);
    } else if (strcmp(method, "POST") == 0 && strcmp(path, "/config/reset") == 0) {
        handle_config_reset(fd);
    } else if (strcmp(method, "POST") == 0 && strcmp(path, "/broker/control") == 0) {
        handle_broker_control(fd, body_start);
    } else if (strcmp(method, "POST") == 0 && strcmp(path, "/publish-test") == 0) {
        handle_publish_test(fd, body_start);
    } else if (strcmp(method, "GET") == 0 && strcmp(path, "/peers") == 0) {
        handle_get_peers(fd);
    } else if (strcmp(method, "GET") == 0 && strcmp(path, "/peer-status") == 0) {
        handle_get_peer_status(fd);
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
