import { invoke } from '@tauri-apps/api/core';
import { listen } from '@tauri-apps/api/event';
import Chart from 'chart.js/auto';
import { warn, debug, trace, info, error } from '@tauri-apps/plugin-log';

// Declare the AndroidBridge interface injected by MainActivity.kt
// This provides runtime permission checking/requesting on Android.
declare global {
  interface Window {
    AndroidBridge?: {
      hasPermissions(permissionsJson: string): boolean;
      requestPermissions(permissionsJson: string): void;
    };
  }
}

const menus = document.querySelectorAll<HTMLElement>('[id$="-menu"]');
const charts = document.querySelectorAll<HTMLElement>(".chart-screen");
// <-- UI state & helpers -->
function showView(id: string) {
  // Use the `hidden` attribute consistently so <section hidden> in HTML works.
  document.querySelectorAll('section').forEach(s => {
    s.hidden = true;
    s.classList.add('hidden');
  });
  const el = document.getElementById(id);
  if (el) {
    el.hidden = false;
    el.classList.remove('hidden');
    // mark submenu panels active for styling when appropriate
    if (id.endsWith('-menu')) {
      el.classList.add('active');
    }
  }
  // hide header controls when on main menu and clear selection state
  ['recordBtn','playBtn','stopBtn'].forEach(cid => {
    const c = document.getElementById(cid);
    if (c) c.style.display = id === 'main-menu' ? 'none' : '';
  });
  if (id === 'main-menu') document.querySelectorAll('.menu-btn').forEach(b => b.classList.remove('selected'));
}
function hideMenus() {
  info(`hideMenus: hiding ${menus.length} menu(s)`);
  menus.forEach(menu => menu.classList.remove("active"));
  menus.forEach(menu => menu.hidden = true);
  
}
function hideAll() {
  menus.forEach(menu => menu.classList.remove("active"));
  menus.forEach(menu => menu.hidden = true);
  charts.forEach(chart => chart.classList.remove("active"));
  charts.forEach(chart => chart.hidden = true);
}
function showMenu(id: string) {
  // Keep for semantic calls but delegate to showView so visibility
  // and header controls stay consistent across the app.
  info(`showMenu: ${id}`);
  showView(id);
}
// NOTE: menu button handling is centralized in `setup()`'s wiring below.
// The older generic binding was removed to avoid duplicate handlers and
// to ensure submenu visibility uses `hidden = true/false` consistently.

function appendLog(line: string) {
  const out = document.getElementById('cell-scan-output') as HTMLPreElement;
  if (!out) return;
  out.textContent = (out.textContent ? out.textContent + '\n' : '') + line;
  out.scrollTop = out.scrollHeight;
}

// <-- Main menu wiring -->
type MenuMapEntry = { view: string; chart?: 'signal' | 'channels' | 'sensor' | 'logs'; autoRun?: boolean };
const menuToTemplate: Record<string, MenuMapEntry> = {
  'wifi': { view: 'wifi-menu' },                             // show Wi‑Fi submenu
  'ble': { view: 'ble-menu' },                               // show BLE submenu
  'subghz': { view: 'sub-ghz-menu' },    // open Sub‑GHz submenu (do not auto-run scanner)
  'nrf-disruptor': { view: 'chart-screen', chart: 'signal' },  // disruptor → signal plot
  'nrf-scanner': { view: 'chart-screen', chart: 'channels' },
  'nfc': { view: 'chart-screen', chart: 'logs' },
  'cell-scan': { view: 'chart-screen', chart: 'logs' },     // moved Cell Scan into main menu
  'infrared': { view: 'chart-screen', chart: 'logs' },
  'gpio': { view: 'chart-screen', chart: 'logs' },
  'apps': { view: 'chart-screen', chart: 'logs' },
  'bad-usb': { view: 'chart-screen', chart: 'logs' },
  'remotes': { view: 'chart-screen', chart: 'logs' },
  'oscilloscope': { view: 'sensor-panel', chart: 'sensor' },
  'wifi-scan': { view: 'chart-screen', chart: 'logs' },
  'sd': { view: 'chart-screen', chart: 'logs' },
  'settings': { view: 'chart-screen', chart: 'logs' },
  'about': { view: 'chart-screen', chart: 'logs' }
};

