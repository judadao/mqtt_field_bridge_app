#!/usr/bin/env node
'use strict';

const fs = require('fs');
const http = require('http');
const os = require('os');
const path = require('path');
const { spawn, spawnSync } = require('child_process');

const ROOT = path.resolve(__dirname, '../..');
const TEST_DIR = path.join(ROOT, 'tests/linux');
const OUT_DIR = path.join(TEST_DIR, 'out');
const SERVER = path.join(OUT_DIR, 'run_web_config_server');
const BASE = 'http://127.0.0.1:8080';

let tests = 0;
let failures = 0;

function check(expr, msg) {
  tests++;
  if (!expr) {
    failures++;
    console.error(`FAIL  ${msg}`);
  }
}

function sleep(ms) {
  return new Promise(resolve => setTimeout(resolve, ms));
}

function requestJson(url, method = 'GET') {
  return new Promise((resolve, reject) => {
    const req = http.request(url, { method }, res => {
      let body = '';
      res.setEncoding('utf8');
      res.on('data', chunk => { body += chunk; });
      res.on('end', () => {
        if (res.statusCode < 200 || res.statusCode >= 300) {
          reject(new Error(`${url}: HTTP ${res.statusCode} ${body}`));
          return;
        }
        try {
          resolve(JSON.parse(body));
        } catch (err) {
          reject(err);
        }
      });
    });
    req.on('error', reject);
    req.end();
  });
}

async function waitForHttp(url, timeoutMs) {
  const deadline = Date.now() + timeoutMs;
  while (Date.now() < deadline) {
    try {
      await requestJson(url);
      return;
    } catch (_) {
      await sleep(100);
    }
  }
  throw new Error(`timed out waiting for ${url}`);
}

function findChrome() {
  const candidates = [
    process.env.CHROME_BIN,
    'google-chrome',
    'chromium',
    'chromium-browser',
  ].filter(Boolean);
  for (const cmd of candidates) {
    const found = spawnSync('which', [cmd], { encoding: 'utf8' });
    if (found.status === 0) return found.stdout.trim();
    if (fs.existsSync(cmd)) return cmd;
  }
  return null;
}

function readHttpText(url) {
  return new Promise((resolve, reject) => {
    http.get(url, res => {
      let body = '';
      res.setEncoding('utf8');
      res.on('data', chunk => { body += chunk; });
      res.on('end', () => resolve(body));
    }).on('error', reject);
  });
}

async function launchChrome() {
  const chrome = findChrome();
  if (!chrome) throw new Error('Chrome binary not found');
  const userDataDir = fs.mkdtempSync(path.join(os.tmpdir(), 'bridge-ui-chrome-'));
  const args = [
    '--headless=new',
    '--disable-gpu',
    '--no-sandbox',
    '--disable-dev-shm-usage',
    '--remote-debugging-port=0',
    `--user-data-dir=${userDataDir}`,
    'about:blank',
  ];
  const proc = spawn(chrome, args, { stdio: ['ignore', 'ignore', 'pipe'] });
  let stderr = '';
  const wsUrl = await new Promise((resolve, reject) => {
    const timer = setTimeout(() => reject(new Error('timed out waiting for Chrome DevTools')), 10000);
    proc.stderr.on('data', chunk => {
      stderr += chunk.toString();
      const match = stderr.match(/DevTools listening on (ws:\/\/[^\s]+)/);
      if (match) {
        clearTimeout(timer);
        resolve(match[1]);
      }
    });
    proc.on('exit', code => {
      clearTimeout(timer);
      reject(new Error(`Chrome exited early with ${code}: ${stderr}`));
    });
  });
  return { proc, wsUrl, userDataDir };
}

function cdpConnect(wsUrl) {
  const ws = new WebSocket(wsUrl);
  let nextId = 1;
  const pending = new Map();
  const handlers = new Set();

  ws.addEventListener('message', event => {
    const msg = JSON.parse(event.data);
    if (msg.id && pending.has(msg.id)) {
      const { resolve, reject } = pending.get(msg.id);
      pending.delete(msg.id);
      if (msg.error) reject(new Error(JSON.stringify(msg.error)));
      else resolve(msg.result || {});
      return;
    }
    for (const handler of handlers) handler(msg);
  });

  function send(method, params = {}, sessionId) {
    const id = nextId++;
    const payload = { id, method, params };
    if (sessionId) payload.sessionId = sessionId;
    ws.send(JSON.stringify(payload));
    return new Promise((resolve, reject) => pending.set(id, { resolve, reject }));
  }

  return new Promise((resolve, reject) => {
    ws.addEventListener('open', () => resolve({ send, handlers, close: () => ws.close() }));
    ws.addEventListener('error', reject);
  });
}

