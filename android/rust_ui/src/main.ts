import { invoke } from '@tauri-apps/api/core';
import { listen } from '@tauri-apps/api/event';
import Chart from 'chart.js/auto';
import { warn, debug, trace, info, error } from '@tauri-apps/plugin-log';

import { currentMonitor } from '@tauri-apps/api/window';
// `currentMonitor()` returns a Promise; we store its result when it arrives so
// other code (and our screenSize helper) can use the actual monitor
// dimensions later.  When the promise resolves we also update the
// screenSize value via a small helper.
let monitor: any = null;

// mutable screenSize object so we can recalc when monitor info becomes
// available.  For callers we expose a simple getter function below that
// always returns the current value.
let screenSize = (function calculateSize() {
  // prefer monitor info when it has resolved
  if (monitor && monitor.size && typeof monitor.size.width === 'number' && typeof monitor.size.height === 'number') {
    return { width: Math.floor(monitor.size.width), height: Math.floor(monitor.size.height) };
  }
  try {
    if (window.screen && typeof window.screen.width === 'number') {
      return { width: Math.floor(window.screen.width), height: Math.floor(window.screen.height) };
    }
  } catch (e) {
    // defensive: fall through to other viewport options
  }
  if (window.visualViewport) {
    return { width: Math.floor(window.visualViewport.width), height: Math.floor(window.visualViewport.height) };
  }
  return { width: document.documentElement.clientWidth, height: document.documentElement.clientHeight };
})();

function updateScreenSizeFromMonitor() {
  if (monitor && monitor.size && typeof monitor.size.width === 'number' && typeof monitor.size.height === 'number') {
    screenSize = { width: Math.floor(monitor.size.width), height: Math.floor(monitor.size.height) };
  }
}

currentMonitor().then(m => { monitor = m; updateScreenSizeFromMonitor(); }).catch(() => { monitor = null; });



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

// Show a modal alert overlay for disconnected radio modules
function showRadioAlert(messages: string[]) {
  // Remove any existing alert
  document.getElementById('radio-alert-overlay')?.remove();

  const overlay = document.createElement('div');
  overlay.id = 'radio-alert-overlay';
  overlay.style.cssText = 'position:fixed;inset:0;background:rgba(0,0,0,0.7);display:flex;align-items:center;justify-content:center;z-index:1100;padding:24px';

  const box = document.createElement('div');
  box.style.cssText = 'max-width:420px;width:100%;background:var(--bg-panel);border-radius:12px;padding:20px;border:1px solid var(--danger);box-shadow:0 8px 40px rgba(220,38,38,0.3)';

  const title = document.createElement('h3');
  title.style.cssText = 'margin:0 0 12px;color:var(--danger);font-size:16px';
  title.textContent = 'Radio Connection Warning';
  box.appendChild(title);

  messages.forEach(msg => {
    const p = document.createElement('p');
    p.style.cssText = 'margin:6px 0;color:var(--text);font-size:14px';
    p.textContent = msg;
    box.appendChild(p);
  });

  const hint = document.createElement('p');
  hint.style.cssText = 'margin:12px 0 0;color:var(--muted);font-size:12px';
  hint.textContent = 'Check SPI wiring, CS/GDO pins, and ensure modules are powered.';
  box.appendChild(hint);

  const dismissBtn = document.createElement('button');
  dismissBtn.className = 'btn';
  dismissBtn.style.cssText = 'margin-top:16px;width:100%';
  dismissBtn.textContent = 'Dismiss';
  dismissBtn.addEventListener('click', () => overlay.remove());
  box.appendChild(dismissBtn);

  overlay.appendChild(box);
  // Also dismiss on clicking outside the box
  overlay.addEventListener('click', (ev) => { if (ev.target === overlay) overlay.remove(); });
  document.body.appendChild(overlay);
}

// Display sub-ghz radio test results on the status screen (not a popup).
// Accepts either a parsed JSON object from BLE notification or a GATT-ack string.
function showSubghzTestResult(data: any) {
  const content = document.getElementById('status-content');
  if (!content) return;
  content.innerHTML = '';

  const addItem = (label: string, val: string, pass: boolean | null) => {
    const div = document.createElement('div');
    div.style.cssText = 'padding:10px 12px;background:var(--bg-panel);border:1px solid rgba(255,255,255,0.08);border-radius:8px;font-size:14px;display:flex;justify-content:space-between;align-items:center';
    const lbl = document.createElement('span');
    lbl.textContent = label;
    lbl.style.color = 'var(--text)';
    const badge = document.createElement('span');
    badge.textContent = val;
    badge.style.cssText = 'font-weight:600;padding:2px 8px;border-radius:4px;font-size:13px';
    if (pass === true) {
      badge.style.color = '#22c55e';
      badge.style.background = 'rgba(34,197,94,0.12)';
    } else if (pass === false) {
      badge.style.color = '#ef4444';
      badge.style.background = 'rgba(239,68,68,0.12)';
    } else {
      badge.style.color = 'var(--muted)';
    }
    div.appendChild(lbl);
    div.appendChild(badge);
    content.appendChild(div);
  };

  // If data is just the GATT ack string (e.g. "sent:subghz.test@..."), show waiting
  if (typeof data === 'string' && !data.startsWith('{')) {
    addItem('Sub-GHz Radio Test', 'Running...', null);
    addItem('Status', 'Waiting for result from device', null);
    showView('status-screen');
    return;
  }

  // Parse JSON if string
  let obj = data;
  if (typeof data === 'string') {
    try { obj = JSON.parse(data); } catch { addItem('Error', 'Invalid response', false); return; }
  }

  const isPass = (v: string) => v === 'pass';
  const overall = obj.overall === 'pass';

  const title = document.createElement('h3');
  title.style.cssText = 'margin:0 0 12px;font-size:16px;text-align:center';
  title.style.color = overall ? '#22c55e' : '#ef4444';
  title.textContent = overall ? 'Radio Test PASSED' : 'Radio Test FAILED';
  content.appendChild(title);

  if (obj.spi1 !== undefined) addItem('SPI Radio 1', obj.spi1.toUpperCase(), isPass(obj.spi1));
  if (obj.spi2 !== undefined) addItem('SPI Radio 2', obj.spi2.toUpperCase(), isPass(obj.spi2));
  if (obj.rssi !== undefined) {
    const delta = obj.rssi_delta !== undefined ? ` (${obj.rssi_delta} dB)` : '';
    addItem('RSSI Carrier Sense', obj.rssi.toUpperCase() + delta, isPass(obj.rssi));
  }
  if (obj.fsk_slow !== undefined) addItem('2-FSK 9.6 kbaud', obj.fsk_slow.toUpperCase(), isPass(obj.fsk_slow));
  if (obj.fsk_fast !== undefined) addItem('2-FSK 100 kbaud', obj.fsk_fast.toUpperCase(), isPass(obj.fsk_fast));
  if (obj.ask_slow !== undefined) addItem('ASK 9.6 kbaud', obj.ask_slow.toUpperCase(), isPass(obj.ask_slow));

  showView('status-screen');
}