// Add explicit mappings for sub‑GHz submenu actions (so the UI can route
// and the Rust `run_action` plumbing will be used when desired).
Object.assign(menuToTemplate, {
  'sub-ghz-scanner': { view: 'chart-screen', chart: 'channels', autoRun: false },
  'sub-ghz-playback': { view: 'chart-screen', chart: 'logs' },
  'sub-ghz-recorder': { view: 'chart-screen', chart: 'logs' },
  'sub-ghz-packet-sender': { view: 'chart-screen', chart: 'logs' },
  'sub-ghz-disruptor': { view: 'chart-screen', chart: 'signal' }
});


// submenu items mapping → reuse chart/log templates
Object.assign(menuToTemplate, {
  'ble-scanner': { view: 'chart-screen', chart: 'logs' },
  'ble-packet-sender': { view: 'chart-screen', chart: 'logs' },
  'ble-spoof': { view: 'chart-screen', chart: 'logs' },
  'wifi-scanner': { view: 'chart-screen', chart: 'logs' },
  'wifi-deauth': { view: 'chart-screen', chart: 'logs' },
  'wifi-spoofer': { view: 'chart-screen', chart: 'logs' },
  'wifi-packet-sender': { view: 'chart-screen', chart: 'logs' },
  'wifi-restful': { view: 'chart-screen', chart: 'logs' }
});

// <-- Charts -->
let sensorChart: Chart | null = null;
let signalChart: Chart | null = null;
let channelsChart: Chart | null = null;
const MAX_DATA_POINTS = 60;

function cssVar(name: string, fallback: string) {
  const v = getComputedStyle(document.documentElement).getPropertyValue(name);
  return v ? v.trim() : fallback;
}

function createSignalChart() {
  const c = document.getElementById('signalChart') as HTMLCanvasElement | null;
  if (!c) return null;
  try {
    const start = performance.now();
    const ch = new Chart(c, {
      type: 'line',
      data: {
        labels: Array.from({length:16}, (_,i)=>String(i)),
        datasets: [
          { label: 'R1', data: Array(16).fill(0).map(()=>Math.random()*30+20), borderColor: cssVar('--danger','#ff6384'), tension: 0.2 },
          { label: 'R2', data: Array(16).fill(0).map(()=>Math.random()*30+5), borderColor: cssVar('--accent','#36a2eb'), tension: 0.2 }
        ]
      },
      options: { animation: false, responsive:true, maintainAspectRatio:false }
    });
    info(`createSignalChart: OK (${Math.round(performance.now()-start)}ms)`);
    return ch;
  } catch (err) {
    error(`createSignalChart: error=${String(err)}`);
    appendLog(`signalChart error: ${String(err)}`);
    return null;
  }
}

function createChannelsChart() {
  const c = document.getElementById('channelsChart') as HTMLCanvasElement | null;
  if (!c) return null;
  try {
    const start = performance.now();
    const ch = new Chart(c, {
      type: 'bar',
      data: {
        labels: Array.from({length:16}, (_,i)=>`ch ${i}`),
        datasets: [{ label: 'Energy', backgroundColor: cssVar('--accent','#0ea5e9'), data: Array(16).fill(0).map(()=>Math.random()*80) }]
      },
      options: { animation:false, responsive:true, maintainAspectRatio:false, scales:{y:{beginAtZero:true}} }
    });
    info(`createChannelsChart: OK (${Math.round(performance.now()-start)}ms)`);
    return ch;
  } catch (err) {
    error(`createChannelsChart: error=${String(err)}`);
    appendLog(`channelsChart error: ${String(err)}`);
    return null;
  }
}

function showChart(kind: string) {
  (document.getElementById('signalChart') as HTMLCanvasElement | null)?.parentElement?.classList.toggle('hidden', kind !== 'signal');
  (document.getElementById('channelsChart') as HTMLCanvasElement | null)?.parentElement?.classList.toggle('hidden', kind !== 'channels');
  (document.getElementById('sensorChart') as HTMLCanvasElement | null)?.parentElement?.classList.toggle('hidden', kind !== 'sensor');
  try {
    if (kind === 'signal' && signalChart) signalChart.update();
  } catch (err) { error('showChart(signal) update failed: ' + String(err)); appendLog('Chart error: '+String(err)); }
  try {
    if (kind === 'channels' && channelsChart) channelsChart.update();
  } catch (err) { error('showChart(channels) update failed: ' + String(err)); appendLog('Chart error: '+String(err)); }
  try {
    if (kind === 'sensor' && sensorChart) sensorChart.update();
  } catch (err) { error('showChart(sensor) update failed: ' + String(err)); appendLog('Chart error: '+String(err)); }
}

