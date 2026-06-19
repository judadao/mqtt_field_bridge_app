const $ = id => document.getElementById(id);
const form = $('peer-form');
const st = $('status');
const ss = $('save-state');
const elog = $('event-log');

let cfg = {};
let token = '';
let peers = [];
let scanResults = [];
let bridgeCurrent = null;
let bridgeRecent = [];
let saveTimer = 0;

function esc(s) {
  return String(s || '').replace(/[&<>"]/g, c => ({
    '&': '&amp;',
    '<': '&lt;',
    '>': '&gt;',
    '"': '&quot;',
  }[c]));
}

async function json(url, opt = {}) {
  opt.headers = opt.headers || {};
  if (token) opt.headers['X-Auth-Token'] = token;
  let r;
  try {
    r = await fetch(url, opt);
  } catch (e) {
    throw new Error(`${url}: ${e.message || e}`);
  }
  if (!r.ok) throw new Error(`${url}: ${await r.text()}`);
  return r.json();
}

function norm(p) {
  return {
    name: p.name || '',
    host: p.host || '',
    mqtt_port: +(p.mqtt_port || 1883),
    p2p_port: +(p.p2p_port || 4884),
    enabled: p.enabled ? 1 : 0,
  };
}

function stamp() {
  return new Date().toLocaleString();
}

function logLine(msg) {
  if (!elog) return;
  const line = `${stamp()}  ${msg}`;
  elog.textContent = elog.textContent === '-' ? line : `${line}\n${elog.textContent}`;
  const rows = elog.textContent.split('\n');
  if (rows.length > 8) elog.textContent = rows.slice(0, 8).join('\n');
}

function notice(msg, cls = 'muted', hold = 1800) {
  clearTimeout(saveTimer);
  ss.textContent = msg;
  ss.className = `pill ${cls} save-toast`;
  if (hold) saveTimer = setTimeout(() => ss.classList.add('hide'), hold);
}

function failMsg(x, prefix) {
  let m = x && x.message ? x.message : 'failed';
  try {
    const j = JSON.parse(m);
    if (j.error) m = j.error;
  } catch (e) {}
  if ($('bridge-details')) $('bridge-details').open = true;
  logLine(`${prefix}: ${m}`);
  notice(`${prefix}: ${m}`, 'bad', 0);
}

function autoBridgePeerIndex() {
  if (!bridgeCurrent || !bridgeCurrent.connected || !bridgeCurrent.current) return -1;
  const current = bridgeCurrent.current;
  const peerHost = bridgeCurrent.peer_broker_ip || current.host || '';
  return peers.findIndex(p => {
    const x = norm(p);
    return x.enabled &&
      x.host === peerHost &&
      x.mqtt_port === +(current.mqtt_port || 1883) &&
      x.p2p_port === +(current.p2p_port || 4884);
  });
}

function isAutoBridgePeer(i, p) {
  return i === autoBridgePeerIndex() && p.enabled;
}

function row(p, i) {
  p = norm(p);
  const current = bridgeCurrent && bridgeCurrent.current ? bridgeCurrent.current : {};
  const wifiName = current.ssid || p.name || '-';
  const localIp = bridgeCurrent && bridgeCurrent.local_sta_ip ? bridgeCurrent.local_sta_ip : '-';
  const gatewayIp = bridgeCurrent && bridgeCurrent.gateway_ip ? bridgeCurrent.gateway_ip : '-';
  const brokerIp = bridgeCurrent && bridgeCurrent.peer_broker_ip ? bridgeCurrent.peer_broker_ip : p.host || '-';
  return `<div class="peer-summary" data-i="${i}"><div class="peer-summary-head"><div><span class="field-label">WiFi Broker</span><strong>${esc(wifiName)}</strong></div><span class="wifi-badge">Auto Bridge WiFi</span></div><div class="peer-summary-grid"><div><span>Peer Index</span><strong>${i}</strong></div><div><span>Broker IP</span><strong>${esc(brokerIp)}</strong></div><div><span>MQTT</span><strong>${p.mqtt_port}</strong></div><div><span>P2P</span><strong>${p.p2p_port}</strong></div><div><span>Local STA IP</span><strong>${esc(localIp)}</strong></div><div><span>Gateway / AP IP</span><strong>${esc(gatewayIp)}</strong></div></div></div>`;
}

