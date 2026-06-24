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
  for (const file of ['ui_peers.bin', 'ui_settings.bin']) {
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
      DEPHY_CONFIG_DIR: 'out/ui_config',
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
          postData: msg.params.request.postData || '',
        });
      }
    });

    await cdp.send('Page.enable', {}, sessionId);
    await cdp.send('Runtime.enable', {}, sessionId);
    await cdp.send('Network.enable', {}, sessionId);
    await cdp.send('Page.navigate', { url: `${BASE}/` }, sessionId);
    await waitEval(cdp, sessionId, 'document.readyState === "complete"');

    await waitEval(cdp, sessionId, 'document.getElementById("login-form") === null');
    await waitEval(cdp, sessionId, 'document.getElementById("app") !== null');
    await waitEval(cdp, sessionId, 'document.getElementById("network-state").textContent.includes("static")');
    await waitEval(cdp, sessionId, 'document.getElementById("ip-state").textContent.includes("192.168.127.4")');
    await evalPage(cdp, sessionId, `
      document.querySelector('[data-tab="peers"]').click();
    `);
    await waitEval(cdp, sessionId, 'document.querySelector("#broker #mesh_enabled") === null');
    await waitEval(cdp, sessionId, 'document.querySelector("#peers #mesh_enabled") !== null');
    await waitEval(cdp, sessionId, 'document.getElementById("bridge_peer_index").value === "0"');
    await waitEval(cdp, sessionId, 'document.getElementById("scan-bridge-wifi") === null');
    await waitEval(cdp, sessionId, 'document.body.textContent.includes("Broker Slots")');
    await waitEval(cdp, sessionId, '!document.body.textContent.includes("Bridge WiFi")');
    await evalPage(cdp, sessionId, `
      const peerSelect = document.getElementById('bridge_peer_index');
      peerSelect.value = '1';
      peerSelect.dispatchEvent(new Event('change', { bubbles: true }));
      document.getElementById('peer_name_1').value = 'broker-a';
      document.getElementById('peer_host_1').value = '192.168.127.10';
      document.getElementById('peer_mqtt_1').value = '1884';
      document.getElementById('peer_p2p_1').value = '4885';
      document.getElementById('peer_enabled_1').checked = true;
    `);
    requests.length = 0;
    await evalPage(cdp, sessionId, `
      document.querySelector('[data-i="1"] button').click();
    `);
    await waitEval(cdp, sessionId, 'document.getElementById("save-state").textContent.includes("Broker 1 saved")');
    check(requests.some(r => r.method === 'POST' &&
          r.url === `${BASE}/peers/1` &&
          /"host":"192.168.127.10"/.test(r.postData) &&
          /"p2p_port":4885/.test(r.postData)),
          'broker slot save should POST selected peer body');
    await waitEval(cdp, sessionId, 'document.querySelector("[data-i=\\"1\\"]").textContent.includes("broker-a")');
    await waitEval(cdp, sessionId, 'document.querySelector("[data-i=\\"1\\"]").textContent.includes("192.168.127.10")');
    await waitEval(cdp, sessionId, 'document.getElementById("add-peer") === null');

    await waitEval(cdp, sessionId, 'document.getElementById("event-log") === null');

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