// <-- playing / recording state -->
let isPlaying = false;
let isRecording = false;
let currentView = 'main-menu';
let currentAction: string | null = null; // track last action (used by Play button)
const recordedEvents: any[] = [];
let simUpdateId: number | null = null;

function setPlaying(val: boolean) {
  isPlaying = val;
  const playBtn = document.getElementById('playBtn') as HTMLButtonElement | null;
  const stopBtn = document.getElementById('stopBtn') as HTMLButtonElement | null;
  if (playBtn && stopBtn) {
    playBtn.disabled = !!isPlaying;
    stopBtn.disabled = !isPlaying;
    playBtn.classList.toggle('playing', isPlaying);
  }
  // IMPORTANT: do NOT generate fake/simulated signal/chart data here.
  // isPlaying controls whether incoming (real) events update charts, but
  // the app will NOT synthesize random data anymore.
  if (!isPlaying) {
    // ensure any leftover sim timer is cleared (defensive)
    if (simUpdateId) { window.clearInterval(simUpdateId); simUpdateId = null; }
  }
}

function setRecording(val: boolean) {
  isRecording = val;
  const rec = document.getElementById('recordBtn');
  if (rec) rec.classList.toggle('recording', isRecording);
  appendLog(isRecording ? '--- RECORDING STARTED ---' : '--- RECORDING STOPPED ---');
}

function enableHeaderControls(enabled: boolean) {
  const rec = document.getElementById('recordBtn') as HTMLButtonElement | null;
  const play = document.getElementById('playBtn') as HTMLButtonElement | null;
  const stop = document.getElementById('stopBtn') as HTMLButtonElement | null;
  if (rec) { rec.style.display = enabled ? '' : 'none'; rec.disabled = !enabled; }
  if (play) { play.style.display = enabled ? '' : 'none'; play.disabled = !enabled || isPlaying; play.classList.toggle('playing', isPlaying); }
  if (stop) { stop.style.display = enabled ? '' : 'none'; stop.disabled = !enabled || !isPlaying; }
}

const BT_PERMISSIONS = [
  'android.permission.BLUETOOTH_SCAN',
  'android.permission.BLUETOOTH_CONNECT',
  'android.permission.ACCESS_FINE_LOCATION'
];

/** Check if all Bluetooth permissions are already granted (sync, via AndroidBridge). */
function checkBluetoothPermissions(): boolean {
  if (!window.AndroidBridge) {
    info('checkBluetoothPermissions: no AndroidBridge (not on Android?), assuming granted');
    return true;
  }
  return window.AndroidBridge.hasPermissions(JSON.stringify(BT_PERMISSIONS));
}

/**
 * Request Bluetooth runtime permissions via the native AndroidBridge.
 * Returns a Promise that resolves to `true` once the user responds.
 * The actual grant/deny result arrives via the 'permissions-result' CustomEvent.
 */
function requestBluetoothPermissions(): Promise<boolean> {
  if (!window.AndroidBridge) {
    info('requestBluetoothPermissions: no AndroidBridge, skipping');
    return Promise.resolve(true);
  }
  // If already granted, short-circuit
  if (checkBluetoothPermissions()) {
    info('requestBluetoothPermissions: already granted');
    return Promise.resolve(true);
  }
  return new Promise((resolve) => {
    // Listen for the one-shot permission result from MainActivity
    const handler = (ev: Event) => {
      window.removeEventListener('permissions-result', handler);
      const detail = (ev as CustomEvent).detail;
      info(`requestBluetoothPermissions: result = ${JSON.stringify(detail)}`);
      // Check if all BT permissions are now granted
      const allGranted = checkBluetoothPermissions();
      resolve(allGranted);
    };
    window.addEventListener('permissions-result', handler);
    info('requestBluetoothPermissions: showing system permission dialog');
    window.AndroidBridge!.requestPermissions(JSON.stringify(BT_PERMISSIONS));
  });
};
async function requestPermissionsRust() {
  info('[requestPermissionsRust] Ensuring BT permissions before Rust call');
  const granted = await requestBluetoothPermissions();
  if (!granted) {
    error('[requestPermissionsRust] BT permissions denied by user');
    return;
  }
  info('[requestPermissionsRust] Permissions granted, invoking Rust command');
  const result = await invoke<string[][]>("trigger_bluetooth_connection_screen", {macaddy: ""});
  info(`[requestPermissionsRust] result: ${JSON.stringify(result)}`);
}
async function requestBluetoothConnectionRust(macaddy: string) {
  info(`[requestBluetoothConnectionRust] Ensuring BT permissions before Rust call (mac=${macaddy})`);
  const granted = await requestBluetoothPermissions();
  if (!granted) {
    error('[requestBluetoothConnectionRust] BT permissions denied by user');
    return;
  }
  info(`[requestBluetoothConnectionRust] Permissions granted, invoking Rust command (mac=${macaddy})`);
  const result = await invoke<string[][]>("trigger_bluetooth_connection_screen", {macaddy: macaddy});
  info(`[requestBluetoothConnectionRust] result: ${JSON.stringify(result)}`);
}