// <-- Main menu wiring -->
type MenuMapEntry = { view: string; chart?: 'signal' | 'channels' | 'sensor' | 'logs'; autoRun?: boolean };
const menuToTemplate: Record<string, MenuMapEntry> = {
  'wifi': { view: 'wifi-menu' },                             // show Wi‑Fi submenu
  'ble': { view: 'ble-menu' },                               // show BLE submenu
  'subghz': { view: 'sub-ghz-menu' },    // open Sub‑GHz submenu (do not auto-run scanner)
  'subghz-test': { view: 'status-screen' },
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
  'wifi-channel-scan': { view: 'chart-screen', chart: 'channels', autoRun: false },
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

const WIFI_CHANNELS: Record<string, number> = {
  // 2.4 GHz
  "1": 2412, "2": 2417, "3": 2422, "4": 2427, "5": 2432, "6": 2437, "7": 2442,
  "8": 2447, "9": 2452, "10": 2457, "11": 2462, "12": 2467, "13": 2472, "14": 2484,
  // 5 GHz
  "36": 5180, "40": 5200, "44": 5220, "48": 5240, "52": 5260, "56": 5280, "60": 5300,
  "64": 5320, "100": 5500, "104": 5520, "108": 5540, "112": 5560, "116": 5580, "120": 5600,
  "124": 5620, "128": 5640, "132": 5660, "136": 5680, "140": 5700, "144": 5720, "149": 5745,
  "153": 5765, "157": 5785, "161": 5805, "165": 5825
};
// Reverse map: freq to channel
const WIFI_FREQUENCIES: Record<string, number> = Object.entries(WIFI_CHANNELS).reduce((acc, [c, f]) => { acc[f.toString()] = parseInt(c); return acc; }, {} as Record<string, number>);

function getChannelFromFreq(mhz: number): number | null {
  if (WIFI_FREQUENCIES[mhz.toString()]) return WIFI_FREQUENCIES[mhz.toString()];
  // fallback logic
  if (mhz >= 2412 && mhz <= 2484) return Math.min(14, Math.max(1, Math.round((mhz - 2407) / 5)));
  if (mhz >= 5170 && mhz <= 5835) return Math.round((mhz - 5000) / 5);
  return null;
}

// <-- Charts -->
let sensorChart: Chart | null = null;
let signalChart: Chart | null = null;
let channelsChart: Chart | null = null;
const MAX_DATA_POINTS = 60;

// Channel/frequency range vars (can be set at runtime)
let channelsXMin: number | null = null;
let channelsXMax: number | null = null;

function setChartRange(kind: 'channels' | 'signal', min: number | null, max: number | null) {
  if (kind === 'channels' && channelsChart) {
    channelsXMin = min; channelsXMax = max;
    const xScale: any = channelsChart.options!.scales!['x'] as any;
    if (min !== null) xScale.min = min;
    else delete xScale.min;
    if (max !== null) xScale.max = max;
    else delete xScale.max;
    // Adjust tick step: for frequency ranges >20, use 5 MHz steps
    const range = (max ?? 0) - (min ?? 0);
    xScale.ticks.stepSize = range > 20 ? 5 : 1;
    // Adjust bar width for dense frequency sweeps
    const ds = channelsChart.data.datasets[0] as any;
    if (ds) {
      ds.barPercentage = range > 20 ? 0.4 : 0.8;
      ds.barThickness = range > 50 ? 3 : (range > 20 ? 5 : undefined);
    }
    channelsChart.update();
  }
}

function cssVar(name: string, fallback: string) {
  const v = getComputedStyle(document.documentElement).getPropertyValue(name);
  return v ? v.trim() : fallback;
}

function createSignalChart() {
  const c = document.getElementById('signalChart') as HTMLCanvasElement | null;
  if (!c) return null;
  try {
    const start = performance.now();
    // Create a single, empty line dataset for stability (no initial bars/spikes)
    // Animation is disabled to avoid intermittent crashes on some devices.
    // @ts-ignore global Chart
    // Ensure chart canvas uses exact app viewport size (avoid ~5px overshoot)
    const cw = Math.max(0, screenSize.width - 5);
    const chPx = Math.max(0, screenSize.height - 5);
    c.style.width = `${cw}px`;
    c.style.height = `${chPx}px`;
    c.width = cw;
    c.height = chPx;
    const chart = new Chart(c.getContext('2d') as CanvasRenderingContext2D, {
      type: 'line',
      data: {
        datasets: [
          {
            label: 'Signal',
            data: [],
            borderColor: cssVar('--accent', '#0ea5e9'),
            backgroundColor: 'rgba(14,165,233,0.06)',
            fill: true,
            pointRadius: 0,
            tension: 0.2
          }
        ]
      },
      options: {
        animation: false,
        responsive: true,
        maintainAspectRatio: false,
        interaction: { mode: 'nearest', intersect: false },
        scales: {
          x: { display: true, title: { display: true, text: 'Time' } },
          y: { display: true, title: { display: true, text: 'Amplitude' } }
        },
        plugins: { legend: { display: false } }
      }
    });
    info(`createSignalChart: OK (${Math.round(performance.now() - start)}ms)`);
    try { attachZoomHandlers(c.parentElement as HTMLElement | null); } catch {}
    return chart;
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
    // Use a horizontal bar chart where Y is Channel and X is RSSI (represented as strength 0-100%)
    // Since Chart.js handles categories on the index axis, if indexAxis='y', Y is categorical.
    // However, we want numeric Y (channel).
    // Let's stick to vertical bars (Standard Bar Chart) with X as Channel.
    // But user asked for "annotation of which wifi ssid is on the side". Side usually means Y axis if vertical?
    // Or maybe horizontal bars?
    // Let's implement a Vertical Bar Chart grouped by Channel.
    // X Axis: Channel (Category)
    // Y Axis: Signal Strength (RSSI).
    // Tooltip: SSID.
    
    // To support multiple SSIDs per channel without clutter, we can use a "bubble" chart?
    // Or just a bar chart where we only keep the strongest per channel?
    // The user said "group based on channels" and "annotation... on the side".
    // A horizontal bar chart listing SSIDs on Y axis and RSSI on X axis, grouped by color?
    // No, "line graph for channels... should be bar graph".
    // I will use a Bar chart with data points {x: channel, y: strength, ssid: string}.

    // @ts-ignore global Chart
    // Ensure chart canvas uses exact app viewport size (avoid ~5px overshoot)
    const cw = Math.max(0, screenSize.width - 5);
    const chPx = Math.max(0, screenSize.height - 5);
    info(`createChannelsChart: initializing with canvas size ${cw}x${chPx}`);
    c.style.width = `${cw}px`;
    c.style.height = `${chPx}px`;
    c.width = cw;
    c.height = chPx;
    const chart = new Chart(c.getContext('2d') as CanvasRenderingContext2D, {
      type: 'bar',
      data: { 
        datasets: [{
          label: 'Signal',
          data: [], // objects { x: channel/freq, y: rssi, ssid: 'name', module?:number }
          // per-bar colors will be populated dynamically when signals arrive
          backgroundColor: [] as any[],
          borderWidth: 0,
          barPercentage: 0.8,
          categoryPercentage: 1.0
        }]
      },
      options: {
        animation: false,
        responsive: true,
        maintainAspectRatio: false,
        indexAxis: 'x', // Vertical bars
        scales: {
          x: { 
            type: 'linear', 
            display: true, 
            title: { display: true, text: 'Channel' },
            min: channelsXMin ?? undefined, 
            max: channelsXMax ?? undefined, 
            ticks: { stepSize: 1 } 
          },
          y: { 
            display: true, 
            title: { display: true, text: 'Signal Strength (RSSI dBm)' },
            min: -100,
            max: 0
          }
        },
        plugins: {
          legend: { display: false },
          tooltip: {
            callbacks: {
              label: (ctx: any) => {
                const raw = ctx.raw as any;
                const label = raw.ssid || 'Unknown';
                const xLabel = currentAction?.startsWith('sub-ghz') ? `${raw.x} MHz` : `Ch ${raw.x}`;
                return `${label} @ ${xLabel}: ${raw.y} dBm`;
              }
            }
          }
        }
      }
    });

    info(`createChannelsChart: OK (${Math.round(performance.now() - start)}ms)`);
    try { attachZoomHandlers(c.parentElement as HTMLElement | null); } catch {}
    return chart;
  } catch (err) {
    error(`createChannelsChart: error=${String(err)}`);
    appendLog(`channelsChart error: ${String(err)}`);
    return null;
  }
}

// Lightweight per-container zoom/scroll support for canvases. Zoom is
// applied as a CSS scale on the canvas while the container remains scrollable.
function attachZoomHandlers(container: HTMLElement | null) {
  if (!container) return;
  container.classList.add('zoomable-container');
  const canvas = container.querySelector('canvas') as HTMLCanvasElement | null;
  if (!canvas) return;
  let scale = 1;
  const min = 0.6;
  const max = 3;
  container.style.overflow = 'auto';
  container.style.touchAction = 'pan-x pan-y';
  canvas.style.transformOrigin = 'center center';
  canvas.style.transition = 'transform 80ms linear';
  container.addEventListener('wheel', (ev) => {
    // require ctrl/meta to zoom so normal scroll still pans
    if (!ev.ctrlKey && !ev.metaKey) return;
    ev.preventDefault();
    const delta = ev.deltaY > 0 ? -0.08 : 0.08;
    scale = Math.min(max, Math.max(min, scale + delta));
    canvas.style.transform = `scale(${scale})`;
  }, { passive: false });
}

// Reconfigure the channels chart axes depending on whether we're showing
// Sub-GHz frequency data or Wi-Fi channel data.
function configureChannelsChartForAction(action: string | null) {
  if (!channelsChart) return;
  const xScale = (channelsChart as any).options.scales.x;
  if (action?.startsWith('sub-ghz')) {
    xScale.title.text = 'Frequency (MHz)';
    xScale.ticks.stepSize = 1;
  } else {
    xScale.title.text = 'Channel';
    xScale.ticks.stepSize = 1;
  }
}

function showChart(kind: string) {
  const signalPanel = document.getElementById('signal-panel');
  const channelsPanel = document.getElementById('channels-panel');
  
  // Reconfigure axes for the active action
  if (kind === 'channels') configureChannelsChartForAction(currentAction);

  // Hide log panel for channel scan to maximize space
  const logPanel = document.querySelector('.log-panel') as HTMLElement | null;
  if (logPanel) {
    const hideLog = (currentAction === 'wifi-channel-scan' || currentAction?.startsWith('sub-ghz') || kind === 'channels');
    logPanel.style.display = hideLog ? 'none' : '';
    // Make main panel take full width when log is hidden
    const mainPanel = document.querySelector('.chart-main') as HTMLElement | null;
    if (mainPanel) {
      mainPanel.style.flex = hideLog ? '1 1 100%' : '2';
    }
  }

  if (signalPanel) {
    const show = kind === 'signal';
    signalPanel.classList.toggle('hidden', !show);
    (signalPanel as HTMLElement).hidden = !show;
  }
  if (channelsPanel) {
    const show = kind === 'channels';
    channelsPanel.classList.toggle('hidden', !show);
    (channelsPanel as HTMLElement).hidden = !show;
  }
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
let pendingRadioCheck = false; // set when entering sub-ghz submenu to show radio popup
const recordedEvents: any[] = [];
let simUpdateId: number | null = null;

function setPlaying(val: boolean) {
  const wasPlaying = isPlaying;
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

  // When toggling play state for the Sub‑GHz scanner, send per-radio
  // start/stop commands so the backend/device knows which radio to control.
  (async () => {
    try {
      // Ignore repeated state sets to avoid accidental duplicate stop/start.
      if (wasPlaying === isPlaying) return;

      const action = currentAction;
      if (action !== 'sub-ghz-scanner') return;

      const macaddy = (loadSavedBTDevice()?.mac) || '';
      const in1 = document.getElementById('subghzFreqInput1') as HTMLInputElement | null;
      const in2 = document.getElementById('subghzFreqInput2') as HTMLInputElement | null;
      const mod1el = document.getElementById('subghzModSelect1') as HTMLSelectElement | null;
      const mod2el = document.getElementById('subghzModSelect2') as HTMLSelectElement | null;

      // Read values (MHz) or fall back to sensible defaults
      let f1 = in1 ? Number(in1.value || in1.defaultValue || '400') : NaN;
      let f2 = in2 ? Number(in2.value || in2.defaultValue || '433') : NaN;
      if (isNaN(f1)) f1 = 400;
      if (isNaN(f2)) f2 = 433;

      // Read modulation choices
      const m1 = (mod1el && mod1el.value) ? mod1el.value : 'OOK';
      const m2 = (mod2el && mod2el.value) ? mod2el.value : '2-FSK';

      // LoRa forcing: if either side selects LoRa, force both to LoRa
      const loRaActive = (String(m1).toLowerCase() === 'lora' || String(m2).toLowerCase() === 'lora');
      let modA = loRaActive ? 'LoRa' : m1;
      let modB = loRaActive ? 'LoRa' : m2;

      // If LoRa, clamp frequencies to 900-933 MHz
      if (loRaActive) {
        if (in1) { in1.value = String(Math.max(900, Math.min(933, Math.round(f1*10)/10))); }
        if (in2) { in2.value = String(Math.max(900, Math.min(933, Math.round(f2*10)/10))); }
        f1 = Math.max(900, Math.min(933, f1));
        f2 = Math.max(900, Math.min(933, f2));
        // disable second modulation selector when LoRa is active
        if (mod2el) mod2el.disabled = true;
      } else {
        if (mod2el) mod2el.disabled = false;
      }

      // Enforce maximum span (MHz)
      const MAX_SPAN_MHZ = 33;
      if (Math.abs(f1 - f2) > MAX_SPAN_MHZ) {
        // Keep f1 as the "high" value and adjust the other so span <= MAX_SPAN_MHZ
        if (f1 > f2) {
          f2 = f1 - MAX_SPAN_MHZ;
          if (in2) in2.value = String(Math.round(f2*10)/10);
        } else {
          f1 = f2 - MAX_SPAN_MHZ;
          if (in1) in1.value = String(Math.round(f1*10)/10);
        }
      }

      if (isPlaying) {
        const high = Math.max(f1, f2);
        const low = Math.min(f1, f2);
        // Update chart range and clear old data for the new sweep
        setChartRange('channels', Math.floor(low), Math.ceil(high));
        configureChannelsChartForAction('sub-ghz-scanner');
        if (channelsChart) {
          channelsChart.data.datasets.forEach(ds => { ds.data = []; if (Array.isArray(ds.backgroundColor)) (ds.backgroundColor as any[]).length = 0; });
          channelsChart.update();
        }
        const startParams = {
          top_frequency_mhz: high,
          bottom_frequency_mhz: low,
          modulation_one: modA,
          modulation_two: modB,
          frequency_mhz: high
        };
        await invoke<string>('run_action', { action: 'subghz.read.start', macaddy, params: JSON.stringify(startParams) });
        appendLog(`[sub-ghz] start -> high=${high}MHz low=${low}MHz mod1=${modA} mod2=${modB}`);
      } else {
        await invoke<string>('run_action', { action: 'subghz.read.stop', macaddy, params: JSON.stringify({}) });
        appendLog('[sub-ghz] stop');
      }
    } catch (e) {
      error('[sub-ghz] start/stop dispatch failed: ' + String(e));
    }
  })();
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
  const hdr = document.getElementById('subghz-header-controls') as HTMLElement | null;
  if (hdr) {
    // show header subghz controls only when header controls are enabled and
    // the current action is the sub-ghz scanner; otherwise hide.
    hdr.style.display = enabled && currentAction === 'sub-ghz-scanner' ? '' : 'none';
  }
  const wifihdr = document.getElementById('wifi-header-controls') as HTMLElement | null;
  if (wifihdr) {
    wifihdr.style.display = enabled && currentAction === 'wifi-channel-scan' ? '' : 'none';
  }
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
  // orient app to landscape (either primary or secondary).
  if (screen.orientation && screen.orientation.lock) {
    screen.orientation.lock('landscape').catch(e => {
      info('orientation lock failed: ' + String(e));
    });
  }
  // ensure we have file read/write permissions on Android before using storage
  async function ensureFilePermissions(): Promise<boolean> {
    if (!window.AndroidBridge) {
      return true;
    }
    const perms = ['android.permission.WRITE_EXTERNAL_STORAGE','android.permission.READ_EXTERNAL_STORAGE'];
    const have = window.AndroidBridge.hasPermissions(JSON.stringify(perms));
    if (have) return true;
    return new Promise(resolve => {
      const handler = (ev: Event) => {
        window.removeEventListener('permissions-result', handler);
        const ok = window.AndroidBridge!.hasPermissions(JSON.stringify(perms));
        resolve(ok);
      };
      window.addEventListener('permissions-result', handler);
      window.AndroidBridge.requestPermissions(JSON.stringify(perms));
    });
  }
  const storageOk = await ensureFilePermissions();
  if (!storageOk) {
    error('Storage permissions denied');
  }

  // build charts (templates)
  sensorChart = (function initSensor(){
    const ctx = document.getElementById('sensorChart') as HTMLCanvasElement | null;
    if (!ctx) return null;
    // Ensure chart canvas uses exact app viewport size (avoid ~5px overshoot)
    const cw = Math.max(0, screenSize.width - 5);
    const ch = Math.max(0, screenSize.height - 5);
    ctx.style.width = `${cw}px`;
    ctx.style.height = `${ch}px`;
    ctx.width = cw;
    ctx.height = ch;
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

  // zoom handlers are attached in chart creation

  // Wire header sub‑GHz frequency controls (moved into header). These are
  // hidden by default and shown only when the Sub‑GHz scanner view is active.
  const subFreqInput = document.getElementById('subghzFreqInput') as HTMLInputElement | null;
  const subFreqUp = document.getElementById('subghzFreqUp') as HTMLButtonElement | null;
  const subFreqDown = document.getElementById('subghzFreqDown') as HTMLButtonElement | null;
  const hdrControls = document.getElementById('subghz-header-controls') as HTMLElement | null;
  if (subFreqUp && subFreqDown && subFreqInput) {
    const step = parseFloat(subFreqInput.step || '0.1');
    subFreqUp.addEventListener('click', () => {
      const v = Math.min(parseFloat(subFreqInput.max || '1000'), parseFloat(subFreqInput.value || '433') + step);
      subFreqInput.value = v.toFixed(1);
    });
    subFreqDown.addEventListener('click', () => {
      const v = Math.max(parseFloat(subFreqInput.min || '300'), parseFloat(subFreqInput.value || '433') - step);
      subFreqInput.value = v.toFixed(1);
    });
    subFreqInput.addEventListener('keydown', (ev) => {
      if (ev.key === 'ArrowUp') { ev.preventDefault(); subFreqUp.click(); }
      if (ev.key === 'ArrowDown') { ev.preventDefault(); subFreqDown.click(); }
    });
  }

  // Periodically toggle header controls visibility based on current view/action.
  setInterval(() => {
    try {
      if (!hdrControls) return;
      const shouldShow = (currentView === 'chart-screen' && currentAction === 'sub-ghz-scanner');
      hdrControls.style.display = shouldShow ? '' : 'none';
    } catch (e) { /* noop */ }
  }, 200);

  // main menu buttons — skip the dedicated `systemInfoBtn` so it can
  // perform a single special-purpose Bluetooth command without navigating.
  document.querySelectorAll('.menu-btn').forEach(btn => {
    if ((btn as HTMLElement).id === 'systemInfoBtn') return;
    btn.addEventListener('click', (ev) => {
        const action = (ev.currentTarget as HTMLElement).dataset.action || '';
        navigateToAction(action, false);
    });
  });
  info('setup: main menu buttons wired');

  // Wire the System Info button to send a one-shot status command over
  // Bluetooth (no navigation or UI changes).
  const sysBtn = document.getElementById('systemInfoBtn') as HTMLButtonElement | null;
  if (sysBtn) {
    sysBtn.addEventListener('click', async (ev) => {
      try {
        // Ensure permissions on Android before calling into Rust/Rust->Java
        const ok = await requestBluetoothPermissions();
        if (!ok) {
          appendLog('System Info: Bluetooth permissions denied');
          return;
        }
        appendLog('System Info: sending status request...');
        // Use existing backend plumbing to send a one-shot status query.
        // We now invoke `status.info` so the device can return the full status
        // protobuf defined by our new schema. The UI will detect that and show
        // the status screen.
        const result = await invoke<string>('run_action', { action: 'status.info', macaddy: '' });
        appendLog(`System Info: run_action result: ${result}`);
      } catch (e) {
        appendLog('System Info: error sending status: ' + String(e));
      }
    });
  }
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

      // When opening the Sub-GHz submenu, probe CC1101 connectivity and
      // show a popup if either radio is disconnected.
      if (action === 'subghz') {
        (async () => {
          try {
            const macaddy = (loadSavedBTDevice()?.mac) || '';
            await invoke<string>('run_action', { action: 'status.info', macaddy, params: null });
            // The status response will arrive asynchronously via BLE notification
            // and be decoded by handleRadioSignalData → showStatusObject.
            // We set a flag so the status handler can also show the radio popup.
            pendingRadioCheck = true;
          } catch (e) {
            appendLog(`sub-ghz radio check failed: ${String(e)}`);
          }
        })();
      }

      // DO NOT auto-start playing / generating data — user will start explicitly
      return;
    }

    // Non-submenu actions: invoke backend `run_action` and then show the
    // configured view (or chart/logs fallback).
    try {
      const saved = loadSavedBTDevice();
      const macaddy = (saved && saved.mac) ? saved.mac : "";
      
      let params: any = null;
      if (action === 'wifi-channel-scan') {
         const band = (document.getElementById('wifiBandSelect') as HTMLSelectElement)?.value || '2.4';
         const chan = (document.getElementById('wifiChannelSelect') as HTMLSelectElement)?.value || 'all';
         // ensure chart range matches current band selection
         if (band === '2.4') setChartRange('channels', 1, 14);
         else setChartRange('channels', 36, 165);
         
         params = { band, channel: (chan === 'all' ? 0 : parseInt(chan)) };
      }
      if (action === 'sub-ghz-scanner') {
         // Set chart range to match the frequency inputs
         const f1 = parseFloat((document.getElementById('subghzFreqInput1') as HTMLInputElement)?.value || '433');
         const f2 = parseFloat((document.getElementById('subghzFreqInput2') as HTMLInputElement)?.value || '400');
         const lo = Math.floor(Math.min(f1, f2));
         const hi = Math.ceil(Math.max(f1, f2));
         setChartRange('channels', lo, hi);
         // clear old data
         if (channelsChart) {
           channelsChart.data.datasets.forEach(ds => { ds.data = []; if (Array.isArray(ds.backgroundColor)) (ds.backgroundColor as any[]).length = 0; });
         }
      }

      // Sub-GHz scanner is controlled by Play/Stop (per-radio start/stop)
      // so opening this view should not send a generic run_action command.
      if (action !== 'sub-ghz-scanner') {
        const result = await invoke<string>('run_action', { action, macaddy, params: params ? JSON.stringify(params) : null });
        appendLog(`run_action(${action}, ${macaddy}) => ${result}`);
        if (action === 'subghz-test') {
          showSubghzTestResult(result);
        }
      } else {
        appendLog('sub-ghz scanner ready: press Play to start both radios');
      }
    } catch (e) {
      appendLog(`run_action(${action}) failed: ${String(e)}`);
    }

    // Show mapped view (if any) or default to chart logs
    if (map && map.view) {
      showView(map.view);
      if (map.chart) showChart(map.chart as any);
      enableHeaderControls(true);
      // Auto-play for actions that immediately start streaming data (e.g.
      // wifi-channel-scan sends the BLE command above, so data arrives right away).
      if (action === 'wifi-channel-scan' || action === 'wifi-scan' || action === 'wifi-scanner'
          || action === 'ble-scanner' || action === 'nrf-scanner' || action === 'nrf-disruptor') {
        setPlaying(true);
      }
      if (action === 'sub-ghz-scanner') {
        setPlaying(true);
      }
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
  const subFreqInput1 = document.getElementById('subghzFreqInput1') as HTMLInputElement | null;
  const subFreqUp1 = document.getElementById('subghzFreqUp1') as HTMLButtonElement | null;
  const subFreqDown1 = document.getElementById('subghzFreqDown1') as HTMLButtonElement | null;
  const subFreqInput2 = document.getElementById('subghzFreqInput2') as HTMLInputElement | null;
  const subFreqUp2 = document.getElementById('subghzFreqUp2') as HTMLButtonElement | null;
  const subFreqDown2 = document.getElementById('subghzFreqDown2') as HTMLButtonElement | null;
  if (subFreqUp1 && subFreqDown1 && subFreqInput1) {
    const step = parseFloat(subFreqInput1.step || '0.1');
    subFreqUp1.addEventListener('click', () => { try { subFreqInput1.stepUp(); } catch(e){error(String(e));} });
    subFreqDown1.addEventListener('click', () => { try { subFreqInput1.stepDown(); } catch(e){error(String(e));} });
    subFreqInput1.addEventListener('keydown', (ev) => {
      if (ev.key === 'ArrowUp') {/* Line 670 omitted */}
      if (ev.key === 'ArrowDown') {/* Line 671 omitted */}
    });
  }
  if (subFreqUp2 && subFreqDown2 && subFreqInput2) {
    const step = parseFloat(subFreqInput2.step || '0.1');
    subFreqUp2.addEventListener('click', () => { try { subFreqInput2.stepUp(); } catch(e){error(String(e));} });
    subFreqDown2.addEventListener('click', () => { try { subFreqInput2.stepDown(); } catch(e){error(String(e));} });
    subFreqInput2.addEventListener('keydown', (ev) => {
      if (ev.key === 'ArrowUp') {/* Line 670 omitted */}
      if (ev.key === 'ArrowDown') {/* Line 671 omitted */}
    });
  }

  // modality change listener for LoRa enforcement & defaults
  const mod1el = document.getElementById('subghzModSelect1') as HTMLSelectElement | null;
  const mod2el = document.getElementById('subghzModSelect2') as HTMLSelectElement | null;
  // Default modulation profile for scanner start: radio-1 OOK, radio-2 2-FSK.
  if (mod1el && !mod1el.value) mod1el.value = 'OOK';
  if (mod2el && mod2el.value === 'OOK') mod2el.value = '2-FSK';
  function updateModState() {
    const m1 = mod1el?.value || '';
    const m2 = mod2el?.value || '';
    const loRa = m1.toLowerCase() === 'lora' || m2.toLowerCase() === 'lora';
    if (loRa) {
      if (mod1el) mod1el.value = 'LoRa';
      if (mod2el) { mod2el.value = 'LoRa'; mod2el.disabled = true; }
      if (subFreqInput1) subFreqInput1.value = '933.0';
      if (subFreqInput2) subFreqInput2.value = '900.0';
    } else {
      if (mod2el) mod2el.disabled = false;
      // restore sensible defaults if we were previously in LoRa band
      if (subFreqInput1 && parseFloat(subFreqInput1.value) > 900) subFreqInput1.value = '433.0';
      if (subFreqInput2 && parseFloat(subFreqInput2.value) > 900) subFreqInput2.value = '400.0';
    }
  }
  mod1el?.addEventListener('change', updateModState);
  mod2el?.addEventListener('change', updateModState);
  // enforce any initial constraints/defaults
  updateModState();

  // Wire play/stop header buttons to toggle playing state
  const playBtn = document.getElementById('playBtn') as HTMLButtonElement | null;
  const stopBtn = document.getElementById('stopBtn') as HTMLButtonElement | null;
  playBtn?.addEventListener('click', () => { setPlaying(true); });
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
        (channelsChart.data.datasets[0].data as number[])[idx] = Math.abs(r.signal_dbm) % 100; // *10?
        channelsChart.update();
      }
    } catch (err) { error('cell-scan-result channelsChart update failed: '+String(err)); appendLog('channels chart error: '+String(err)); }
  });
  info('setup: cell-scan-result listener registered');

  // ---- Decode helpers for PROTO:base64 payloads ----
  function decodeProtoPayload(raw: string): any | null {
    // raw is the string from the CustomEvent detail — either JSON, PROTO:base64,
    // or a plain base64 string.
    if (typeof raw !== 'string') return raw; // already an object
    const trimmed = raw.trim();
    if (trimmed.startsWith('{')) {
      try { return JSON.parse(trimmed); } catch { return null; }
    }
    let b64 = trimmed;
    if (trimmed.startsWith('PROTO:')) b64 = trimmed.slice(6);
    let bytes: Uint8Array;
    try {
      const bin = atob(b64);
      bytes = new Uint8Array(bin.length);
      for (let i = 0; i < bin.length; i++) bytes[i] = bin.charCodeAt(i);
    } catch { return null; }
    // strip optional 0xAA55 + u16 length framing
    let cursor = 0;
    if (bytes.length >= 4 && bytes[0] === 0xAA && bytes[1] === 0x55) {
      cursor = 4;
    }
    // minimal protobuf varint parser
    const obj: any = {};
    let i = cursor;
    function readVarint(): number {
      let val = 0, shift = 0;
      while (i < bytes.length) {
        const b = bytes[i++];
        val |= (b & 0x7F) << shift;
        if ((b & 0x80) === 0) break;
        shift += 7;
      }
      return val >>> 0; // unsigned
    }
    // flag to detect whether this appears to be the status struct
    let statusMsg = false;
    while (i < bytes.length) {
      const tag = readVarint();
      const field = tag >>> 3;
      const wire = tag & 0x7;
      if (wire === 0) { // varint
        const val = readVarint();
        // check for known status fields first (they share field numbers with
        // radio messages, so we detect by presence of multiple of them or
        // values that clearly don't match the radio semantics).
        if (field === 1 && (val === 0 || val === 1)) {
          // could be timestamp or is_scanning; defer decision until later
          obj.is_scanning = Boolean(val);
          statusMsg = true;
        } else if (field === 2) {
          if (statusMsg) {
            obj.battery_percent = val;
          } else {
            obj.module = val;
          }
        } else if (field === 3) {
          obj.cc1101_1_connected = Boolean(val);
          statusMsg = true;
        } else if (field === 4) {
          if (statusMsg) {
            obj.cc1101_2_connected = Boolean(val);
          } else {
            // ZigZag-decode RSSI (sint) encoded as varint by firmware
            obj.rssi = ((val >>> 1) ^ -(val & 1));
          }
        } else if (field === 5) { obj.lora_connected = Boolean(val); statusMsg = true; }
        else if (field === 6) { obj.nfc_connected = Boolean(val); statusMsg = true; }
        else if (field === 7) { obj.wifi_connected = Boolean(val); statusMsg = true; }
        else if (field === 8) { obj.bluetooth_connected = Boolean(val); statusMsg = true; }
        else if (field === 9) { obj.ir_connected = Boolean(val); statusMsg = true; }
        else if (field === 10) { obj.serial_connected = Boolean(val); statusMsg = true; }
        else {
          // unknown varint field; skip or store generically
          obj[`f${field}`] = val;
        }
      } else if (wire === 5) { // 32-bit fixed
        if (i + 4 <= bytes.length) {
          const dv = new DataView(bytes.buffer, bytes.byteOffset + i, 4);
          const f = dv.getFloat32(0, true); // little-endian
          i += 4;
          if (field === 3 && !statusMsg) obj.frequency_mhz = f;
          else obj[`f${field}_32`] = f;
        } else break;
      } else if (wire === 2) { // length-delimited
        const len = readVarint();
        if (i + len <= bytes.length) {
          const slice = bytes.slice(i, i + len);
          i += len;
          if (field === 5 && !statusMsg) {
            obj.payload = btoa(String.fromCharCode(...slice));
          } else if (field === 6 && !statusMsg) {
            obj.extra = new TextDecoder().decode(slice);
          } else {
            // unknown string field
            obj[`f${field}_str`] = new TextDecoder().decode(slice);
          }
        } else break;
      } else {
        break; // unknown wire type
      }
    }
    // if all we parsed were status fields, return that object
    if (statusMsg && Object.keys(obj).length > 0) {
      return obj;
    }
    return (Object.keys(obj).length > 0) ? obj : null;
  }


// BLE Advertising Channels: 37, 38, 39 are primary. Data channels 0-36.
// LE scanners typically hop all 3 advertising channels.
function getBleChannel(freq: number): number | null {
  // Approximate frequency to channel mapping for BLE
  // 2402 -> 37, 2426 -> 38, 2480 -> 39 (Advertising)
  // Data channels are 2404-2478
  if (freq === 2402) return 37;
  if (freq === 2426) return 38;
  if (freq === 2480) return 39;
  if (freq >= 2404 && freq <= 2478) {
      return Math.round((freq - 2404) / 2); // Channels 0-36
  }
  return null;
}

  // helper: display a status object in the newly added status screen
  function showStatusObject(st: any) {
    // If this was triggered by the sub-ghz submenu radio check, show a
    // popup alert about disconnected radios instead of navigating away.
    if (pendingRadioCheck) {
      pendingRadioCheck = false;
      const r1 = st.cc1101_1_connected;
      const r2 = st.cc1101_2_connected;
      const msgs: string[] = [];
      if (r1 === false) msgs.push('CC1101 Radio 1 (module 0) is DISCONNECTED');
      if (r2 === false) msgs.push('CC1101 Radio 2 (module 1) is DISCONNECTED');
      if (msgs.length > 0) {
        showRadioAlert(msgs);
      } else {
        appendLog('[sub-ghz] Both CC1101 radios connected');
      }
      // Don't navigate to status screen — stay on sub-ghz submenu
      return;
    }

    const content = document.getElementById('status-content');
    if (!content) return;
    // clear previous
    content.innerHTML = '';
    const addItem = (label: string, val: any) => {
      const div = document.createElement('div');
      div.style.minWidth = '120px';
      div.style.padding = '8px';
      div.style.background = 'var(--bg-panel)';
      div.style.border = '1px solid rgba(255,255,255,0.05)';
      div.style.borderRadius = '8px';
      div.style.fontSize = '14px';
      div.textContent = `${label}: ${val}`;
      content.appendChild(div);
    };
    if (st.is_scanning !== undefined) addItem('Scanning', st.is_scanning ? 'yes' : 'no');
    if (st.battery_percent !== undefined) addItem('Battery', st.battery_percent + '%');
    if (st.cc1101_1_connected !== undefined) addItem('Radio1', st.cc1101_1_connected ? 'connected' : 'off');
    if (st.cc1101_2_connected !== undefined) addItem('Radio2', st.cc1101_2_connected ? 'connected' : 'off');
    if (st.lora_connected !== undefined) addItem('LoRa', st.lora_connected ? 'yes' : 'no');
    if (st.nfc_connected !== undefined) addItem('NFC', st.nfc_connected ? 'yes' : 'no');
    if (st.wifi_connected !== undefined) addItem('Wi-Fi', st.wifi_connected ? 'yes' : 'no');
    if (st.bluetooth_connected !== undefined) addItem('Bluetooth', st.bluetooth_connected ? 'yes' : 'no');
    if (st.ir_connected !== undefined) addItem('IR', st.ir_connected ? 'yes' : 'no');
    if (st.serial_connected !== undefined) addItem('Serial', st.serial_connected ? 'yes' : 'no');
    // switch view to status screen
    showView('status-screen');
    showChart('logs');
    enableHeaderControls(true);
  }

  // Unified handler for radio signal data – works whether data arrives via
  // the Tauri event bus or the window CustomEvent from handleRadioNotification.
  function handleRadioSignalData(raw: any) {
    console.debug('[radio] raw payload', raw);
    const processSignal = (s: any) => {
      const ts = s.timestamp_ms ? new Date(s.timestamp_ms) : new Date();
      appendLog(`[RADIO] ${s.frequency_mhz ?? '?'} MHz rssi=${s.rssi ?? '?'} ${s.extra ? '('+s.extra+')' : ''}`);
      if (signalChart && typeof s.rssi === 'number') {
        signalChart.data.labels = signalChart.data.labels || [];
        signalChart.data.labels.push(ts.toLocaleTimeString());
        (signalChart.data.datasets[0].data as any[]).push(s.rssi);
        if (signalChart.data.labels.length > MAX_DATA_POINTS) {
          signalChart.data.labels.shift();
          signalChart.data.datasets.forEach(d => d.data.shift());
        }
        signalChart.update();
      }
      // For channel analysis we push scatter points with X=Channel, Y=strength/count
      if (channelsChart && typeof s.frequency_mhz === 'number' && s.frequency_mhz > 0) {
        try {
          const ds = channelsChart.data.datasets[0];
          if (!ds) return;

          // Determine mode based on active action to decide how to process
          let channel: number | null = null;
          let isBle = false;

          // console.debug('[chart] processing signal:', s.module, s.frequency_mhz);

          if (currentAction === 'wifi-channel-scan' || s.module === 4 /* WIFI */) {
             const mhz = Number(s.frequency_mhz);
             channel = getChannelFromFreq(mhz);
             if (channel === null) {
               // console.debug('[wifi] dropping unmapped freq', s.frequency_mhz);
               return;
             }
             // console.debug('[wifi] mapped freq', s.frequency_mhz, 'to channel', channel);
          } else if (currentAction === 'ble-scanner' || s.module === 5 /* BT */) {
             channel = getBleChannel(s.frequency_mhz);
             isBle = true;
             if (channel === null && s.frequency_mhz === 0 && s.extra) {
               // If no freq but we have extra data (name/mac), maybe just show as channel 0?
               // Or skip. Skipping for now.
               return; 
             }
          } else {
             // Fallback (non-wifi/ble) – number may be used directly
             channel = s.frequency_mhz; 
          }

          if (channel === null) return; // catch any leftover nulls

          // If in Wi-Fi scan mode, perform active filtering based on header selections
          if (currentAction === 'wifi-channel-scan') {
             const bandSel = (document.getElementById('wifiBandSelect') as HTMLSelectElement)?.value || '2.4';
             const chanSel = (document.getElementById('wifiChannelSelect') as HTMLSelectElement)?.value || 'all';

             // Band filtering
             const is24 = (channel >= 1 && channel <= 14);
             const is5 = (channel >= 36);
             if (bandSel === '2.4' && !is24) return;
             if (bandSel === '5' && !is5) return;

             // Channel filtering
             if (chanSel !== 'all' && parseInt(chanSel) !== channel) return;
          }

          // Bar chart: x=Channel, y=Signal Strength (RSSI)
          // SSID stored in `ssid` property for tooltip
          // Use RSSI directly for Y-axis since chart range is -100 to 0
          const strength = s.rssi ?? -100;
          // Build label: <extra> [payload-as-string]
          let payloadStr = '';
          if (s.payload) {
            try {
              if (typeof s.payload === 'string') {
                const bin = atob(s.payload);
                const arr = new Uint8Array(bin.length);
                for (let ii = 0; ii < bin.length; ii++) arr[ii] = bin.charCodeAt(ii);
                payloadStr = new TextDecoder().decode(arr);
              } else if (Array.isArray(s.payload)) {
                const arr = new Uint8Array(s.payload.length);
                for (let ii = 0; ii < s.payload.length; ii++) arr[ii] = s.payload[ii];
                payloadStr = new TextDecoder().decode(arr);
              }
            } catch (e) {
              payloadStr = String(s.payload);
            }
          }
          const ssid = `${s.extra || 'Unknown'} ${payloadStr ? '['+payloadStr+']' : ''}`;
          
          console.debug('[chart] updating channel', channel, 'with ssid', ssid, 'strength', strength);

          // Check if we already have this SSID on this channel to update it instead of adding redundant bars
          const dataArr = ds.data as any[];
          const existingIdx = dataArr.findIndex(d => d.x === channel && d.ssid === ssid);
          // choose color based on module (0=CC1101_1 purple,1=CC1101_2 red)
          let barColor = '#DC2626';
          if (s.module === 0) barColor = '#8C00FF';
          else if (s.module === 1) barColor = '#DC2626';

          if (existingIdx >= 0) {
             dataArr[existingIdx].y = strength;
             // update color as well
             const bc = ds.backgroundColor as any[];
             if (bc && bc.length > existingIdx) bc[existingIdx] = barColor;
          } else {
             dataArr.push({ x: channel, y: strength, ssid: ssid, module: s.module });
             // append color slot
             const bc = ds.backgroundColor as any[];
             if (bc) bc.push(barColor);
          }

          // keep reasonable history? No, for bar chart we want current snapshot.
          // Maybe clear old ones?
          // We can remove items that haven't been updated recently if we tracked timestamp.
          // For now, let's just limit total count to avoid memory leaks if scanning 100s of APs.
          if (dataArr.length > 200) {
             // remove oldest? or random?
             // Ideally we'd remove by timestamp. Simple shift is okay for now.
             dataArr.shift();
          }
          
          channelsChart.update();
        } catch (e) { error('channelsChart update failed: '+String(e)); }
      }
      if (isRecording) recordedEvents.push({ type: 'radio-signal', payload: s, ts: Date.now() });
    };

    // `raw` may be a string (PROTO:base64 or JSON), an already-parsed object,
    // or a batch wrapper.
    let r: any = raw;
    if (typeof raw === 'string') {
      r = decodeProtoPayload(raw);
      console.debug('[radio] decoded payload', r);
      if (!r) {
        appendLog(`[RADIO] undecoded payload (${raw.length} chars)`);
        return;
      }
    }
    // if this looks like a status struct, show it and bail out
    if (r && (r.battery_percent !== undefined || r.is_scanning !== undefined)) {
      showStatusObject(r);
      return;
    }
    // if this is a sub-ghz test result, route to test display
    if (r && r.subghz_test) {
      showSubghzTestResult(r);
      return;
    }
    if (r && r.type === 'radio-batch' && Array.isArray(r.signals)) {
      for (const s of r.signals) {
        processSignal(s);
        try { invoke('bt_listener_append', { payload: JSON.stringify(s) }).catch(() => {}); } catch {}
      }
    } else {
      processSignal(r);
      try { invoke('bt_listener_append', { payload: JSON.stringify(r) }).catch(() => {}); } catch {}
    }
  }

  // Listen on the Tauri event bus (emitted from Rust backend)
  await listen<any>('radio-signal', (event) => {
    if (!isPlaying) return;
    try { handleRadioSignalData(event.payload); } catch (err) { error('radio-signal (tauri) error: '+String(err)); }
  });

  // Listen on window CustomEvents (dispatched by handleRadioNotification in
  // MainActivity.kt via evaluateJavascript). This is the primary path on
  // Android when BLE notifications arrive.
  window.addEventListener('radio-signal', (ev: Event) => {
    if (!isPlaying) return;
    try {
      const detail = (ev as CustomEvent).detail;
      handleRadioSignalData(detail);
    } catch (err) { error('radio-signal (window) error: '+String(err)); }
  });
  info('setup: radio-signal listener registered');

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

  // Wi-Fi Header Controls Logic
  const wifiBand = document.getElementById('wifiBandSelect') as HTMLSelectElement | null;
  const wifiChan = document.getElementById('wifiChannelSelect') as HTMLSelectElement | null;

  function updateWifiChannelOptions() {
    if (!wifiBand || !wifiChan) return;
    const band = wifiBand.value;
    // preserve selection if possible? No, reset to all makes most sense on band switch.
    while (wifiChan.options.length > 0) wifiChan.remove(0);

    const allOpt = document.createElement('option');
    allOpt.value = 'all';
    allOpt.text = 'All Channels';
    wifiChan.add(allOpt);

    // List standard channels
    const channels = band === '2.4'
      ? Array.from({length: 14}, (_, i) => i + 1)
      : [36, 40, 44, 48, 52, 56, 60, 64, 100, 104, 108, 112, 116, 120, 124, 128, 132, 136, 140, 144, 149, 153, 157, 161, 165];

    channels.forEach(ch => {
      const opt = document.createElement('option');
      opt.value = String(ch);
      opt.text = `Channel ${ch}`;
      wifiChan.add(opt);
    });

    // Update chart range immediately
    if (band === '2.4') setChartRange('channels', 1, 14);
    else setChartRange('channels', 36, 165);
    
    // Clear chart data on band switch to avoid confusion
    if (channelsChart) {
      channelsChart.data.datasets.forEach(ds => ds.data = []);
      channelsChart.update();
    }
  }

  if (wifiBand) {
    wifiBand.addEventListener('change', () => {
       updateWifiChannelOptions();
       if (isPlaying && currentAction === 'wifi-channel-scan') {
          // Restart scan with new params
          const action = 'wifi-channel-scan';
          const saved = loadSavedBTDevice();
          const macaddy = (saved && saved.mac) ? saved.mac : "";
          const band = wifiBand.value || '2.4';
          const chan = (wifiChan?.value || 'all');
          const params = { band, channel: (chan === 'all' ? 0 : parseInt(chan)) };
          
          invoke<string>('run_action', { action, macaddy, params: JSON.stringify(params) })
            .then(r => appendLog(`Switched band to ${band} => ${r}`))
            .catch(e => error(String(e)));
       }
    });
    // Init
    updateWifiChannelOptions();
  }
  
  if (wifiChan) {
    wifiChan.addEventListener('change', () => {
      // Just clear chart when filter changes? Or keep data but filtering happens on-ingest.
      // Better to clear old data that might be hidden now.
      if (channelsChart) {
        channelsChart.data.datasets.forEach(ds => { ds.data = []; if (Array.isArray(ds.backgroundColor)) (ds.backgroundColor as any[]).length = 0; });
        channelsChart.update();
      }
      if (isPlaying && currentAction === 'wifi-channel-scan') {
          // Restart scan with new params (channel specific)
          const action = 'wifi-channel-scan';
          const saved = loadSavedBTDevice();
          const macaddy = (saved && saved.mac) ? saved.mac : "";
          const band = wifiBand?.value || '2.4';
          const chan = (wifiChan?.value || 'all');
          const params = { band, channel: (chan === 'all' ? 0 : parseInt(chan)) };
          
          invoke<string>('run_action', { action, macaddy, params: JSON.stringify(params) })
            .then(r => appendLog(`Switched channel to ${chan} => ${r}`))
            .catch(e => error(String(e)));
      }
    });
  }

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