function put(c) {
  cfg = c;
  for (const k in c) {
    const e = $(k);
    if (!e) continue;
    if (e.type === 'checkbox') e.checked = !!c[k];
    else e.value = c[k] || '';
  }
  $('cfg_mqtt_port').value = c.mqtt_port || 1883;
  $('cfg_p2p_port').value = c.p2p_port || 4884;
  $('ip-state').textContent = c.device_ip || '-';
  $('summary-gateway').textContent = c.gateway || '-';
  $('summary-netmask').textContent = c.netmask || '-';
  $('summary-dns').textContent = c.dns || '-';
}

function collect() {
  const autoBridge = $('mesh_enabled').checked ? 1 : 0;
  return {
    device_name: $('device_name').value,
    admin_password: $('admin_password').value,
    wifi_ssid: cfg.wifi_ssid || '',
    wifi_password: cfg.wifi_password || '',
    ap_ssid: $('ap_ssid').value,
    ap_password: $('ap_password').value,
    device_ip: $('device_ip').value,
    gateway: $('gateway').value,
    netmask: $('netmask').value,
    dns: $('dns').value,
    dhcp_enabled: $('dhcp_enabled').checked ? 1 : 0,
    site_id: $('site_id').value,
    topic_prefix: cfg.topic_prefix || 'site/field-a',
    mqtt_port: +($('cfg_mqtt_port').value || cfg.mqtt_port || 1883),
    p2p_port: +($('cfg_p2p_port').value || cfg.p2p_port || 4884),
    broker_enabled: $('broker_enabled').checked ? 1 : 0,
    bridge_enabled: autoBridge,
    mesh_enabled: autoBridge,
  };
}

function renderPeers() {
  const visible = peers
    .map((p, i) => ({ p: norm(p), i }))
    .filter(x => isAutoBridgePeer(x.i, x.p));
  form.innerHTML = visible.map(x => row(x.p, x.i)).join('');
  $('peer-empty').classList.toggle('hide', visible.length > 0);
}

function availablePeerIndex() {
  let i = peers.findIndex(p => {
    const x = norm(p);
    return !x.enabled && !x.name && !x.host;
  });
  if (i < 0) i = peers.findIndex(() => true);
  return i;
}

function bridgeJoinPeerIndex() {
  const autoIndex = autoBridgePeerIndex();
  return autoIndex >= 0 ? autoIndex : availablePeerIndex();
}

function bridgeWifiEnabled() {
  return !bridgeCurrent || bridgeCurrent.enabled !== 0;
}