// Persistent saved Bluetooth device (name + mac) helpers using localStorage
function loadSavedBTDevice(): { name?: string; mac?: string } | null {
  const raw = localStorage.getItem('savedBTDevice');
  if (!raw) return null;
  try { return JSON.parse(raw); } catch { return null; }
}

function saveSavedBTDevice(name: string, mac: string) {
  localStorage.setItem('savedBTDevice', JSON.stringify({ name, mac }));
}

// Update the header 'connectedDevice' display from saved device
function updateConnectedDeviceDisplay() {
  const connected = document.getElementById('connectedDevice');
  const saved = loadSavedBTDevice();
  if (!connected) return;
  if (saved && saved.mac && saved.mac.length > 0) {
    connected.textContent = `${saved.name || 'Saved device'} (${saved.mac})`;
  } else {
    connected.textContent = 'No device connected';
  }
}

// Open/close device picker modal and manage device list
async function refreshDeviceList(): Promise<void> {
  const select = document.getElementById('deviceSelect') as HTMLSelectElement | null;
  const msg = document.getElementById('devicePickerMsg') as HTMLElement | null;
  if (!select) return;
  select.innerHTML = '';
  const scanningOpt = document.createElement('option');
  scanningOpt.value = '';
  scanningOpt.text = 'Scanning for paired devices...';
  select.appendChild(scanningOpt);
  try {
    const devices = await invoke<string[][]>('trigger_bluetooth_connection_screen', { macaddy: '' });
    select.innerHTML = '';
    if (!devices || devices.length === 0) {
      const emptyOpt = document.createElement('option');
      emptyOpt.value = '';
      emptyOpt.text = 'No paired / bonded devices found';
      select.appendChild(emptyOpt);
      (document.getElementById('saveDeviceBtn') as HTMLButtonElement).disabled = true;
      (document.getElementById('openBTScreenBtn') as HTMLButtonElement).disabled = true;
      if (msg) msg.textContent = 'No paired devices found.';
      return;
    }
    devices.forEach(d => {
      const name = d[0] || 'Unknown';
      const mac = d[1] || '';
      const bonded = d[3] === 'true';
      const opt = document.createElement('option');
      opt.value = mac;
      opt.text = `${name} — ${mac}${bonded ? ' (bonded)' : ''}`;
      opt.setAttribute('data-name', name);
      select.appendChild(opt);
    });
    const saved = loadSavedBTDevice();
    if (saved && saved.mac) {
      const idx = Array.from(select.options).findIndex(o => o.value === saved.mac);
      if (idx >= 0) select.selectedIndex = idx;
    }
    (document.getElementById('saveDeviceBtn') as HTMLButtonElement).disabled = false;
    (document.getElementById('openBTScreenBtn') as HTMLButtonElement).disabled = false;
    if (msg) msg.textContent = `${devices.length} device(s) found`;
  } catch (e) {
    select.innerHTML = '';
    const errOpt = document.createElement('option');
    errOpt.value = '';
    errOpt.text = 'Error reading devices';
    select.appendChild(errOpt);
    if (msg) msg.textContent = 'Error while scanning for devices';
    (document.getElementById('saveDeviceBtn') as HTMLButtonElement).disabled = true;
    (document.getElementById('openBTScreenBtn') as HTMLButtonElement).disabled = true;
  }
}