async function waitEval(cdp, sessionId, expression, timeoutMs = 5000) {
  const deadline = Date.now() + timeoutMs;
  while (Date.now() < deadline) {
    const result = await cdp.send('Runtime.evaluate', {
      expression,
      returnByValue: true,
      awaitPromise: true,
    }, sessionId);
    if (result.result && result.result.value) return result.result.value;
    await sleep(100);
  }
  throw new Error(`timed out waiting for expression: ${expression}`);
}

async function evalPage(cdp, sessionId, expression) {
  const result = await cdp.send('Runtime.evaluate', {
    expression,
    returnByValue: true,
    awaitPromise: true,
  }, sessionId);
  if (result.exceptionDetails) {
    throw new Error(JSON.stringify(result.exceptionDetails));
  }
  return result.result ? result.result.value : undefined;
}

async function main() {
  console.log('=== ui_browser_test ===');

  fs.mkdirSync(OUT_DIR, { recursive: true });
  for (const file of ['ui_peers.bin', 'ui_settings.bin', 'ui_bridge_wifi.bin']) {
    try { fs.unlinkSync(path.join(OUT_DIR, file)); } catch (_) {}
  }

  const build = spawnSync('make', ['-C', TEST_DIR, 'out/run_web_config_server'], {
    stdio: 'inherit',
  });
  if (build.status !== 0) process.exit(build.status || 1);

  const server = spawn(SERVER, [], {
    cwd: TEST_DIR,
    env: {
      ...process.env,
      BRIDGE_PEERS_FILE: 'out/ui_peers.bin',
      BRIDGE_SETTINGS_FILE: 'out/ui_settings.bin',
      BRIDGE_WIFI_FILE: 'out/ui_bridge_wifi.bin',
    },
    stdio: ['ignore', 'pipe', 'pipe'],
  });
  server.stdout.on('data', chunk => process.stdout.write(chunk));
  server.stderr.on('data', chunk => process.stderr.write(chunk));

  let chromeProc;
  let cdp;
  try {
    await waitForHttp(`${BASE}/status`, 5000);
    const chrome = await launchChrome();
    chromeProc = chrome.proc;
    cdp = await cdpConnect(chrome.wsUrl);

    const target = await cdp.send('Target.createTarget', { url: 'about:blank' });
    const attached = await cdp.send('Target.attachToTarget', {
      targetId: target.targetId,
      flatten: true,
    });
    const sessionId = attached.sessionId;
    const requests = [];
    cdp.handlers.add(msg => {
      if (msg.sessionId === sessionId &&
          msg.method === 'Network.requestWillBeSent') {
        requests.push({
          url: msg.params.request.url,
          method: msg.params.request.method,
        });
      }
    });

    await cdp.send('Page.enable', {}, sessionId);
    await cdp.send('Runtime.enable', {}, sessionId);
    await cdp.send('Network.enable', {}, sessionId);
    await cdp.send('Page.navigate', { url: `${BASE}/` }, sessionId);
    await waitEval(cdp, sessionId, 'document.readyState === "complete"');

    await evalPage(cdp, sessionId, `
      document.getElementById('login-password').value = 'admin';
      document.getElementById('login-form')
        .dispatchEvent(new Event('submit', { bubbles: true, cancelable: true }));
    `);
    await waitEval(cdp, sessionId, '!document.getElementById("app").classList.contains("hide")');
    await evalPage(cdp, sessionId, `
      document.querySelector('[data-tab="peers"]').click();
    `);
    await waitEval(cdp, sessionId, 'document.querySelector("#broker #mesh_enabled") === null');
    await waitEval(cdp, sessionId, 'document.querySelector("#peers #mesh_enabled") !== null');
    await waitEval(cdp, sessionId, 'document.getElementById("bridge_wifi_enabled").checked === true');
    await waitEval(cdp, sessionId, 'document.getElementById("bridge-current-state").textContent === "disconnected"');
    await waitEval(cdp, sessionId, 'document.getElementById("bridge-details").open === false');
    await waitEval(cdp, sessionId, 'document.querySelector("#bridge-scan-list").closest(".wifi-list-panel").querySelector("#scan-bridge-wifi") !== null');
    requests.length = 0;
    await evalPage(cdp, sessionId, `
      document.getElementById('bridge_wifi_enabled').click();
    `);
    await waitEval(cdp, sessionId, 'document.getElementById("scan-bridge-wifi").disabled === true');
    await waitEval(cdp, sessionId, 'document.getElementById("bridge-current-state").textContent === "disconnected"');
    check(requests.some(r => r.method === 'POST' && r.url === `${BASE}/bridge-wifi/enabled`),
          'bridge WiFi switch should POST /bridge-wifi/enabled');

    await evalPage(cdp, sessionId, `
      document.getElementById('bridge_wifi_enabled').click();
    `);
    await waitEval(cdp, sessionId, 'document.getElementById("scan-bridge-wifi").disabled === false');
    await evalPage(cdp, sessionId, `
      document.getElementById('scan-bridge-wifi').click();
    `);
    await waitEval(cdp, sessionId, 'document.getElementById("bridge-scan-list").textContent.includes("MQTT-BRIDGE-node1")');
    await waitEval(cdp, sessionId, 'document.getElementById("bridge-scan-list").textContent.includes("MQTT-BRIDGE-node10")');
    await waitEval(cdp, sessionId, 'document.getElementById("bridge-scan-list").scrollHeight > document.getElementById("bridge-scan-list").clientHeight');
    requests.length = 0;
    await evalPage(cdp, sessionId, `
      document.querySelector('#bridge-scan-list button').click();
    `);
    await waitEval(cdp, sessionId, 'document.getElementById("save-state").textContent.includes("Joined MQTT-BRIDGE-node1")');
    check(requests.some(r => r.method === 'POST' && r.url === `${BASE}/bridge-wifi/join`),
          'bridge WiFi join should POST /bridge-wifi/join');
    await waitEval(cdp, sessionId, 'document.querySelector("[data-i=\\"0\\"]").textContent.includes("Auto Bridge WiFi")');
    await waitEval(cdp, sessionId, 'document.querySelector("[data-i=\\"0\\"]").textContent.includes("MQTT-BRIDGE-node1")');
    await waitEval(cdp, sessionId, 'document.querySelector("[data-i=\\"0\\"]").textContent.includes("127.0.0.2")');
    await waitEval(cdp, sessionId, 'document.getElementById("add-peer") === null');
    await evalPage(cdp, sessionId, 'document.getElementById("bridge-details").open = true');
    await waitEval(cdp, sessionId, 'document.getElementById("bridge-current").textContent.includes("MQTT-BRIDGE-node1")');
    await waitEval(cdp, sessionId, 'document.getElementById("bridge-peer-broker-ip").textContent.includes("127.0.0.2")');
    await waitEval(cdp, sessionId, 'document.getElementById("bridge-local-sta-ip").textContent.includes("127.0.0.10")');

    requests.length = 0;
    await evalPage(cdp, sessionId, `
      document.querySelector('#bridge-scan-list .wifi-row.active button').click();
    `);
    await waitEval(cdp, sessionId, 'document.getElementById("save-state").textContent.includes("Bridge WiFi disconnected")');
    check(requests.some(r => r.method === 'POST' && r.url === `${BASE}/bridge-wifi/disconnect`),
          'bridge WiFi disconnect should POST /bridge-wifi/disconnect');
    await waitEval(cdp, sessionId, 'document.getElementById("bridge-current-state").textContent === "disconnected"');
    await waitEval(cdp, sessionId, '!document.getElementById("bridge-recent-list").textContent.includes("Current")');
    await waitEval(cdp, sessionId, 'document.querySelector("#bridge-recent-list button").textContent.includes("Connect")');
    await waitEval(cdp, sessionId, 'Array.from(document.querySelectorAll("#bridge-recent-list button")).some(b => b.textContent.includes("Delete"))');
    requests.length = 0;
    await evalPage(cdp, sessionId, `
      Array.from(document.querySelectorAll('#bridge-recent-list button'))
        .find(b => b.textContent.includes('Delete')).click();
    `);
    await waitEval(cdp, sessionId, 'document.getElementById("save-state").textContent.includes("Deleted MQTT-BRIDGE-node1")');
    check(requests.some(r => r.method === 'DELETE' && r.url === `${BASE}/bridge-wifi/recent/0`),
          'bridge WiFi recent delete should DELETE /bridge-wifi/recent/0');
    await waitEval(cdp, sessionId, '!document.getElementById("bridge-recent-list").textContent.includes("MQTT-BRIDGE-node1")');

    await evalPage(cdp, sessionId, `
      const realFetch = window.fetch;
      window.fetch = () => Promise.reject(new Error('forced fetch failure'));
      scanBridgeWifi().finally(() => { window.fetch = realFetch; });
    `);
    await waitEval(cdp, sessionId, 'document.getElementById("event-log").textContent.includes("forced fetch failure")');
    await waitEval(cdp, sessionId, 'document.getElementById("bridge-details").open === true');
    const eventLog = await evalPage(cdp, sessionId, 'document.getElementById("event-log").textContent');
    check(/Bridge WiFi scan failed/.test(eventLog), 'event log should record failed bridge WiFi scan');
    check(/\d/.test(eventLog), 'event log should include a timestamp');

    console.log(`${tests - failures}/${tests} browser checks passed`);
    if (failures) process.exitCode = 1;
  } finally {
    if (cdp) cdp.close();
    if (chromeProc) chromeProc.kill('SIGTERM');
    server.kill('SIGTERM');
  }
}

main().catch(err => {
  console.error(err.stack || err.message || err);
  process.exit(1);
});