function renderBridgeWifi() {
  const current = bridgeCurrent && bridgeCurrent.current ? bridgeCurrent.current : {};
  const enabled = bridgeWifiEnabled();
  const connected = enabled && bridgeCurrent && bridgeCurrent.connected;
  $('bridge-current').textContent = current.ssid || '-';
  $('bridge-current-detail').textContent = current.host
    ? `${current.host} / MQTT ${current.mqtt_port || '-'} / P2P ${current.p2p_port || '-'}`
    : '-';
  $('bridge-local-sta-ip').textContent = bridgeCurrent && bridgeCurrent.local_sta_ip ? bridgeCurrent.local_sta_ip : '-';
  $('bridge-gateway-ip').textContent = bridgeCurrent && bridgeCurrent.gateway_ip ? bridgeCurrent.gateway_ip : '-';
  $('bridge-peer-broker-ip').textContent = bridgeCurrent && bridgeCurrent.peer_broker_ip ? bridgeCurrent.peer_broker_ip : current.host || '-';
  $('bridge_wifi_enabled').checked = enabled;
  $('scan-bridge-wifi').disabled = !enabled;
  $('bridge-current-state').textContent = connected ? 'connected' : 'disconnected';
  $('bridge-current-state').className = `pill ${connected ? 'ok' : 'bad'}`;
  $('bridge-last-event').textContent = bridgeCurrent && bridgeCurrent.last_event ? bridgeCurrent.last_event : '-';
  $('bridge-last-error').textContent = bridgeCurrent && bridgeCurrent.last_error ? bridgeCurrent.last_error : '-';
  $('bridge-details').open = !!(bridgeCurrent && bridgeCurrent.last_error);

  $('bridge-scan-list').innerHTML = scanResults.length
    ? scanResults.map((x, i) => {
      const active = connected && current.ssid === x.ssid;
      const button = active
        ? `<button class="danger" type="button" ${enabled ? '' : 'disabled'} onclick="disconnectBridgeWifi()">Disconnect</button>`
        : `<button type="button" ${enabled ? '' : 'disabled'} onclick="joinBridgeWifi(${i})">Connect</button>`;
      return `<div class="wifi-row ${active ? 'active' : ''}"><div><div class="wifi-title">${esc(x.ssid)}${active ? '<span class="wifi-badge">Current</span>' : ''}</div><div class="wifi-meta"><span>${esc(x.host)}</span><span>RSSI ${x.rssi ?? '-'}</span><span>CH ${x.channel ?? '-'}</span></div></div>${button}</div>`;
    }).join('')
    : `<div class="empty">${enabled ? 'No scan results' : 'Bridge WiFi disabled'}</div>`;
  $('bridge-recent-list').innerHTML = bridgeRecent.length
    ? bridgeRecent.slice(0, 3).map((x, i) => {
      const active = connected && current.ssid === x.ssid;
      const button = active
        ? `<button class="danger" type="button" ${enabled ? '' : 'disabled'} onclick="disconnectBridgeWifi()">Disconnect</button>`
        : `<button type="button" ${enabled ? '' : 'disabled'} onclick="reconnectBridgeWifi(${i})">Connect</button>`;
      return `<div class="wifi-row ${active ? 'active' : ''}"><div><div class="wifi-title">${esc(x.ssid)}${active ? '<span class="wifi-badge">Current</span>' : ''}</div><div class="wifi-meta"><span>${esc(x.host)}</span><span>MQTT ${x.mqtt_port}</span><span>P2P ${x.p2p_port}</span></div></div>${button}</div>`;
    }).join('')
    : '<div class="empty">No recent bridge WiFi</div>';
}

async function loadBridgeWifi() {
  try {
    [bridgeCurrent, bridgeRecent] = await Promise.all([
      json('/bridge-wifi/current'),
      json('/bridge-wifi/recent'),
    ]);
    renderBridgeWifi();
    renderPeers();
  } catch (x) {
    failMsg(x, 'Bridge WiFi load failed');
  }
}

async function scanBridgeWifi() {
  if (!bridgeWifiEnabled()) {
    notice('Bridge WiFi is disabled', 'bad', 0);
    return;
  }
  try {
    notice('Scanning bridge WiFi', 'muted', 0);
    scanResults = await json('/wifi/scan');
    renderBridgeWifi();
    notice(`${scanResults.length} bridge WiFi found`, 'ok');
  } catch (x) {
    failMsg(x, 'Bridge WiFi scan failed');
  }
}

async function joinBridgeWifiFrom(entry) {
  if (!bridgeWifiEnabled()) {
    notice('Bridge WiFi is disabled', 'bad', 0);
    return;
  }
  const peerIndex = bridgeJoinPeerIndex();
  if (peerIndex < 0) {
    notice('No peer index available', 'bad', 0);
    return;
  }
  try {
    notice(`Joining ${entry.ssid}`, 'muted', 0);
    await json('/bridge-wifi/join', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({
        ssid: entry.ssid,
        password: entry.password || '',
        peer_name: entry.peer_name || entry.ssid,
        host: entry.host,
        mqtt_port: +(entry.mqtt_port || 1883),
        p2p_port: +(entry.p2p_port || 4884),
        peer_index: peerIndex,
      }),
    });
    await load();
    notice(`Joined ${entry.ssid} as peer index ${peerIndex}`, 'ok');
  } catch (x) {
    failMsg(x, `Join ${entry.ssid} failed`);
  }
}