async function showDevicePicker() {
  const overlay = document.getElementById('device-picker-overlay') as HTMLElement | null;
  if (!overlay) return;
  overlay.hidden = false;
  const msg = document.getElementById('devicePickerMsg') as HTMLElement | null;
  if (msg) msg.textContent = 'Refreshing device list...';
  await refreshDeviceList();
  if (msg) msg.textContent = '';
}
function hideDevicePicker() {
  const overlay = document.getElementById('device-picker-overlay') as HTMLElement | null;
  if (!overlay) return;
  overlay.hidden = true;
}

// <-- initialize UI & listeners -->
async function setup() {
  info('setup: initializing UI');
  // build charts (templates)
  sensorChart = (function initSensor(){
    const ctx = document.getElementById('sensorChart') as HTMLCanvasElement | null;
    if (!ctx) return null;
    return new Chart(ctx, {
      type: 'line',
      data: { labels: [], datasets: [
        { label: 'X', data: [], borderColor: cssVar('--danger','rgb(255,99,132)'), tension:0.1 },
        { label: 'Y', data: [], borderColor: cssVar('--accent','rgb(54,162,235)'), tension:0.1 },
        { label: 'Z', data: [], borderColor: cssVar('--tertiary','rgb(75,192,192)'), tension:0.1 }
      ]},
      options: { responsive:true, maintainAspectRatio:false, animation:false, scales:{x:{display:false}, y:{min:-2,max:2}} }
    });
  })();
  info('setup: sensor chart initialized');

  const tChartsStart = performance.now();
  signalChart = createSignalChart();
  info('setup: signalChart created');
  channelsChart = createChannelsChart();
  info('setup: channelsChart created');
  info(`charts init took ${Math.round(performance.now() - tChartsStart)}ms`);

  // main menu buttons
  document.querySelectorAll('.menu-btn').forEach(btn => {
    btn.addEventListener('click', (ev) => {
        const action = (ev.currentTarget as HTMLElement).dataset.action || '';
        navigateToAction(action, false);
    });
  });
  info('setup: main menu buttons wired');

  // navigate to a menu action (reusable from click handlers and routing)
  async function navigateToAction(action: string, replaceHistory = false) {
    const map = menuToTemplate[action];
    currentView = map?.view || 'chart-screen';

    // hide any open submenus / charts first
    hideMenus();

    // remember last selected UI action (used by header Play/Stop)
    currentAction = action;

    // If this action maps to a *submenu* (id ends with '-menu'), just show it.
    if (map && map.view && map.view.endsWith('-menu')) {
      if (replaceHistory) history.replaceState({view: map.view}, '', '#'+action);
      else history.pushState({view: map.view}, '', '#'+action);
      showView(map.view);
      if (map.chart) showChart(map.chart as any);
      appendLog(`Opened submenu: ${action}`);
      enableHeaderControls(true);
      // DO NOT auto-start playing / generating data — user will start explicitly
      return;
    }

    // Non-submenu actions: invoke backend `run_action` and then show the
    // configured view (or chart/logs fallback).
    try {
      const saved = loadSavedBTDevice();
      const macaddy = (saved && saved.mac) ? saved.mac : "";
      const result = await invoke<string>('run_action', { action, macaddy });
      appendLog(`run_action(${action}, ${macaddy}) => ${result}`);
    } catch (e) {
      appendLog(`run_action(${action}) failed: ${String(e)}`);
    }

    // Show mapped view (if any) or default to chart logs
    if (map && map.view) {
      showView(map.view);
      if (map.chart) showChart(map.chart as any);
      enableHeaderControls(true);
      // don't auto-play on navigation — let user press Play
    } else {
      showView('chart-screen');
      showChart('logs');
      enableHeaderControls(true);
    }
  }

  // header controls
  const back = document.getElementById('backBtn') as HTMLButtonElement | null;
    back?.addEventListener('click', () => {
      // remove hash from URL without reloading and navigate to main menu
      history.replaceState({}, '', location.pathname + location.search);
      currentView = 'main-menu';
      showView('main-menu');
      showChart('sensor');
      enableHeaderControls(false);
      setPlaying(false);
    });

  const recBtn = document.getElementById('recordBtn') as HTMLButtonElement | null;
  recBtn?.addEventListener('click', () => { setRecording(!isRecording); });

  // wire sub‑GHz input + up/down controls
  const freqInput = document.getElementById('subghzFreqInput') as HTMLInputElement | null;
  document.getElementById('subghzFreqUp')?.addEventListener('click', () => { try { freqInput?.stepUp(); } catch(e){error(String(e));} });
  document.getElementById('subghzFreqDown')?.addEventListener('click', () => { try { freqInput?.stepDown(); } catch(e){error(String(e));} });
  freqInput?.addEventListener('keydown', (ev) => { if ((ev as KeyboardEvent).key === 'ArrowUp') { ev.preventDefault(); try{ freqInput.stepUp(); }catch{} } if ((ev as KeyboardEvent).key === 'ArrowDown') { ev.preventDefault(); try{ freqInput.stepDown(); }catch{} } });

  const playBtn = document.getElementById('playBtn') as HTMLButtonElement | null;
  const stopBtn = document.getElementById('stopBtn') as HTMLButtonElement | null;
  playBtn?.addEventListener('click', async () => {
    // If we're on the sub‑GHz scanner page, start the _device_ scan with
    // the frequency from the input (do NOT auto-start when the page is opened).
    if (currentAction === 'sub-ghz-scanner') {
      const freqEl = document.getElementById('subghzFreqInput') as HTMLInputElement | null;
      const raw = freqEl?.value || '433';
      const mhz = Number(raw);
      if (isNaN(mhz) || mhz < 300 || mhz > 1000) {
        error('[sub-ghz] frequency out of range (300-1000 MHz)');
        appendLog('[sub-ghz] invalid frequency — must be 300-1000 MHz');
        return;
      }
      // send run_action with params (frequency in kHz)
      const macaddy = (loadSavedBTDevice()?.mac) || '';
      try {
        const params = { frequency_khz: Math.round(mhz * 1000), modulation: 'OOK' };
        const res = await invoke<string>('run_action', { action: 'subghz.read.start', macaddy, params: JSON.stringify(params) });
        appendLog(`[sub-ghz] start -> ${res}`);
      } catch (e) {
        appendLog(`[sub-ghz] failed to start scan: ${String(e)}`);
      }
    }

    setPlaying(true);
  });
  stopBtn?.addEventListener('click', () => { setPlaying(false); });

  window.onpopstate = (ev) => {
    const state = ev.state as any;
    if (!state || !state.view) { currentView = 'main-menu'; showView('main-menu'); showChart('sensor'); enableHeaderControls(false); setPlaying(false); return; }
    currentView = state.view;
    showView(state.view);
    enableHeaderControls(state.view !== 'main-menu');
    setPlaying(state.view !== 'main-menu');
  };

  // keep original sensor + cell-scan listeners (reuse existing Tauri events)
  await listen<any>('sensor-update', (event) => {
    if (!isPlaying) return; // respect pause
    const data = event.payload as {x:number;y:number;z:number;timestamp:number};
    if (!sensorChart) return;
    try {
      sensorChart.data.labels?.push(new Date(data.timestamp).toLocaleTimeString());
      (sensorChart.data.datasets[0].data as any[]).push(data.x);
      (sensorChart.data.datasets[1].data as any[]).push(data.y);
      (sensorChart.data.datasets[2].data as any[]).push(data.z);
      if (sensorChart.data.labels && sensorChart.data.labels.length > MAX_DATA_POINTS) {
        sensorChart.data.labels.shift();
        sensorChart.data.datasets.forEach(d => d.data.shift());
      }
      sensorChart.update();
    } catch (err) { error('sensor-update chart update failed: '+String(err)); appendLog('sensor chart error: '+String(err)); }
    if (isRecording) recordedEvents.push({type:'sensor-update', payload: data, ts: Date.now()});
  });
  info('setup: sensor-update listener registered');

  await listen<any>('cell-scan-result', (event) => {
    if (!isPlaying) return; // respect pause
    const r = event.payload as {mcc:number;mnc:number;cid:number;signal_dbm:number;timestamp:number};
    const line = `[CID=${r.cid} mcc=${r.mcc} mnc=${r.mnc} rssi=${r.signal_dbm}dBm]`;
    appendLog(line);
    if (isRecording) recordedEvents.push({type:'cell-scan-result', payload: r, ts: Date.now()});
    // update a channels chart sample slot so UI reflects incoming data
    try {
      if (channelsChart) {
        const idx = Math.floor(Math.random() * (channelsChart.data.labels?.length||16));
        (channelsChart.data.datasets[0].data as number[])[idx] = Math.abs(r.signal_dbm) % 100;
        channelsChart.update();
      }
    } catch (err) { error('cell-scan-result channelsChart update failed: '+String(err)); appendLog('channels chart error: '+String(err)); }
  });
  info('setup: cell-scan-result listener registered');

  // header cell-scan button removed (cell scan is now a main-menu item)
  // (legacy header button listener intentionally removed)

  // permissions / device picker button — open the device-picker modal
  const reqBtn = document.getElementById('requestPermsBtn') as HTMLButtonElement | null;
  reqBtn?.addEventListener('click', () => {
    info('Request Permissions / Choose Device button clicked');
    showDevicePicker().catch(e => error(String(e)));
  });

  // permissions-result listeners (keep compatibility)
  await listen<any>('permissions-result', (event) => {
    appendLog('permissions-result: ' + JSON.stringify(event.payload));
  });
  window.addEventListener('permissions-result', (ev:Event) => { appendLog('permissions-result: ' + JSON.stringify((ev as CustomEvent).detail)); });

  // Device picker modal buttons
  document.getElementById('refreshDeviceBtn')?.addEventListener('click', () => { refreshDeviceList().catch(e => error(String(e))); });
  document.getElementById('cancelDeviceBtn')?.addEventListener('click', () => { hideDevicePicker(); });
  document.getElementById('saveDeviceBtn')?.addEventListener('click', () => {
    const select = document.getElementById('deviceSelect') as HTMLSelectElement | null;
    if (!select) return;
    const opt = select.selectedOptions[0];
    if (!opt || !opt.value) return;
    const name = opt.getAttribute('data-name') || opt.text;
    saveSavedBTDevice(name, opt.value);
    updateConnectedDeviceDisplay();
    hideDevicePicker();
  });
  document.getElementById('openBTScreenBtn')?.addEventListener('click', () => {
    const select = document.getElementById('deviceSelect') as HTMLSelectElement | null;
    const mac = select?.value || '';
    // required by your spec: ensure requestPermissionsRust() runs when this button is pressed
    requestPermissionsRust().catch(e => error(String(e)));
    if (mac) {
      requestBluetoothConnectionRust(mac).catch(e => error(String(e)));
    } else {
      // fallback: open generic Bluetooth connection screen (no mac)
      requestPermissionsRust().catch(e => error(String(e)));
    }
    hideDevicePicker();
  });

  // update header from any saved device
  updateConnectedDeviceDisplay();

  // on initial load, route from URL hash if present so direct links work
  function routeFromHash() {
    const raw = location.hash || '';
    const h = raw.startsWith('#') ? raw.slice(1) : raw;
    if (!h) {
      showView('main-menu');
      showChart('sensor');
      enableHeaderControls(false);
      setPlaying(false);
      return;
    }
    // if hash matches a known action in menuToTemplate (eg. #wifi, #ble, #cell-scan)
    const mapped = menuToTemplate[h];
    if (mapped) {
      navigateToAction(h, true);
      return;
    }
    // if the hash directly references a view id (section id)
    const el = document.getElementById(h);
    if (el) {
      currentView = h;
      showView(h);
      enableHeaderControls(h !== 'main-menu');
      setPlaying(h !== 'main-menu');
      appendLog(`Opened view via URL: ${h}`);
      return;
    }
    // fallback to main menu
    showView('main-menu');
    showChart('sensor');
    enableHeaderControls(false);
    setPlaying(false);
  }
  // global error hook to capture UI crashes and surface in log panel
  window.addEventListener('error', (ev: ErrorEvent) => {
    appendLog(`UI ERROR: ${ev.message} @ ${ev.filename}:${ev.lineno}`);
    error(`UI ERROR: ${ev.message}`, ev.error);
  });

  routeFromHash();
  info('setup: routeFromHash executed');

  // Ensure a persistent key exists; if a saved MAC is present, request connection.
  const saved = loadSavedBTDevice();
  if (!saved) {
    // create an empty persistent value so the key exists across restarts
    saveSavedBTDevice('', '');
    requestBluetoothConnectionRust("").catch(e => error(String(e))); 
  } else if (saved.mac && saved.mac.length > 0) {
    // pass saved MAC to Rust command on startup
    requestBluetoothConnectionRust(saved.mac).catch(e => error(String(e))); 
  }
  info('setup: persistent device checked');
  info('setup: complete');
}

setup().catch(e => error(String(e)));

