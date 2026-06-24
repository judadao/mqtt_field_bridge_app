const $ = id => document.getElementById(id);
const form = $('peer-form');
const st = $('status');
const ss = $('save-state');

let cfg = {};
let peers = [];
let peerStatus = [];
let selectedPeerIndex = 0;
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
  notice(`${prefix}: ${m}`, 'bad', 0);
}

function peerState(i) {
  return peerStatus.find(x => +x.index === +i) || {};
}

function renderPeerSelector() {
  const select = $('bridge_peer_index');
  const current = Number.isInteger(selectedPeerIndex) ? selectedPeerIndex : +(select.value || 0);
  select.innerHTML = peers.map((p, i) => {
    const x = norm(p);
    const label = x.enabled
      ? `Broker ${i} - ${x.name || x.host || 'configured'}`
      : `Broker ${i} - empty`;
    return `<option value="${i}">${esc(label)}</option>`;
  }).join('');
  if (!peers.length) {
    select.innerHTML = '<option value="0">Broker 0</option>';
  }
  const max = Math.max(0, peers.length - 1);
  selectedPeerIndex = Math.min(Math.max(current, 0), max);
  select.value = String(selectedPeerIndex);
}

function row(p, i) {
  p = norm(p);
  const s = peerState(i);
  return `<div class="peer-summary" data-i="${i}">
    <div class="peer-summary-head">
      <div><span class="field-label">Broker ${i}</span><strong>${esc(p.name || 'Unnamed broker')}</strong></div>
      <span class="broker-badge">${esc(s.state || (p.enabled ? 'configured' : 'disabled'))}</span>
    </div>
    <div class="peer-editor-grid">
      <label>Name<input id="peer_name_${i}" value="${esc(p.name)}" maxlength="31"></label>
      <label>Host / IP<input id="peer_host_${i}" value="${esc(p.host)}" maxlength="63"></label>
      <label>MQTT Port<input id="peer_mqtt_${i}" type="number" min="1" max="65535" value="${p.mqtt_port}"></label>
      <label>P2P Port<input id="peer_p2p_${i}" type="number" min="1" max="65535" value="${p.p2p_port}"></label>
      <label>Enabled<input id="peer_enabled_${i}" type="checkbox" ${p.enabled ? 'checked' : ''}></label>
    </div>
    <div class="peer-summary-grid">
      <div><span>Host</span><strong>${esc(p.host || '-')}</strong></div>
      <div><span>MQTT</span><strong>${p.mqtt_port || '-'}</strong></div>
      <div><span>P2P</span><strong>${p.p2p_port || '-'}</strong></div>
      <div><span>Runtime</span><strong>${esc(s.state || '-')}</strong></div>
      <div><span>Error</span><strong>${esc(s.last_error || '-')}</strong></div>
      <div><span>Enabled</span><strong>${p.enabled ? 'yes' : 'no'}</strong></div>
    </div>
    <div class="actions"><button type="button" onclick="savePeer(${i})">Save Broker ${i}</button></div>
  </div>`;
}

function renderPeers() {
  renderPeerSelector();
  form.innerHTML = peers.map((p, i) => row(p, i)).join('');
  $('peer-empty').classList.toggle('hide', peers.length > 0);
  $('peer-current-state').textContent = `${peers.filter(p => norm(p).enabled).length} enabled`;
  $('peer-current-state').className = 'pill ok';
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
  const bridge = $('mesh_enabled').checked ? 1 : 0;
  return {
    device_name: $('device_name').value,
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
    bridge_enabled: bridge,
    mesh_enabled: bridge,
  };
}

async function savePeer(i) {
  const body = {
    name: $(`peer_name_${i}`).value,
    host: $(`peer_host_${i}`).value,
    mqtt_port: +($(`peer_mqtt_${i}`).value || 1883),
    p2p_port: +($(`peer_p2p_${i}`).value || 4884),
    enabled: $(`peer_enabled_${i}`).checked ? 1 : 0,
  };
  try {
    notice(`Saving broker ${i}`, 'muted', 0);
    await json(`/peers/${i}`, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(body),
    });
    selectedPeerIndex = i;
    await loadPeers();
    notice(`Broker ${i} saved`, 'ok');
  } catch (x) {
    failMsg(x, `Save broker ${i} failed`);
  }
}

async function loadPeers() {
  [peers, peerStatus] = await Promise.all([
    json('/peers'),
    json('/peer-status'),
  ]);
  peers = peers.map(norm);
  renderPeers();
}

function renderStatus(s) {
  $('broker-state').textContent = s.broker_state || '-';
  $('p2p-state').textContent = `${s.p2p_role || '-'} / peers ${s.connected_peers || 0}`;
  $('network-state').textContent = `${s.network_state || '-'} / ${s.ip_addr || '-'}`;
  $('network-state').className = 'pill ok';
  st.textContent = 'Online';
  st.className = 'pill ok';
}

async function loadStatus() {
  st.textContent = 'Loading';
  st.className = 'pill muted';
  const s = await json('/status');
  renderStatus(s);
  return s;
}

async function load() {
  const [s, c] = await Promise.all([
    loadStatus(),
    json('/config'),
  ]);
  put(c);
  renderStatus(s);
  await loadPeers();
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
    await load();
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

for (const b of document.querySelectorAll('.tab')) b.onclick = () => showTab(b.dataset.tab);
$('refresh').onclick = load;
$('save-system').onclick = () => saveConfig('System');
$('reset-config').onclick = resetConfig;
$('reset-config-network').onclick = resetConfig;
$('save-network').onclick = () => saveConfig('Network');
$('save-broker').onclick = () => saveConfig('Broker');
$('mesh_enabled').onchange = () => saveConfig('Bridge Routing');
$('broker-start').onclick = () => brokerControl(1);
$('broker-stop').onclick = () => brokerControl(0);
$('bridge_peer_index').onchange = () => {
  selectedPeerIndex = +($('bridge_peer_index').value || 0);
};

load().catch(x => failMsg(x, 'Load failed'));