async function setBridgeWifiEnabled() {
  const enabled = $('bridge_wifi_enabled').checked ? 1 : 0;
  try {
    await json('/bridge-wifi/enabled', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ enabled }),
    });
    scanResults = enabled ? scanResults : [];
    await loadBridgeWifi();
    notice(enabled ? 'Bridge WiFi enabled' : 'Bridge WiFi disabled', enabled ? 'ok' : 'muted');
  } catch (x) {
    $('bridge_wifi_enabled').checked = !enabled;
    failMsg(x, 'Bridge WiFi switch failed');
  }
}

async function disconnectBridgeWifi() {
  try {
    notice('Disconnecting bridge WiFi', 'muted', 0);
    await json('/bridge-wifi/disconnect', { method: 'POST' });
    await load();
    notice('Bridge WiFi disconnected', 'ok');
  } catch (x) {
    failMsg(x, 'Bridge WiFi disconnect failed');
  }
}

async function joinBridgeWifi(i) {
  await joinBridgeWifiFrom(scanResults[i]);
}

async function reconnectBridgeWifi(i) {
  await joinBridgeWifiFrom(bridgeRecent[i]);
}

async function load() {
  st.textContent = 'Loading';
  st.className = 'pill muted';
  const [s, c, p] = await Promise.all([
    json('/status'),
    json('/config'),
    json('/peers'),
  ]);
  put(c);
  peers = p.map(norm);
  $('wifi-state').textContent = c.ap_ssid ? `${c.ap_ssid} / ${c.device_ip || '-'}` : 'AP active';
  $('broker-state').textContent = s.broker_state || '-';
  $('p2p-state').textContent = `${s.p2p_role || '-'} / peers ${s.connected_peers || 0}`;
  st.textContent = 'Online';
  st.className = 'pill ok';
  await loadBridgeWifi();
}

async function saveConfig(part) {
  try {
    notice('Saving', 'muted', 0);
    await json('/config', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(collect()),
    });
    await load();
    notice(`${part} saved`, 'ok');
  } catch (x) {
    failMsg(x, 'Save failed');
  }
}

async function resetConfig() {
  try {
    notice('Resetting', 'muted', 0);
    await json('/config/reset', { method: 'POST' });
    token = '';
    $('login').classList.remove('hide');
    $('app').classList.add('hide');
    $('refresh').classList.add('hide');
    $('login-password').value = 'admin';
    $('login-state').textContent = 'Config reset; login again';
    $('login-state').className = 'muted';
    notice('Config reset', 'ok');
  } catch (x) {
    failMsg(x, 'Reset failed');
  }
}

async function brokerControl(enabled) {
  try {
    notice(enabled ? 'Starting broker' : 'Stopping broker', 'muted', 0);
    await json('/broker/control', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ enabled }),
    });
    await load();
    notice(enabled ? 'Broker start requested' : 'Broker stopped', 'ok');
  } catch (x) {
    failMsg(x, 'Broker control failed');
  }
}

function showTab(id) {
  for (const b of document.querySelectorAll('.tab')) {
    b.classList.toggle('active', b.dataset.tab === id);
  }
  for (const v of document.querySelectorAll('.view')) {
    v.classList.toggle('hide', v.id !== id);
  }
}

$('login-form').onsubmit = async e => {
  e.preventDefault();
  try {
    const r = await json('/login', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ password: $('login-password').value }),
    });
    token = r.token;
    $('login').classList.add('hide');
    $('app').classList.remove('hide');
    $('refresh').classList.remove('hide');
    await load();
  } catch (x) {
    $('login-state').textContent = 'Login failed';
    $('login-state').className = 'bad';
  }
};

for (const b of document.querySelectorAll('.tab')) b.onclick = () => showTab(b.dataset.tab);
$('refresh').onclick = load;
$('save-system').onclick = () => saveConfig('System');
$('reset-config').onclick = resetConfig;
$('save-network').onclick = () => saveConfig('Network');
$('save-broker').onclick = () => saveConfig('Broker');
$('mesh_enabled').onchange = () => saveConfig('Auto Bridge');
$('broker-start').onclick = () => brokerControl(1);
$('broker-stop').onclick = () => brokerControl(0);
$('scan-bridge-wifi').onclick = scanBridgeWifi;
$('bridge_wifi_enabled').onchange = setBridgeWifiEnabled;
