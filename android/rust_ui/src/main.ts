import { invoke } from '@tauri-apps/api/core';
import { listen } from '@tauri-apps/api/event';
import Chart from 'chart.js/auto';
import { save } from '@tauri-apps/plugin-dialog';
import { writeTextFile } from '@tauri-apps/plugin-fs';
import { warn, debug, trace, info, error } from '@tauri-apps/plugin-log';

import { currentMonitor } from '@tauri-apps/api/window';
// `currentMonitor()` returns a Promise; we store its result when it arrives so
// other code (and our screenSize helper) can use the actual monitor
// dimensions later.  When the promise resolves we also update the
// screenSize value via a small helper.
let monitor: any = null;

// Use CSS-viewport dimensions (logical pixels).  `currentMonitor()` returns
// physical pixels which are 2-3x larger on high-DPI Android screens and
// caused the app/charts to overflow the visible area.
let screenSize = { width: window.innerWidth, height: window.innerHeight };

function updateScreenSize() {
  screenSize = { width: window.innerWidth, height: window.innerHeight };
  // let CSS 100vw/100vh handle the .app container — no JS override needed
}

// recalc on orientation change or resize
window.addEventListener('resize', updateScreenSize);
currentMonitor().then(m => { monitor = m; /* logged only, no sizing */ }).catch(() => { monitor = null; });



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
  if (obj.lora !== undefined) addItem('LoRa TX (SX1276)', obj.lora.toUpperCase(), isPass(obj.lora));

  showView('status-screen');
}

// Display full device status (radios, BLE, uptime) on the status screen
function showDeviceStatus(data: any) {
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

  // Parse JSON if string
  let obj = data;
  if (typeof data === 'string') {
    try { obj = JSON.parse(data); } catch { addItem('Error', 'Invalid response', false); showView('status-screen'); return; }
  }

  const title = document.createElement('h3');
  title.style.cssText = 'margin:0 0 12px;font-size:16px;text-align:center;color:var(--text)';
  title.textContent = 'Device Status';
  content.appendChild(title);

  if (obj.radios) {
    const r = obj.radios;
    addItem('CC1101 Radio 1', r.cc1101_1 === 'ok' ? 'OK' : 'FAIL', r.cc1101_1 === 'ok');
    addItem('CC1101 Radio 2', r.cc1101_2 === 'ok' ? 'OK' : 'FAIL', r.cc1101_2 === 'ok');
    addItem('LoRa (SX1276)', r.lora === 'ok' ? 'OK' : 'Not Connected', r.lora === 'ok' ? true : null);
    addItem('NFC (PN532)', r.nfc === 'ok' ? 'OK' : 'Not Connected', r.nfc === 'ok' ? true : null);
    addItem('nRF24', r.nrf24 === 'ok' ? 'OK' : 'Not Connected', r.nrf24 === 'ok' ? true : null);
  }

  if (obj.ble_paired !== undefined) addItem('BLE Paired', obj.ble_paired ? 'Yes' : 'No', obj.ble_paired);
  if (obj.ble_connected !== undefined) addItem('BLE Connected', obj.ble_connected ? 'Yes' : 'No', obj.ble_connected);
  if (obj.uptime_sec !== undefined) {
    const s = obj.uptime_sec;
    const hrs = Math.floor(s / 3600);
    const mins = Math.floor((s % 3600) / 60);
    const secs = s % 60;
    addItem('Uptime', `${hrs}h ${mins}m ${secs}s`, null);
  }

  showView('status-screen');
}

// <-- Main menu wiring -->
type MenuMapEntry = { view: string; chart?: 'signal' | 'channels' | 'sensor' | 'logs'; autoRun?: boolean };
const menuToTemplate: Record<string, MenuMapEntry> = {
  'wifi': { view: 'wifi-menu' },                             // show Wi‑Fi submenu
  'ble': { view: 'ble-menu' },                               // show BLE submenu
  'subghz': { view: 'sub-ghz-menu' },    // open Sub‑GHz submenu (do not auto-run scanner)
  'subghz-test': { view: 'status-screen' },
  'device-status': { view: 'status-screen' },
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
  'sub-ghz-algorithms': { view: 'sub-ghz-algorithms' },
  'sub-ghz-disruptor': { view: 'sub-ghz-disruptor' },
  'sub-ghz-smart-disruptor': { view: 'sub-ghz-smart-disruptor' }
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
  'wifi-restful': { view: 'chart-screen', chart: 'logs' },
  'wifi-wireshark': { view: 'wifi-wireshark' },
  'wifi-https-cracker': { view: 'wifi-https-cracker' },
  'wifi-wpa-cracker': { view: 'wifi-wpa-cracker' }
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
      if (range > 50) {
        ds.barPercentage = 0.4;
        ds.barThickness = 3;
        ds.maxBarThickness = 4;
      } else if (range > 20) {
        ds.barPercentage = 0.4;
        ds.barThickness = 5;
        ds.maxBarThickness = 6;
      } else {
        // WiFi channels or narrow ranges — fat bars, auto-sized
        ds.barPercentage = 0.9;
        ds.barThickness = 18;
        ds.maxBarThickness = 24;
        ds.categoryPercentage = 1.0;
      }
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
    // Let Chart.js responsive mode size the canvas to its container
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
    // Let Chart.js responsive mode size the canvas to its container
    const chart = new Chart(c.getContext('2d') as CanvasRenderingContext2D, {
      type: 'bar',
      data: { 
        datasets: [{
          label: 'Signal',
          data: [], // objects { x: channel/freq, y: rssi, ssid: 'name', module?:number }
          // per-bar colors will be populated dynamically when signals arrive
          backgroundColor: [] as any[],
          borderWidth: 0,
          barThickness: 18,
          maxBarThickness: 24,
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
            offset: true,
            min: channelsXMin ?? undefined, 
            max: channelsXMax ?? undefined, 
            ticks: { stepSize: 1 } 
          },
          y: { 
            display: true, 
            title: { display: true, text: 'Signal Strength (RSSI dBm)' },
            // We map RSSI (-100..0) to 0..100 so bars grow upward.  Axis ticks
            // show the original negative values via callback.
            min: 0,
            max: 100,
            ticks: {
              stepSize: 20,
              callback: (val: any) => {
                // val runs 0..100; convert back to dBm
                const dbm = (val as number) - 100;
                return dbm.toString();
              }
            }
          }
        },
        plugins: {
          legend: { display: false },
          tooltip: {
            callbacks: {
              label: (ctx: any) => {
                const raw = ctx.raw as any;
                const rssi = (typeof raw.y === 'number') ? (raw.y - 100) : raw.y;
                if (currentAction?.startsWith('sub-ghz')) {
                  return [
                    `Modulation: ${raw.modulation || 'Unknown'}`,
                    `Freq: ${raw.x} MHz`,
                    `Data Len: ${raw.dataLen ?? 0}`,
                    `RSSI: ${rssi} dBm`
                  ];
                }
                const label = raw.ssid || 'Unknown';
                const xLabel = `Ch ${raw.x}`;
                return `${label} @ ${xLabel}: ${rssi} dBm`;
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
  const ds = channelsChart.data.datasets[0] as any;
  if (action?.startsWith('sub-ghz')) {
    xScale.title.text = 'Frequency (MHz)';
    xScale.ticks.stepSize = 1;
  } else {
    // WiFi channel mode — ensure bars are wide enough to see
    xScale.title.text = 'Channel';
    xScale.ticks.stepSize = 1;
    if (ds) {
      ds.barPercentage = 0.9;
      ds.barThickness = 18;
      ds.maxBarThickness = 24;
      ds.categoryPercentage = 1.0;
    }
  }
}

function refreshVisibleChart(kind: 'signal' | 'channels' | 'sensor') {
  const run = () => {
    try {
      if (kind === 'signal' && signalChart) {
        signalChart.resize();
        signalChart.update('none');
      }
      if (kind === 'channels' && channelsChart) {
        channelsChart.resize();
        channelsChart.update('none');
      }
      if (kind === 'sensor' && sensorChart) {
        sensorChart.resize();
        sensorChart.update('none');
      }
    } catch (err) {
      error(`refreshVisibleChart(${kind}) failed: ${String(err)}`);
    }
  };

  requestAnimationFrame(() => {
    requestAnimationFrame(run);
  });
}

function showChart(kind: string) {
  const signalPanel = document.getElementById('signal-panel');
  const channelsPanel = document.getElementById('channels-panel');
  const snifferDetailPanel = document.getElementById('sniffer-detail-panel');
  const showSnifferDetail = currentAction === 'sub-ghz-recorder';
  
  // Reconfigure axes for the active action
  if (kind === 'channels') configureChannelsChartForAction(currentAction);

  // Hide log panel for channel scan to maximize space
  const logPanel = document.querySelector('.log-panel') as HTMLElement | null;
  if (logPanel) {
    const hideLog = (currentAction === 'wifi-channel-scan' || (currentAction?.startsWith('sub-ghz') && currentAction !== 'sub-ghz-recorder' && currentAction !== 'sub-ghz-playback') || kind === 'channels');
    logPanel.style.display = hideLog ? 'none' : '';
    const mainPanel = document.querySelector('.chart-main') as HTMLElement | null;
    if (mainPanel) {
      mainPanel.style.flex = hideLog ? '1 1 100%' : '2';
    }
    // show recorder controls only when recorder action
    const recControls = document.getElementById('recorder-controls');
    if (recControls) recControls.style.display = (currentAction === 'sub-ghz-recorder') ? 'flex' : 'none';
    // show playback controls only when playback action
    const playbackControls = document.getElementById('playback-controls');
    if (playbackControls) playbackControls.style.display = (currentAction === 'sub-ghz-playback') ? 'flex' : 'none';
    const cellOutput = document.getElementById('cell-scan-output');
    if (cellOutput) cellOutput.style.display = (currentAction === 'sub-ghz-recorder' || currentAction === 'sub-ghz-playback') ? 'none' : '';
  }

  if (signalPanel) {
    const show = !showSnifferDetail && kind === 'signal';
    signalPanel.classList.toggle('hidden', !show);
    (signalPanel as HTMLElement).hidden = !show;
  }
  if (channelsPanel) {
    const show = !showSnifferDetail && kind === 'channels';
    channelsPanel.classList.toggle('hidden', !show);
    (channelsPanel as HTMLElement).hidden = !show;
  }
  if (snifferDetailPanel) {
    snifferDetailPanel.classList.toggle('hidden', !showSnifferDetail);
    (snifferDetailPanel as HTMLElement).hidden = !showSnifferDetail;
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

  if (kind === 'signal' || kind === 'channels' || kind === 'sensor') {
    refreshVisibleChart(kind);
  }
  if (currentAction === 'sub-ghz-recorder') {
    void refreshRecorderDisplay();
  }
  if (currentAction === 'sub-ghz-playback') {
    updatePlaybackDisplay();
  }
}

// <-- playing / recording state -->
let isPlaying = false;
let isRecording = false;
let currentView = 'main-menu';
let currentAction: string | null = null; // track last action (used by Play button)
let pendingRadioCheck = false; // set when entering sub-ghz submenu to show radio popup
const recordedEvents: any[] = [];
let simUpdateId: number | null = null;

// recorder-specific data
type SnifferViewMode = 'hex' | 'binary' | 'text';
let snifferViewMode: SnifferViewMode = 'hex';

type SnifferSessionPacketInput = {
  timestamp_ms: number;
  freq: number;
  mod_name: string;
  rssi: number;
  len: number;
  data: string;
  module: number;
};

type SnifferSessionRecord = SnifferSessionPacketInput & {
  id: number;
};

type SnifferSessionPage = {
  rows: SnifferSessionRecord[];
  total_count: number;
  filtered_count: number;
  offset: number;
  limit: number;
  storage_bytes: number;
};

type SnifferSessionStats = {
  total_count: number;
  storage_bytes: number;
};

const RECORDER_PAGE_SIZE = 100;
const RECORDER_SAVE_PAGE_SIZE = 250;
const RECORDER_CACHE_PAGE_LIMIT = 12;
const RECORDER_ROW_HEIGHT = 28;
const RECORDER_OVERSCAN_ROWS = 30;
const RECORDER_TEMP_WARN_BYTES = 25 * 1024 * 1024;

let selectedRecorderId: number | null = null;
let selectedRecorderRecord: SnifferSessionRecord | null = null;
let recorderPageCache = new Map<number, SnifferSessionRecord[]>();
let recorderPageUseOrder: number[] = [];
let recorderPagesInFlight = new Set<string>();
let recorderTotalCount = 0;
let recorderFilteredCount = 0;
let recorderStorageBytes = 0;
let recorderLastThreshold = Number.NaN;
let recorderQueryGeneration = 0;
let recorderPendingEntries: SnifferSessionPacketInput[] = [];
let recorderFlushTimer: number | null = null;
let recorderFlushPromise: Promise<void> | null = null;
let recorderViewportRefreshQueued = false;

// ── Sniffer Signal Database (localStorage) ──
const SNIFFER_DB_KEY = 'sharkos_sniffer_db';
const SNIFFER_DB_MAX_BYTES = 50 * 1024 * 1024; // 50 MB warning threshold
const SNIFFER_DB_CRITICAL_BYTES = 100 * 1024 * 1024; // 100 MB hard limit

function snifferDbLoad(): any[] {
  try {
    const raw = localStorage.getItem(SNIFFER_DB_KEY);
    return raw ? JSON.parse(raw) : [];
  } catch { return []; }
}
function snifferDbSave(db: any[]) {
  localStorage.setItem(SNIFFER_DB_KEY, JSON.stringify(db));
}
function snifferDbAdd(entry: any) {
  const db = snifferDbLoad();
  db.push(entry);
  snifferDbSave(db);
  return db.length;
}
function snifferDbClear() {
  localStorage.removeItem(SNIFFER_DB_KEY);
}
function snifferDbSizeBytes(): number {
  const raw = localStorage.getItem(SNIFFER_DB_KEY);
  return raw ? raw.length * 2 : 0; // UTF-16 chars ≈ 2 bytes each
}
function checkSnifferDbSize() {
  const bytes = snifferDbSizeBytes();
  const warnEl = document.getElementById('recorder-size-warn');
  if (bytes >= SNIFFER_DB_CRITICAL_BYTES) {
    if (warnEl) { warnEl.style.display = ''; warnEl.textContent = '⚠ Storage critical! (' + (bytes / 1024 / 1024).toFixed(1) + ' MB)'; }
    // Show a popup alert
    alert('⚠ Sniffer database is very large (' + (bytes / 1024 / 1024).toFixed(1) + ' MB). Consider exporting and clearing to avoid performance issues.');
  } else if (bytes >= SNIFFER_DB_MAX_BYTES) {
    if (warnEl) { warnEl.style.display = ''; warnEl.textContent = '⚠ Storage large (' + (bytes / 1024 / 1024).toFixed(1) + ' MB)'; }
  } else {
    if (warnEl) warnEl.style.display = 'none';
  }
}
// Disruptor state tracking
let disruptorRadio1Active = false;
let disruptorRadio2Active = false;
let disruptorPower1 = 0; // 0=LOW, 1=MID, 2=MAX
let disruptorPower2 = 0;
// Smart disruptor state
let smartDisruptorActive = false;
let smartDisruptorPower1 = 0;
let smartDisruptorChart: any = null;

function updateDisruptorBubbles() {
  const b1 = document.getElementById('disruptor-bubble-1');
  const b2 = document.getElementById('disruptor-bubble-2');
  const range1 = (document.getElementById('disruptorFreqRange1') as HTMLSelectElement | null)?.value || '387-464';
  const mod1 = (document.getElementById('disruptorModSelect1') as HTMLSelectElement | null)?.value || '?';
  const range2 = (document.getElementById('disruptorFreqRange2') as HTMLSelectElement | null)?.value || '387-464';
  const mod2 = (document.getElementById('disruptorModSelect2') as HTMLSelectElement | null)?.value || '?';
  const label1 = range1 === 'custom' ? `${(document.getElementById('disruptorStartFreq1') as HTMLInputElement)?.value || '?'}-${(document.getElementById('disruptorStopFreq1') as HTMLInputElement)?.value || '?'}` : range1;
  const label2 = range2 === 'custom' ? `${(document.getElementById('disruptorStartFreq2') as HTMLInputElement)?.value || '?'}-${(document.getElementById('disruptorStopFreq2') as HTMLInputElement)?.value || '?'}` : range2;
  if (b1) {
    b1.className = 'disruptor-bubble ' + (disruptorRadio1Active ? 'active' : 'inactive');
    b1.textContent = disruptorRadio1Active
      ? `Radio 1 disrupting ${label1} MHz (${mod1})`
      : `Radio 1 idle – ${label1} MHz (${mod1})`;
  }
  if (b2) {
    b2.className = 'disruptor-bubble ' + (disruptorRadio2Active ? 'active' : 'inactive');
    b2.textContent = disruptorRadio2Active
      ? `Radio 2 disrupting ${label2} MHz (${mod2})`
      : `Radio 2 idle – ${label2} MHz (${mod2})`;
  }
}

function updateSmartDisruptorBubble() {
  const b1 = document.getElementById('smart-disruptor-bubble-1');
  if (b1) {
    b1.className = 'disruptor-bubble ' + (smartDisruptorActive ? 'active' : 'inactive');
    b1.textContent = smartDisruptorActive ? 'Running' : 'Idle';
  }
}

function downloadCsv(filename: string, text: string) {
  const blob = new Blob([text], { type: 'text/csv' });
  const url = URL.createObjectURL(blob);
  const a = document.createElement('a');
  a.href = url;
  a.download = filename;
  a.click();
  URL.revokeObjectURL(url);
}

function csvEscape(value: unknown): string {
  const str = String(value ?? '');
  return `"${str.replace(/"/g, '""')}"`;
}

function buildRecorderCsv(rows: any[]): string {
  const lines = ['time,frequency_mhz,modulation,rssi,data_length,raw_data_hex'];
  rows.forEach(rec => {
    const timestamp = new Date(rec.timestamp_ms || rec.timestamp || Date.now()).toISOString();
    const freq = rec.freq || rec.frequency_mhz || '';
    const mod = rec.mod || rec.modulation || '';
    const rssi = rec.rssi ?? '';
    const len = rec.len ?? '';
    const data = rec.data || rec.payload || '';
    lines.push([
      csvEscape(timestamp),
      csvEscape(freq),
      csvEscape(mod),
      csvEscape(rssi),
      csvEscape(len),
      csvEscape(data)
    ].join(','));
  });
  return lines.join('\n');
}

async function saveTextAsCsv(text: string, defaultName: string) {
  const filePath = await save({
    defaultPath: defaultName,
    filters: [{ name: 'CSV', extensions: ['csv'] }]
  });
  if (!filePath) return false;
  await writeTextFile(filePath, text);
  appendLog(`[sniffer] Saved CSV to ${filePath}`);
  return true;
}

function getRecorderTimestamp(rec: any): number {
  return rec.timestamp_ms || rec.timestamp || Date.now();
}

function getRecorderPayload(rec: any): string {
  return String(rec.data || rec.payload || '');
}

function parseHexPayload(raw: string): Uint8Array | null {
  const normalized = raw.replace(/0x/gi, '').replace(/[^0-9a-f]/gi, '');
  if (!normalized || normalized.length % 2 !== 0) return null;
  const bytes = new Uint8Array(normalized.length / 2);
  for (let i = 0; i < normalized.length; i += 2) {
    bytes[i / 2] = parseInt(normalized.slice(i, i + 2), 16);
  }
  return bytes;
}

function formatHexPayload(raw: string): string {
  const normalized = raw.replace(/0x/gi, '').replace(/[^0-9a-f]/gi, '').toUpperCase();
  if (!normalized) return '(empty payload)';
  const bytes = normalized.match(/.{1,2}/g) || [];
  const lines: string[] = [];
  for (let i = 0; i < bytes.length; i += 16) {
    lines.push(bytes.slice(i, i + 16).join(' '));
  }
  return lines.join('\n');
}

function formatBinaryPayload(raw: string): string {
  const bytes = parseHexPayload(raw);
  if (!bytes) return 'Binary view unavailable: payload is not valid hex.';
  const lines: string[] = [];
  for (let i = 0; i < bytes.length; i += 6) {
    lines.push(Array.from(bytes.slice(i, i + 6)).map(b => b.toString(2).padStart(8, '0')).join(' '));
  }
  return lines.join('\n');
}

function formatTextPayload(raw: string): string {
  const bytes = parseHexPayload(raw);
  if (!bytes) return raw || '(empty payload)';
  const decoded = new TextDecoder('utf-8', { fatal: false }).decode(bytes);
  return decoded.replace(/[\x00-\x08\x0B\x0C\x0E-\x1F\x7F]/g, ' ');
}

function formatSnifferPayload(raw: string, mode: SnifferViewMode): string {
  switch (mode) {
    case 'binary':
      return formatBinaryPayload(raw);
    case 'text':
      return formatTextPayload(raw);
    case 'hex':
    default:
      return formatHexPayload(raw);
  }
}

function normalizeRecorderThreshold(value: string | number | null | undefined): number {
  if (typeof value === 'number') {
    if (!Number.isFinite(value)) return -200;
    const normalized = Math.trunc(value);
    return normalized > 0 ? -normalized : normalized;
  }

  const raw = String(value ?? '').trim();
  if (!raw || raw === '-') return -200;

  const parsed = parseInt(raw, 10);
  if (Number.isNaN(parsed)) return -200;
  return parsed > 0 ? -parsed : parsed;
}

function getRecorderThreshold(): number {
  const threshEl = document.getElementById('recorder-thresh') as HTMLInputElement | null;
  return normalizeRecorderThreshold(threshEl?.value);
}

function syncRecorderThresholdInput() {
  const threshEl = document.getElementById('recorder-thresh') as HTMLInputElement | null;
  if (!threshEl) return;
  const raw = threshEl.value.trim();
  if (!raw || raw === '-') return;
  threshEl.value = String(getRecorderThreshold());
}

function getRecorderScrollContainer(): HTMLElement | null {
  return document.getElementById('recorder-table-scroll') as HTMLElement | null;
}

function touchRecorderPage(pageIndex: number) {
  recorderPageUseOrder = recorderPageUseOrder.filter(value => value !== pageIndex);
  recorderPageUseOrder.push(pageIndex);
  while (recorderPageUseOrder.length > RECORDER_CACHE_PAGE_LIMIT) {
    const evicted = recorderPageUseOrder.shift();
    if (evicted !== undefined) {
      recorderPageCache.delete(evicted);
    }
  }
}

function clearRecorderPageCache() {
  recorderPageCache = new Map<number, SnifferSessionRecord[]>();
  recorderPageUseOrder = [];
  recorderPagesInFlight.clear();
}

function updateRecorderCountLabel() {
  const countEl = document.getElementById('recorder-count');
  if (countEl) {
    countEl.textContent = `${recorderFilteredCount} shown / ${recorderTotalCount} total`;
  }
  const warnEl = document.getElementById('recorder-size-warn');
  if (!warnEl) return;
  if (recorderStorageBytes >= RECORDER_TEMP_WARN_BYTES) {
    warnEl.style.display = '';
    warnEl.textContent = `Temp store ${(recorderStorageBytes / 1024 / 1024).toFixed(1)} MB`;
  } else {
    warnEl.style.display = 'none';
  }
}

function getRecorderRowAt(index: number): SnifferSessionRecord | null {
  if (index < 0) return null;
  const pageIndex = Math.floor(index / RECORDER_PAGE_SIZE);
  const page = recorderPageCache.get(pageIndex);
  if (!page) return null;
  return page[index - (pageIndex * RECORDER_PAGE_SIZE)] || null;
}

function createRecorderSpacerRow(height: number): HTMLTableRowElement {
  const tr = document.createElement('tr');
  const td = document.createElement('td');
  td.colSpan = 7;
  td.style.height = `${Math.max(0, Math.round(height))}px`;
  td.style.padding = '0';
  td.style.border = '0';
  tr.appendChild(td);
  return tr;
}

function renderRecorderViewport() {
  const tbody = document.querySelector('#recorder-table tbody') as HTMLElement | null;
  const container = getRecorderScrollContainer();
  if (!tbody || !container) return;

  tbody.innerHTML = '';
  updateRecorderCountLabel();

  if (recorderFilteredCount === 0) {
    const emptyRow = document.createElement('tr');
    const emptyCell = document.createElement('td');
    emptyCell.colSpan = 7;
    emptyCell.style.padding = '12px';
    emptyCell.style.color = 'var(--muted)';
    const minRssi = getRecorderThreshold();
    emptyCell.textContent = recorderTotalCount === 0
      ? 'No packets captured yet.'
      : `No packets match the current RSSI filter (RSSI >= ${minRssi} dBm). Lower the threshold to show weaker packets.`;
    emptyRow.appendChild(emptyCell);
    tbody.appendChild(emptyRow);
    return;
  }

  const viewportHeight = Math.max(container.clientHeight, RECORDER_ROW_HEIGHT * 8);
  const startIndex = Math.max(0, Math.floor(container.scrollTop / RECORDER_ROW_HEIGHT) - RECORDER_OVERSCAN_ROWS);
  const endIndex = Math.min(
    recorderFilteredCount,
    Math.ceil((container.scrollTop + viewportHeight) / RECORDER_ROW_HEIGHT) + RECORDER_OVERSCAN_ROWS,
  );

  if (startIndex > 0) {
    tbody.appendChild(createRecorderSpacerRow(startIndex * RECORDER_ROW_HEIGHT));
  }

  for (let index = startIndex; index < endIndex; index++) {
    const rec = getRecorderRowAt(index);
    const tr = document.createElement('tr');
    tr.style.height = `${RECORDER_ROW_HEIGHT}px`;

    if (!rec) {
      const loadingCell = document.createElement('td');
      loadingCell.colSpan = 7;
      loadingCell.style.padding = '6px 8px';
      loadingCell.style.color = 'var(--muted)';
      loadingCell.textContent = 'Loading...';
      tr.appendChild(loadingCell);
      tbody.appendChild(tr);
      continue;
    }

    const payload = getRecorderPayload(rec);
    const timestamp = new Date(getRecorderTimestamp(rec)).toLocaleTimeString();
    const dataShort = payload.length > 24 ? `${payload.substring(0, 24)}…` : payload;
    tr.dataset.snifferRowId = String(rec.id);
    tr.classList.toggle('selected', selectedRecorderId === rec.id);

    [
      timestamp,
      String(rec.freq || ''),
      String(rec.mod_name || ''),
      String(rec.rssi ?? ''),
      String(rec.len ?? ''),
      dataShort,
    ].forEach((value, cellIndex) => {
      const td = document.createElement('td');
      td.textContent = value;
      if (cellIndex === 5) {
        td.title = payload;
        td.style.fontFamily = 'monospace';
        td.style.fontSize = '10px';
        td.style.maxWidth = '120px';
        td.style.overflow = 'hidden';
        td.style.textOverflow = 'ellipsis';
        td.style.whiteSpace = 'nowrap';
      }
      tr.appendChild(td);
    });

    const actionTd = document.createElement('td');
    const saveBtn = document.createElement('button');
    saveBtn.className = 'btn secondary';
    saveBtn.style.fontSize = '9px';
    saveBtn.style.padding = '2px 4px';
    saveBtn.dataset.snifferSaveId = String(rec.id);
    saveBtn.textContent = '+DB';
    actionTd.appendChild(saveBtn);
    tr.appendChild(actionTd);
    tbody.appendChild(tr);
  }

  if (endIndex < recorderFilteredCount) {
    tbody.appendChild(createRecorderSpacerRow((recorderFilteredCount - endIndex) * RECORDER_ROW_HEIGHT));
  }
}

async function loadRecorderPage(pageIndex: number, minRssi: number, generation: number) {
  const requestKey = `${generation}:${pageIndex}`;
  if (pageIndex < 0 || recorderPagesInFlight.has(requestKey)) return;
  recorderPagesInFlight.add(requestKey);
  try {
    const page = await invoke<SnifferSessionPage>('sniffer_session_page', {
      offset: pageIndex * RECORDER_PAGE_SIZE,
      limit: RECORDER_PAGE_SIZE,
      minRssi,
    });
    if (generation !== recorderQueryGeneration || minRssi !== recorderLastThreshold) {
      return;
    }
    recorderPageCache.set(pageIndex, page.rows);
    touchRecorderPage(pageIndex);
    recorderTotalCount = page.total_count;
    recorderFilteredCount = page.filtered_count;
    recorderStorageBytes = page.storage_bytes;
  } catch (err) {
    error(`[sniffer] page load failed: ${String(err)}`);
    appendLog(`[sniffer] Failed to load packet page: ${String(err)}`);
  } finally {
    recorderPagesInFlight.delete(requestKey);
  }
}

async function ensureRecorderPagesForViewport() {
  const container = getRecorderScrollContainer();
  if (!container) return;

  const minRssi = getRecorderThreshold();
  if (recorderLastThreshold !== minRssi) {
    recorderLastThreshold = minRssi;
    recorderQueryGeneration += 1;
    clearRecorderPageCache();
    recorderFilteredCount = 0;
    container.scrollTop = 0;
  }
  const generation = recorderQueryGeneration;

  const viewportHeight = Math.max(container.clientHeight, RECORDER_ROW_HEIGHT * 8);
  const startIndex = Math.max(0, Math.floor(container.scrollTop / RECORDER_ROW_HEIGHT) - RECORDER_OVERSCAN_ROWS);
  const endIndex = Math.max(
    RECORDER_PAGE_SIZE,
    Math.ceil((container.scrollTop + viewportHeight) / RECORDER_ROW_HEIGHT) + RECORDER_OVERSCAN_ROWS,
  );
  const firstPage = Math.floor(startIndex / RECORDER_PAGE_SIZE);
  const lastPage = Math.floor(Math.max(0, endIndex - 1) / RECORDER_PAGE_SIZE);
  const loads: Promise<void>[] = [];

  for (let pageIndex = firstPage; pageIndex <= lastPage; pageIndex++) {
    if (!recorderPageCache.has(pageIndex)) {
      loads.push(loadRecorderPage(pageIndex, minRssi, generation));
    } else {
      touchRecorderPage(pageIndex);
    }
  }

  if (loads.length > 0) {
    await Promise.all(loads);
  } else if (recorderPageCache.size === 0) {
    await loadRecorderPage(0, minRssi, generation);
  }
}

function queueRecorderViewportRefresh() {
  if (recorderViewportRefreshQueued) return;
  recorderViewportRefreshQueued = true;
  requestAnimationFrame(() => {
    recorderViewportRefreshQueued = false;
    void refreshRecorderDisplay();
  });
}

async function refreshRecorderDisplay(forceReset = false) {
  const container = getRecorderScrollContainer();
  if (forceReset) {
    recorderQueryGeneration += 1;
    clearRecorderPageCache();
    if (container) container.scrollTop = 0;
  }
  renderRecorderViewport();
  await ensureRecorderPagesForViewport();
  renderRecorderViewport();
}

async function loadSelectedSnifferPacket(id: number | null) {
  selectedRecorderId = id;
  if (id === null) {
    selectedRecorderRecord = null;
    renderSelectedSnifferPacket();
    renderRecorderViewport();
    return;
  }

  selectedRecorderRecord = Array.from(recorderPageCache.values())
    .flat()
    .find(record => record.id === id) || null;

  if (!selectedRecorderRecord) {
    try {
      selectedRecorderRecord = await invoke<SnifferSessionRecord | null>('sniffer_session_get', { id });
    } catch (err) {
      error(`[sniffer] failed to load selected packet: ${String(err)}`);
      appendLog(`[sniffer] Failed to inspect packet: ${String(err)}`);
      selectedRecorderRecord = null;
    }
  }

  renderSelectedSnifferPacket();
  renderRecorderViewport();
}

function toSavedSnifferRecord(rec: SnifferSessionRecord) {
  return {
    timestamp_ms: rec.timestamp_ms,
    freq: rec.freq,
    mod: rec.mod_name,
    rssi: rec.rssi,
    len: rec.len,
    data: rec.data,
    module: rec.module,
  };
}

async function flushRecorderEntries() {
  if (recorderFlushTimer !== null) {
    window.clearTimeout(recorderFlushTimer);
    recorderFlushTimer = null;
  }
  if (recorderPendingEntries.length === 0) return;
  if (recorderFlushPromise) {
    await recorderFlushPromise;
    return;
  }

  const batch = recorderPendingEntries.splice(0, recorderPendingEntries.length);
  recorderFlushPromise = (async () => {
    try {
      const stats = await invoke<SnifferSessionStats>('sniffer_session_append', { entries: batch });
      recorderTotalCount = stats.total_count;
      recorderStorageBytes = stats.storage_bytes;
      queueRecorderViewportRefresh();
    } catch (err) {
      error(`[sniffer] append failed: ${String(err)}`);
      appendLog(`[sniffer] Failed to store packets: ${String(err)}`);
      recorderPendingEntries.unshift(...batch);
    } finally {
      recorderFlushPromise = null;
      if (recorderPendingEntries.length > 0) {
        void flushRecorderEntries();
      }
    }
  })();

  await recorderFlushPromise;
}

function scheduleRecorderFlush() {
  if (recorderPendingEntries.length >= 25) {
    void flushRecorderEntries();
    return;
  }
  if (recorderFlushTimer !== null) return;
  recorderFlushTimer = window.setTimeout(() => {
    recorderFlushTimer = null;
    void flushRecorderEntries();
  }, 200);
}

async function clearRecorderSession() {
  if (recorderFlushTimer !== null) {
    window.clearTimeout(recorderFlushTimer);
    recorderFlushTimer = null;
  }
  recorderPendingEntries = [];
  await invoke('sniffer_session_clear');
  clearRecorderPageCache();
  recorderTotalCount = 0;
  recorderFilteredCount = 0;
  recorderStorageBytes = 0;
  recorderLastThreshold = Number.NaN;
  await loadSelectedSnifferPacket(null);
  await refreshRecorderDisplay(true);
}

async function renderSelectedSnifferPacket() {
  const summary = document.getElementById('sniffer-detail-summary');
  const freq = document.getElementById('sniffer-detail-freq');
  const mod = document.getElementById('sniffer-detail-mod');
  const rssi = document.getElementById('sniffer-detail-rssi');
  const len = document.getElementById('sniffer-detail-len');
  const time = document.getElementById('sniffer-detail-time');
  const output = document.getElementById('sniffer-detail-output') as HTMLPreElement | null;
  document.querySelectorAll<HTMLElement>('[data-sniffer-view-mode]').forEach(btn => {
    btn.classList.toggle('selected', btn.dataset.snifferViewMode === snifferViewMode);
  });

  if (!summary || !freq || !mod || !rssi || !len || !time || !output) return;

  if (!selectedRecorderRecord) {
    summary.textContent = 'Tap a received transmission on the right to inspect it here.';
    freq.textContent = '-';
    mod.textContent = '-';
    rssi.textContent = '-';
    len.textContent = '-';
    time.textContent = '-';
    output.textContent = 'Tap a received transmission on the right to inspect it here.';
    return;
  }

  const payload = getRecorderPayload(selectedRecorderRecord);
  summary.textContent = `Viewing packet ${selectedRecorderRecord.id} in ${snifferViewMode.toUpperCase()} mode.`;
  freq.textContent = `${selectedRecorderRecord.freq || '-'} MHz`;
  mod.textContent = selectedRecorderRecord.mod_name || '-';
  rssi.textContent = `${selectedRecorderRecord.rssi ?? '-'} dBm`;
  len.textContent = String(selectedRecorderRecord.len ?? (payload ? Math.ceil(payload.length / 2) : '-'));
  time.textContent = new Date(getRecorderTimestamp(selectedRecorderRecord)).toLocaleString();
  output.textContent = formatSnifferPayload(payload, snifferViewMode);
}

async function saveRecorderCsv() {
  try {
    await flushRecorderEntries();
    const filePath = await save({
      defaultPath: `sharkos_sniffer_${Date.now()}.csv`,
      filters: [{ name: 'CSV', extensions: ['csv'] }],
    });
    if (!filePath) return;

    const minRssi = getRecorderThreshold();
    const rows: SnifferSessionRecord[] = [];

    for (let offset = 0; ; offset += RECORDER_SAVE_PAGE_SIZE) {
      const page = await invoke<SnifferSessionPage>('sniffer_session_page', {
        offset,
        limit: RECORDER_SAVE_PAGE_SIZE,
        minRssi,
      });
      if (page.rows.length === 0) {
        recorderTotalCount = page.total_count;
        recorderFilteredCount = page.filtered_count;
        recorderStorageBytes = page.storage_bytes;
        break;
      }
      rows.push(...page.rows);
      recorderTotalCount = page.total_count;
      recorderFilteredCount = page.filtered_count;
      recorderStorageBytes = page.storage_bytes;
      if (rows.length >= page.filtered_count) {
        break;
      }
    }

    await writeTextFile(filePath, buildRecorderCsv(rows));
    appendLog(`[sniffer] Saved ${rows.length} packets to ${filePath}`);
    updateRecorderCountLabel();
  } catch (err) {
    error(`saveRecorderCsv failed: ${String(err)}`);
    appendLog(`[sniffer] CSV save failed: ${String(err)}`);
  }
}

async function saveAllToDb() {
  try {
    await flushRecorderEntries();
    const stats = await invoke<SnifferSessionStats>('sniffer_session_stats');
    if (stats.total_count === 0) return;

    const db = snifferDbLoad();
    for (let offset = 0; offset < stats.total_count; offset += RECORDER_SAVE_PAGE_SIZE) {
      const page = await invoke<SnifferSessionPage>('sniffer_session_page', {
        offset,
        limit: RECORDER_SAVE_PAGE_SIZE,
        minRssi: -200,
      });
      page.rows.forEach(rec => db.push(toSavedSnifferRecord(rec)));
    }
    snifferDbSave(db);
    checkSnifferDbSize();
    appendLog(`[sniffer] Saved ${stats.total_count} packets to DB (total: ${db.length})`);
  } catch (err) {
    error(`[sniffer] saveAllToDb failed: ${String(err)}`);
    appendLog(`[sniffer] Failed to save packets to DB: ${String(err)}`);
  }
}

async function saveSingleToDb(id: number) {
  if (id < 0) return;
  try {
    const rec = (Array.from(recorderPageCache.values()).flat().find(row => row.id === id))
      || await invoke<SnifferSessionRecord | null>('sniffer_session_get', { id });
    if (!rec) return;
    snifferDbAdd(toSavedSnifferRecord(rec));
    checkSnifferDbSize();
    appendLog(`[sniffer] Saved signal at ${rec.freq}MHz to DB`);
  } catch (err) {
    error(`[sniffer] saveSingleToDb failed: ${String(err)}`);
    appendLog(`[sniffer] Failed to save packet to DB: ${String(err)}`);
  }
}

function updatePlaybackDisplay() {
  const tbody = document.querySelector('#playback-table tbody') as HTMLElement | null;
  if (!tbody) return;
  const db = snifferDbLoad();
  tbody.innerHTML = '';
  const countEl = document.getElementById('playback-count');
  if (countEl) countEl.textContent = `${db.length} signals`;
  // Show most recent first, limit to 500 for performance
  const start = Math.max(0, db.length - 500);
  for (let i = db.length - 1; i >= start; i--) {
    const rec = db[i];
    const tr = document.createElement('tr');
    const d = new Date(rec.timestamp_ms || Date.now()).toLocaleTimeString();
    const dataStr = rec.data || '';
    const dataShort = dataStr.length > 20 ? dataStr.substring(0, 20) + '…' : dataStr;
    tr.innerHTML = `<td>${d}</td><td>${rec.freq||''}</td><td>${rec.mod||''}</td><td>${rec.rssi||''}</td><td>${rec.len||''}</td><td title="${dataStr}" style="font-family:monospace;font-size:10px;max-width:100px;overflow:hidden;text-overflow:ellipsis;white-space:nowrap">${dataShort}</td><td><button class="btn" style="font-size:9px;padding:2px 6px" data-replay-idx="${i}">TX</button> <button class="btn secondary" style="font-size:9px;padding:2px 4px" data-delete-idx="${i}">✕</button></td>`;
    tbody.appendChild(tr);
  }
}
async function exportPlaybackCsv() {
  const db = snifferDbLoad();
  if (db.length === 0) return;
  try {
    await saveTextAsCsv(buildRecorderCsv(db), `sharkos_sniffer_db_${Date.now()}.csv`);
  } catch (err) {
    error(`exportPlaybackCsv failed: ${String(err)}`);
    appendLog(`[playback] CSV export failed: ${String(err)}`);
  }
}

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
      if (action !== 'sub-ghz-scanner' && action !== 'sub-ghz-recorder') return;

      const macaddy = (loadSavedBTDevice()?.mac) || '';
      const in1 = document.getElementById('subghzFreqInput1') as HTMLInputElement | null;
      const in2 = document.getElementById('subghzFreqInput2') as HTMLInputElement | null;
      const mod1el = document.getElementById('subghzModSelect1') as HTMLSelectElement | null;
      const mod2el = document.getElementById('subghzModSelect2') as HTMLSelectElement | null;

      // Read values (MHz) or fall back to sensible defaults
      // in1 = subghzFreqInput1 = HIGH (433 default), in2 = subghzFreqInput2 = LOW (400 default)
      let f1 = in1 ? Number(in1.value || in1.defaultValue || '433') : NaN;
      let f2 = in2 ? Number(in2.value || in2.defaultValue || '400') : NaN;
      if (isNaN(f1)) f1 = 433;
      if (isNaN(f2)) f2 = 400;

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
        // f1=High, f2=Low; adjust the low end (f2) to keep span within limit
        if (f1 > f2) {
          f2 = f1 - MAX_SPAN_MHZ;
          if (in2) in2.value = String(Math.round(f2*10)/10);
        } else {
          // f2 is somehow higher than f1 (shouldn't happen but be safe)
          f1 = f2 + MAX_SPAN_MHZ;
          if (in1) in1.value = String(Math.round(f1*10)/10);
        }
      }

      if (action === 'sub-ghz-scanner') {
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
      } else if (action === 'sub-ghz-recorder') {
        // ── Packet Sniffer start/stop ──
        if (isPlaying) {
          const sniffFreqLow = parseFloat((document.getElementById('sniffer-freq-low') as HTMLInputElement)?.value || '400');
          const sniffFreqHigh = parseFloat((document.getElementById('sniffer-freq-high') as HTMLInputElement)?.value || '433');
          const sniffMod = (document.getElementById('sniffer-mod') as HTMLSelectElement)?.value || 'OOK';
          const sniffThresh = getRecorderThreshold();
          const sniffParams = {
            top_frequency_mhz: Math.max(sniffFreqLow, sniffFreqHigh),
            bottom_frequency_mhz: Math.min(sniffFreqLow, sniffFreqHigh),
            modulation: sniffMod,
            modulation_one: sniffMod,
            rssi_threshold: sniffThresh
          };
          await invoke<string>('run_action', { action: 'subghz.record.start', macaddy, params: JSON.stringify(sniffParams) });
          appendLog(`[sniffer] start -> ${sniffParams.bottom_frequency_mhz}-${sniffParams.top_frequency_mhz}MHz mod=${sniffMod} rssi>=${sniffThresh}`);
        } else {
          await invoke<string>('run_action', { action: 'sub-ghz-recorder-stop', macaddy, params: JSON.stringify({}) });
          await flushRecorderEntries();
          appendLog('[sniffer] stop');
          await refreshRecorderDisplay();
        }
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
  // count incoming packets and throttle chart redraws
  let packetCounter = 0;
  let channelUpdateScheduled = false;
  let freqRestartTimer: number | null = null;
  setInterval(() => {
    if (packetCounter > 0) {
      console.debug(`[radio] packets last 10s: ${packetCounter}`);
      packetCounter = 0;
    }
  }, 10000);

  // orient app to landscape (either primary or secondary).
  const screenOrientation = screen.orientation as ScreenOrientation & {
    lock?: (orientation: 'any' | 'natural' | 'landscape' | 'portrait' | 'portrait-primary' | 'portrait-secondary' | 'landscape-primary' | 'landscape-secondary') => Promise<void>;
  };
  if (screenOrientation?.lock) {
    screenOrientation.lock('landscape').catch((e: unknown) => {
      info('orientation lock failed: ' + String(e));
    });
  }
  // ensure we have file read/write permissions on Android before using storage
  async function ensureFilePermissions(): Promise<boolean> {
    const androidBridge = window.AndroidBridge;
    if (!androidBridge) {
      return true;
    }
    const perms = ['android.permission.WRITE_EXTERNAL_STORAGE','android.permission.READ_EXTERNAL_STORAGE'];
    const have = androidBridge.hasPermissions(JSON.stringify(perms));
    if (have) return true;
    return new Promise(resolve => {
      const handler = (ev: Event) => {
        window.removeEventListener('permissions-result', handler);
        const ok = androidBridge.hasPermissions(JSON.stringify(perms));
        resolve(ok);
      };
      window.addEventListener('permissions-result', handler);
      androidBridge.requestPermissions(JSON.stringify(perms));
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
    // Let Chart.js responsive mode size the canvas to its container
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
  // (Legacy +/- buttons removed; frequency adjustment is via the rotary dial.)
  const hdrControls = document.getElementById('subghz-header-controls') as HTMLElement | null;

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

    // If this action maps to a *submenu* (id ends with '-menu' or is our
    // special algorithms panel), just show it.
    if (map && map.view && (map.view.endsWith('-menu') || map.view === 'sub-ghz-algorithms' || map.view === 'sub-ghz-disruptor' || map.view === 'sub-ghz-smart-disruptor' || map.view === 'wifi-wireshark' || map.view === 'wifi-https-cracker' || map.view === 'wifi-wpa-cracker')) {
      if (replaceHistory) history.replaceState({view: map.view}, '', '#'+action);
      else history.pushState({view: map.view}, '', '#'+action);
      showView(map.view);
      if (map.chart) showChart(map.chart as any);
      appendLog(`Opened submenu: ${action}`);
      enableHeaderControls(true);

      // Lazy-init the smart disruptor chart when its section becomes visible
      if (map.view === 'sub-ghz-smart-disruptor') {
        setTimeout(() => ensureSmartDisruptorChart(), 80);
      }

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

    // Check for algorithm actions first (handled locally via Rust command).
    if (action.startsWith('algo-')) {
      try {
        const result = await invoke<string>('run_algorithm', { name: action, params: null });
        appendLog(`algorithm ${action} => ${result}`);
      } catch (e) {
        appendLog(`algorithm ${action} error: ${String(e)}`);
      }
      // show logs so the result is visible
      showView('chart-screen');
      showChart('logs');
      enableHeaderControls(false);
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
      if (action === 'sub-ghz-recorder') {
        await clearRecorderSession();
      }
      if (action === 'sub-ghz-playback') {
         updatePlaybackDisplay();
         checkSnifferDbSize();
      }

      // Sub-GHz scanner and recorder are controlled by Play/Stop buttons,
      // so opening these views should not send a generic run_action command.
      if (action !== 'sub-ghz-scanner' && action !== 'sub-ghz-recorder') {
        const result = await invoke<string>('run_action', { action, macaddy, params: params ? JSON.stringify(params) : null });
        appendLog(`run_action(${action}, ${macaddy}) => ${result}`);
        if (action === 'subghz-test') {
          showSubghzTestResult(result);
        }
        if (action === 'device-status') {
          // GATT ack only — real JSON comes via BLE notification (handleRadioSignalData)
          const statusContent = document.getElementById('status-content');
          if (statusContent) {
            statusContent.innerHTML = '<p style="color:var(--muted);text-align:center;padding:20px">Waiting for device response…</p>';
          }
          showView('status-screen');
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
    back?.addEventListener('click', async () => {
      // if we are exiting wifi channel scan, request stop command
      if (currentAction === 'wifi-channel-scan') {
        try {
          const saved = loadSavedBTDevice();
          const macaddy = (saved && saved.mac) ? saved.mac : "";
          await invoke<string>('run_action', { action: 'wifi.scan.stop', macaddy, params: null });
          appendLog('wifi-channel-scan: sent stop command');
        } catch (e) {
          appendLog('wifi-channel-scan stop error: ' + String(e));
        }
      }
      // if we are exiting disruptor, send stop command
      if (currentAction === 'sub-ghz-disruptor' || currentAction === 'sub-ghz-smart-disruptor') {
        try {
          const saved = loadSavedBTDevice();
          const macaddy = (saved && saved.mac) ? saved.mac : "";
          const stopCmd = currentAction === 'sub-ghz-smart-disruptor' ? 'subghz.smart.disruptor.stop' : 'subghz.disruptor.stop';
          await invoke<string>('run_action', { action: stopCmd, macaddy, params: null });
          disruptorRadio1Active = false;
          disruptorRadio2Active = false;
          updateDisruptorBubbles();
          updateSmartDisruptorBubble();
          appendLog('disruptor: sent stop command');
        } catch (e) {
          appendLog('disruptor stop error: ' + String(e));
        }
      }
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

  // wire sub‑GHz input controls (no +/- buttons; frequency is adjusted via the rotary dial)
  const subFreqInput1 = document.getElementById('subghzFreqInput1') as HTMLInputElement | null;
  const subFreqInput2 = document.getElementById('subghzFreqInput2') as HTMLInputElement | null;
  // Keyboard arrow keys on frequency inputs still step the value
  subFreqInput1?.addEventListener('keydown', (ev) => {
    if (ev.key === 'ArrowUp') { ev.preventDefault(); subFreqInput1.stepUp(); subFreqInput1.dispatchEvent(new Event('change')); }
    if (ev.key === 'ArrowDown') { ev.preventDefault(); subFreqInput1.stepDown(); subFreqInput1.dispatchEvent(new Event('change')); }
  });
  subFreqInput2?.addEventListener('keydown', (ev) => {
    if (ev.key === 'ArrowUp') { ev.preventDefault(); subFreqInput2.stepUp(); subFreqInput2.dispatchEvent(new Event('change')); }
    if (ev.key === 'ArrowDown') { ev.preventDefault(); subFreqInput2.stepDown(); subFreqInput2.dispatchEvent(new Event('change')); }
  });

  // 3. Frequency Coupling Logic (enforce max 33 MHz diff)
  // Only runs on 'change' (blur / Enter) so we don't spam BLE mid-edit.
  // NOTE: Input1 = HIGH (top), Input2 = LOW (bottom)
  const enforceFreqCoupling = (changed: '1'|'2') => {
    if (!subFreqInput1 || !subFreqInput2) return;
    let hi = parseFloat(subFreqInput1.value); // Input1 = High
    let lo = parseFloat(subFreqInput2.value); // Input2 = Low
    if (isNaN(hi) || isNaN(lo)) return;

    // Enforce: High must always be >= Low
    if (hi < lo) {
      const tmp = hi; hi = lo; lo = tmp;
      subFreqInput1.value = hi.toFixed(1);
      subFreqInput2.value = lo.toFixed(1);
    }

    // Enforce max 33 MHz span
    const MAX_DIFF = 33;
    if (hi - lo > MAX_DIFF) {
      if (changed === '1') {
         // High changed — pull Low up
         lo = hi - MAX_DIFF;
         subFreqInput2.value = lo.toFixed(1);
      } else {
         // Low changed — pull High down
         hi = lo + MAX_DIFF;
         subFreqInput1.value = hi.toFixed(1);
      }
    }

    // Update chart range (lo → hi)
    setChartRange('channels', Math.floor(lo), Math.ceil(hi));
    configureChannelsChartForAction('sub-ghz-scanner');
    if (channelsChart && !channelUpdateScheduled) {
       channelUpdateScheduled = true;
       setTimeout(() => {
          channelsChart?.update();
          channelUpdateScheduled = false;
       }, 100);
    }

    // Restart scan (debounced) so device picks up new range
    if (isPlaying && currentAction === 'sub-ghz-scanner') {
       if (freqRestartTimer) clearTimeout(freqRestartTimer);
       freqRestartTimer = setTimeout(() => { setPlaying(false); setTimeout(() => setPlaying(true), 400); }, 500);
    }
  };
  // 'change' fires on blur / Enter — NOT on every keystroke
  if (subFreqInput1) subFreqInput1.addEventListener('change', () => enforceFreqCoupling('1'));
  if (subFreqInput2) subFreqInput2.addEventListener('change', () => enforceFreqCoupling('2'));

  // --- Spectrum Analyzer Rotary Dial (left side of channels chart) ---
  // A smooth draggable/scrollable rotary dial that shifts BOTH high and low
  // frequency at the same rate (±1 MHz per 30° of rotation).
  const dialWheel = document.getElementById('spectrumDialWheel') as HTMLElement | null;
  const dialHighVal = document.getElementById('dialHighVal')  as HTMLElement | null;
  const dialLowVal  = document.getElementById('dialLowVal')   as HTMLElement | null;
  const dialSvg     = document.getElementById('rotaryDial')    as SVGElement | null;
  const dialIndicator = document.getElementById('dialIndicator') as SVGElement | null;
  const DIAL_STEP = 1.0; // MHz per notch
  const DIAL_DEG_PER_STEP = 60; // degrees of rotation per 1 MHz step (less sensitive)
  let dialAngle = 0;      // absolute cumulative angle (volume-knob, never resets)
  let dialAccum = 0;      // sub-step accumulator in degrees
  let dialDragging = false;
  let dialLastY = 0;

  function syncDialToInputs() {
    if (dialHighVal && subFreqInput1) dialHighVal.textContent = parseFloat(subFreqInput1.value).toFixed(1);
    if (dialLowVal  && subFreqInput2) dialLowVal.textContent  = parseFloat(subFreqInput2.value).toFixed(1);
  }

  // Shift BOTH high and low by the same delta so the window slides uniformly
  function dialShiftBoth(deltaMHz: number) {
    if (!subFreqInput1 || !subFreqInput2) return;
    let hi = parseFloat(subFreqInput1.value) || 433;
    let lo = parseFloat(subFreqInput2.value) || 400;
    hi = Math.max(300, Math.min(1000, hi + deltaMHz));
    lo = Math.max(300, Math.min(1000, lo + deltaMHz));
    subFreqInput1.value = hi.toFixed(1);
    subFreqInput2.value = lo.toFixed(1);
    subFreqInput1.dispatchEvent(new Event('change'));
    syncDialToInputs();
  }

  function updateDialVisual() {
    if (dialIndicator) {
      // Use absolute angle (volume-knob style — never resets to center)
      const vis = ((dialAngle % 360) + 360) % 360;
      dialIndicator.setAttribute('transform', `rotate(${vis},40,40)`);
    }
  }

  // Mouse / touch drag on the SVG dial
  function dialPointerDown(ev: PointerEvent) {
    dialDragging = true;
    dialLastY = ev.clientY;
    if (dialSvg) dialSvg.style.cursor = 'grabbing';
    (ev.target as Element)?.setPointerCapture?.(ev.pointerId);
    ev.preventDefault();
  }
  function dialPointerMove(ev: PointerEvent) {
    if (!dialDragging) return;
    const dy = dialLastY - ev.clientY; // positive = drag up = increase freq
    dialLastY = ev.clientY;
    const delta = dy * 0.6; // 0.6° per pixel (much less sensitive than old 2°/px)
    dialAngle += delta;
    dialAccum += delta;
    updateDialVisual();
    // Accumulate rotation and emit a step every DIAL_DEG_PER_STEP degrees
    const steps = Math.trunc(dialAccum / DIAL_DEG_PER_STEP);
    if (steps !== 0) {
      dialAccum -= steps * DIAL_DEG_PER_STEP;
      dialShiftBoth(steps * DIAL_STEP);
    }
    ev.preventDefault();
  }
  function dialPointerUp(_ev: PointerEvent) {
    dialDragging = false;
    if (dialSvg) dialSvg.style.cursor = 'grab';
  }

  dialSvg?.addEventListener('pointerdown', dialPointerDown);
  window.addEventListener('pointermove', dialPointerMove);
  window.addEventListener('pointerup', dialPointerUp);

  // Scroll wheel on the dial area shifts both freqs
  dialWheel?.addEventListener('wheel', (ev) => {
    ev.preventDefault();
    const dir = (ev.deltaY < 0 ? 1 : -1);
    dialAngle += dir * DIAL_DEG_PER_STEP;
    updateDialVisual();
    dialShiftBoth(dir * DIAL_STEP);
  }, { passive: false });

  // Keep dial in sync when header inputs change externally
  if (subFreqInput1) subFreqInput1.addEventListener('change', syncDialToInputs);
  if (subFreqInput2) subFreqInput2.addEventListener('change', syncDialToInputs);

  // Toggle dial visibility alongside header controls
  setInterval(() => {
    if (!dialWheel) return;
    dialWheel.style.display = (currentView === 'chart-screen' && currentAction === 'sub-ghz-scanner') ? 'flex' : 'none';
  }, 200);

  // initial sync
  syncDialToInputs();

  // modality change listener for LoRa enforcement & defaults
  const mod1el = document.getElementById('subghzModSelect1') as HTMLSelectElement | null;
  const mod2el = document.getElementById('subghzModSelect2') as HTMLSelectElement | null;
  // Default modulation profile for scanner start: radio-1 OOK, radio-2 2-FSK.
  if (mod1el && !mod1el.value) mod1el.value = 'OOK';
  if (mod2el && mod2el.value === 'OOK') mod2el.value = '2-FSK';
  // helper that also notifies the device of changes via BLE
  async function notifyModulationChange(idx: number, value: string) {
    const macaddy = loadSavedBTDevice()?.mac || '';
    if (!macaddy) return;

    const wasScanning = isPlaying && currentAction === 'sub-ghz-scanner';

    // If we were scanning, pause so the radio isn't mid-sweep when we change mod.
    if (wasScanning) {
      appendLog('[sub-ghz] pausing scan to change modulation');
      setPlaying(false);
      // give device time to settle
      await new Promise(r => setTimeout(r, 700));
    }

    // Send the modulation change command
    const action = idx === 1 ? 'subghz.set.mod.one' : 'subghz.set.mod.two';
    try {
      await invoke<string>('run_action', { action, macaddy, params: JSON.stringify({ modulation: value }) });
      appendLog(`[sub-ghz] sent ${action} modulation=${value}`);
    } catch (e) {
      error(`[sub-ghz] failed to send ${action}: ${String(e)}`);
    }

    // If we paused earlier, resume the scan now that modulation change is done
    if (wasScanning) {
      appendLog('[sub-ghz] resuming scan with updated modulation');
      await new Promise(r => setTimeout(r, 700));
      setPlaying(true);
    }
  }
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
      if (subFreqInput1 && parseFloat(subFreqInput1.value) > 900) subFreqInput1.value = '433.0';
      if (subFreqInput2 && parseFloat(subFreqInput2.value) > 900) subFreqInput2.value = '400.0';
    }
    // Sync the rotary dial display with the updated frequency inputs
    syncDialToInputs();
  }
  mod1el?.addEventListener('change', () => {
    // Only apply LoRa enforcement if explicit LoRa selection
    const val = mod1el?.value || '';
    if (val === 'LoRa') updateModState(); 
    notifyModulationChange(1, val);
  });
  mod2el?.addEventListener('change', () => {
    const val = mod2el?.value || '';
    if (val === 'LoRa') updateModState();
    notifyModulationChange(2, val);
  });
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

    // WiFi capture (Wireshark) data — route to dedicated handler
    let r0 = raw;
    if (typeof r0 === 'string') { try { r0 = JSON.parse(r0); } catch {} }
    if (r0 && r0.wifi_capture) {
      if ((window as any).__handleWifiCapture) (window as any).__handleWifiCapture(r0);
      return;
    }

    // ── Sniffer packet — captured by CC1101/LoRa packet sniffer ──
    if (r0 && r0.sniffer_packet) {
      const sp = r0.sniffer_packet;
      const entry: SnifferSessionPacketInput = {
        timestamp_ms: Date.now(), // Use phone's real time
        freq: sp.freq || 0,
        mod_name: sp.mod || '',
        rssi: sp.rssi || 0,
        len: sp.len || 0,
        data: sp.data || '',
        module: sp.module ?? 0
      };
      if (currentAction === 'sub-ghz-recorder' && isPlaying) {
        recorderPendingEntries.push(entry);
        scheduleRecorderFlush();
      }
      return;
    }

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
          // convert RSSI (-100..0) to positive strength 0..100
          let strength = (s.rssi !== undefined && s.rssi !== null) ? Math.round(100 + s.rssi) : 0;
          if (strength < 0) strength = 0;
          if (strength > 100) strength = 100;
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
              // if decoding fails just keep the raw payload string
              payloadStr = String(s.payload);
            }
          }

          const ssid = `${s.extra || 'Unknown'} ${payloadStr ? '['+payloadStr+']' : ''}`;

          let mod = 'Unknown';
          if (currentAction === 'sub-ghz-scanner') {
             const m1 = (document.getElementById('subghzModSelect1') as HTMLSelectElement)?.value || 'OOK';
             const m2 = (document.getElementById('subghzModSelect2') as HTMLSelectElement)?.value || '2-FSK';
             if (s.module === 0) mod = m1;
             else if (s.module === 1) mod = m2;
          }
          let dataLen = 0;
          if (typeof s.payload === 'string') {
             try { dataLen = atob(s.payload).length; } catch { dataLen = s.payload.length; }
          } else if (Array.isArray(s.payload)) {
             dataLen = s.payload.length;
          }

          console.debug('[chart] updating channel', channel, 'with ssid', ssid, 'strength', strength);

          const isWifiScanSignal = currentAction === 'wifi-channel-scan' || s.module === 4;

          // Wi-Fi scans can produce many APs on the same numeric channel. On a
          // linear bar chart, duplicate x values can collapse bar width to zero.
          // Keep one visible bar per channel using the strongest observed AP.
          const dataArr = ds.data as any[];
          const existingIdx = isWifiScanSignal
            ? dataArr.findIndex(d => d.x === channel)
            : dataArr.findIndex(d => d.x === channel && d.ssid === ssid);
          // choose color based on module (0=CC1101_1 purple,1=CC1101_2 red,2=LORA cyan)
          let barColor = '#DC2626';
          if (s.module === 0) barColor = '#8C00FF';
          else if (s.module === 1) barColor = '#DC2626';
          else if (s.module === 2) barColor = '#00BCD4';

          if (existingIdx >= 0) {
             const existing = dataArr[existingIdx] as any;
             if (isWifiScanSignal) {
               if (strength >= (existing.y ?? 0)) {
                 existing.y = strength;
                 existing.ssid = ssid;
                 existing.module = s.module;
               }
             } else {
               existing.y = strength;
               existing.ssid = ssid;
             }
             existing.modulation = mod;
             existing.dataLen = dataLen;
             // update color as well
             const bc = ds.backgroundColor as any[];
             if (bc && bc.length > existingIdx) bc[existingIdx] = barColor;
          } else {
             dataArr.push({ x: channel, y: strength, ssid: ssid, module: s.module, modulation: mod, dataLen: dataLen });
             // append color slot
             const bc = ds.backgroundColor as any[];
             if (bc) bc.push(barColor);
          }

          // keep reasonable history? No, for bar chart we want current snapshot.
          // Maybe clear old ones?
          // We can remove items that haven't been updated recently if we tracked timestamp.
          // for most scans we never trim; only trim if not doing a sub-ghz sweep so
          // the UI doesn't grow without bound when scanning Wi‑Fi/other large
          // ranges.
          if (!currentAction?.startsWith('sub-ghz') && dataArr.length > 200) {
             // remove oldest entry to limit memory usage
             dataArr.shift();
          }
          if (!channelUpdateScheduled) {
             channelUpdateScheduled = true;
             setTimeout(() => {
               channelsChart?.update();
               channelUpdateScheduled = false;
             }, 100);
          }
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
    // if this is a device status result, route to status display
    if (r && r.device_status) {
      showDeviceStatus(r);
      return;
    }
    // if this is a disruptor status response, update bubbles
    if (r && r.disruptor_status) {
      const ds = r.disruptor_status;
      if (ds.radio1 !== undefined) disruptorRadio1Active = !!ds.radio1;
      if (ds.radio2 !== undefined) disruptorRadio2Active = !!ds.radio2;
      // Check TX verification results (sent after first-pass radio test)
      if (ds.radio1_tx_ok === false || ds.radio2_tx_ok === false) {
        const failedRadios: string[] = [];
        if (ds.radio1_tx_ok === false) failedRadios.push('Radio 1');
        if (ds.radio2_tx_ok === false) failedRadios.push('Radio 2');
        const failMsg = failedRadios.join(' & ') + ' TX verification FAILED — not transmitting';
        appendLog('[Disruptor] ' + failMsg);
        showRadioAlert([failMsg + '. Check SPI wiring and CC1101 modules.']);
        disruptorRadio1Active = false;
        disruptorRadio2Active = false;
      }
      updateDisruptorBubbles();
      return;
    }
    // if this is a smart disruptor status response, update bubble + chart + status text
    if (r && r.smart_disruptor_status) {
      const ds = r.smart_disruptor_status;
      smartDisruptorActive = !!ds.active;
      updateSmartDisruptorBubble();
      // Update the on-screen status line
      const sdStatus = document.getElementById('smart-disruptor-status');
      if (sdStatus) {
        if (!ds.active && ds.tx_verified === false) {
          sdStatus.textContent = 'TX verification FAILED — check radios';
          sdStatus.className = 'error';
        } else if (ds.phase === 'scanning') {
          const det = ds.detected ?? 0;
          sdStatus.textContent = det > 0
            ? `Scanning… (${det} signal${det > 1 ? 's' : ''} found)`
            : 'Scanning — no signals above RSSI floor yet…';
          sdStatus.className = 'info';
        } else if (ds.phase === 'disrupting') {
          const bf = ds.best_freq ? Number(ds.best_freq).toFixed(2) : '?';
          const br = ds.best_rssi ?? '';
          sdStatus.textContent = `Disrupting ${bf} MHz (RSSI ${br})`;
          sdStatus.className = 'info';
        } else if (ds.active) {
          sdStatus.textContent = 'Running…';
          sdStatus.className = 'info';
        }
      }
      // Update spectrum chart if data present
      if (ds.spectrum && Array.isArray(ds.spectrum) && smartDisruptorChart) {
        smartDisruptorChart.data.labels = ds.spectrum.map((p: any) => p.freq?.toFixed(2));
        smartDisruptorChart.data.datasets[0].data = ds.spectrum.map((p: any) => p.rssi);
        smartDisruptorChart.update('none');
      }
      return;
    }
    if (r && r.type === 'radio-batch' && Array.isArray(r.signals)) {
      // Feed smart disruptor chart if active
      if (smartDisruptorActive && smartDisruptorChart) {
        for (const s of r.signals) {
          if (typeof s.frequency_mhz === 'number' && s.frequency_mhz > 0) {
            const freqLabel = s.frequency_mhz.toFixed(2);
            const labels = smartDisruptorChart.data.labels as string[];
            const dataArr = smartDisruptorChart.data.datasets[0].data as number[];
            const bgArr = smartDisruptorChart.data.datasets[0].backgroundColor as string[];
            const bdArr = smartDisruptorChart.data.datasets[0].borderColor as string[];
            const idx = labels.indexOf(freqLabel);
            // Color: module 0 (CC1101_1) = purple (TX), module 1 (CC1101_2) = green (monitor)
            const fillColor = s.module === 0 ? 'rgba(140,0,255,0.7)' : 'rgba(0,255,128,0.5)';
            const edgeColor = s.module === 0 ? '#8c00ff' : '#00ff80';
            if (idx >= 0) {
              dataArr[idx] = s.rssi;
              if (bgArr) bgArr[idx] = fillColor;
              if (bdArr) bdArr[idx] = edgeColor;
            } else {
              // Insert in sorted order by frequency
              let insertAt = labels.length;
              for (let i = 0; i < labels.length; i++) {
                if (parseFloat(labels[i]) > s.frequency_mhz) { insertAt = i; break; }
              }
              labels.splice(insertAt, 0, freqLabel);
              dataArr.splice(insertAt, 0, s.rssi);
              if (bgArr) bgArr.splice(insertAt, 0, fillColor);
              if (bdArr) bdArr.splice(insertAt, 0, edgeColor);
            }
          }
        }
        smartDisruptorChart.update('none');
      }
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
    packetCounter++;
    const p = event.payload;
    if (!isPlaying && !(p && typeof p === 'object' && (p.subghz_test || p.disruptor_status || p.smart_disruptor_status || p.device_status || p.type === 'radio-batch' || p.wifi_capture || p.sniffer_packet)) && !smartDisruptorActive) return;
    try {
      handleRadioSignalData(p);
    } catch (err) { error('radio-signal (tauri) error: '+String(err)); }
  });

  // Listen on window CustomEvents (dispatched by handleRadioNotification in
  // MainActivity.kt via evaluateJavascript). This is the primary path on
  // Android when BLE notifications arrive.
  window.addEventListener('radio-signal', (ev: Event) => {
    packetCounter++;
    const detail = (ev as CustomEvent).detail;
    if (!isPlaying) {
      let parsed = detail;
      if (typeof detail === 'string' && detail.trimStart().startsWith('{')) {
        try { parsed = JSON.parse(detail); } catch {}
      }
      if (!(parsed && typeof parsed === 'object' && (parsed.subghz_test || parsed.disruptor_status || parsed.smart_disruptor_status || parsed.device_status || parsed.type === 'radio-batch' || parsed.wifi_capture || parsed.sniffer_packet)) && !smartDisruptorActive) return;
    }
    try {
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
      channelsChart.data.datasets.forEach(ds => {
        ds.data = [];
        if (Array.isArray(ds.backgroundColor)) (ds.backgroundColor as any[]).length = 0;
      });
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

  // recorder control hooks
  document.getElementById('recorder-thresh')?.addEventListener('input', () => {
    queueRecorderViewportRefresh();
  });
  document.getElementById('recorder-thresh')?.addEventListener('change', () => {
    syncRecorderThresholdInput();
    queueRecorderViewportRefresh();
  });
  document.getElementById('recorder-thresh')?.addEventListener('blur', () => {
    syncRecorderThresholdInput();
    queueRecorderViewportRefresh();
  });
  document.getElementById('recorder-clear')?.addEventListener('click', () => {
    void clearRecorderSession();
  });
  document.getElementById('recorder-save')?.addEventListener('click', () => {
    void saveRecorderCsv();
  });
  document.getElementById('recorder-save-db')?.addEventListener('click', () => {
    void saveAllToDb();
  });
  document.querySelectorAll<HTMLElement>('[data-sniffer-view-mode]').forEach(btn => {
    btn.addEventListener('click', () => {
      const nextMode = btn.dataset.snifferViewMode as SnifferViewMode | undefined;
      if (!nextMode) return;
      snifferViewMode = nextMode;
      void renderSelectedSnifferPacket();
    });
  });

  getRecorderScrollContainer()?.addEventListener('scroll', () => {
    queueRecorderViewportRefresh();
  }, { passive: true });

  // Sniffer modulation auto-adjust frequencies
  document.getElementById('sniffer-mod')?.addEventListener('change', (e) => {
    const mod = (e.target as HTMLSelectElement).value;
    const loEl = document.getElementById('sniffer-freq-low') as HTMLInputElement;
    const hiEl = document.getElementById('sniffer-freq-high') as HTMLInputElement;
    const threshEl = document.getElementById('recorder-thresh') as HTMLInputElement | null;
    if (mod === 'LoRa') {
      if (loEl) loEl.value = '900';
      if (hiEl) hiEl.value = '933';
      if (threshEl) {
        const current = parseInt(threshEl.value, 10);
        if (Number.isNaN(current) || current > -95) {
          threshEl.value = '-100';
          queueRecorderViewportRefresh();
        }
      }
    } else {
      if (loEl && parseFloat(loEl.value) >= 900) loEl.value = '400';
      if (hiEl && parseFloat(hiEl.value) >= 900) hiEl.value = '433';
    }
  });

  // Delegate click for per-row "+DB" buttons in recorder table
  document.getElementById('recorder-table')?.addEventListener('click', (e) => {
    const btn = (e.target as HTMLElement).closest('[data-sniffer-save-id]') as HTMLElement | null;
    if (btn) {
      const id = parseInt(btn.getAttribute('data-sniffer-save-id') || '-1', 10);
      void saveSingleToDb(id);
      btn.textContent = '✓';
      btn.setAttribute('disabled', 'true');
      return;
    }

    const row = (e.target as HTMLElement).closest('tr[data-sniffer-row-id]') as HTMLElement | null;
    if (row) {
      const id = parseInt(row.dataset.snifferRowId || '-1', 10);
      void loadSelectedSnifferPacket(id >= 0 ? id : null);
    }
  });

  // ── Playback controls ──
  document.getElementById('playback-refresh')?.addEventListener('click', () => updatePlaybackDisplay());
  document.getElementById('playback-export-csv')?.addEventListener('click', () => exportPlaybackCsv());
  document.getElementById('playback-clear-db')?.addEventListener('click', () => {
    if (confirm('Clear all saved signals from the database?')) {
      snifferDbClear();
      updatePlaybackDisplay();
      appendLog('[playback] Database cleared');
    }
  });

  // Delegate click for replay (TX) and delete buttons in playback table
  document.getElementById('playback-table')?.addEventListener('click', async (e) => {
    const replayBtn = (e.target as HTMLElement).closest('[data-replay-idx]') as HTMLElement | null;
    const deleteBtn = (e.target as HTMLElement).closest('[data-delete-idx]') as HTMLElement | null;
    const macaddy = (loadSavedBTDevice()?.mac) || '';

    if (replayBtn) {
      const idx = parseInt(replayBtn.getAttribute('data-replay-idx') || '-1');
      const db = snifferDbLoad();
      if (idx >= 0 && idx < db.length) {
        const sig = db[idx];
        const txParams = {
          frequency_mhz: sig.freq,
          modulation: sig.mod,
          data: sig.data || ''
        };
        try {
          replayBtn.textContent = '…';
          await invoke<string>('run_action', { action: 'sub-ghz-packet-sender', macaddy, params: JSON.stringify(txParams) });
          replayBtn.textContent = '✓';
          setTimeout(() => { replayBtn.textContent = 'TX'; }, 1500);
          appendLog(`[playback] TX signal at ${sig.freq}MHz mod=${sig.mod}`);
        } catch (err) {
          replayBtn.textContent = '✕';
          error('[playback] TX failed: ' + String(err));
        }
      }
    }

    if (deleteBtn) {
      const idx = parseInt(deleteBtn.getAttribute('data-delete-idx') || '-1');
      const db = snifferDbLoad();
      if (idx >= 0 && idx < db.length) {
        db.splice(idx, 1);
        snifferDbSave(db);
        updatePlaybackDisplay();
      }
    }
  });

  // --- Disruptor controls wiring ---
  // Helper: parse start/stop freq from a disruptor freq-range dropdown
  function parseDisruptorFreqRange(radioNum: number): { start: number; stop: number } {
    // When LoRa is selected, read from the LoRa freq-range dropdown instead
    const isLoRa = dModSel1?.value === 'LoRa';
    const selId = isLoRa ? `disruptorFreqRangeLoRa${radioNum}` : `disruptorFreqRange${radioNum}`;
    const sel = document.getElementById(selId) as HTMLSelectElement | null;
    const v = sel?.value || (isLoRa ? '902-906' : '431-435');
    if (v === 'custom') {
      const s = parseFloat((document.getElementById(`disruptorStartFreq${radioNum}`) as HTMLInputElement)?.value || '432');
      const e = parseFloat((document.getElementById(`disruptorStopFreq${radioNum}`) as HTMLInputElement)?.value || '436');
      return { start: s, stop: e };
    }
    const parts = v.split('-');
    return { start: parseFloat(parts[0]), stop: parseFloat(parts[1]) };
  }

  // Freq range dropdown custom toggle for both radios (CC1101 + LoRa)
  [1, 2].forEach(n => {
    const sel = document.getElementById(`disruptorFreqRange${n}`) as HTMLSelectElement | null;
    sel?.addEventListener('change', () => {
      const isCustom = sel.value === 'custom';
      const d = isCustom ? '' : 'none';
      [`disruptorStartFreq${n}`, `disruptorFreqSep${n}`, `disruptorStopFreq${n}`, `disruptorFreqUnit${n}`].forEach(id => {
        const el = document.getElementById(id);
        if (el) el.style.display = d;
      });
    });
    const loraSel = document.getElementById(`disruptorFreqRangeLoRa${n}`) as HTMLSelectElement | null;
    loraSel?.addEventListener('change', () => {
      const isCustom = loraSel.value === 'custom';
      const d = isCustom ? '' : 'none';
      [`disruptorStartFreq${n}`, `disruptorFreqSep${n}`, `disruptorStopFreq${n}`, `disruptorFreqUnit${n}`].forEach(id => {
        const el = document.getElementById(id);
        if (el) el.style.display = d;
      });
    });
  });

  // Power selector buttons
  document.querySelectorAll('#power-selector-1 .power-btn').forEach(btn => {
    btn.addEventListener('click', () => {
      document.querySelectorAll('#power-selector-1 .power-btn').forEach(b => b.classList.remove('selected'));
      btn.classList.add('selected');
      disruptorPower1 = parseInt((btn as HTMLElement).dataset.power || '0');
    });
  });
  document.querySelectorAll('#power-selector-2 .power-btn').forEach(btn => {
    btn.addEventListener('click', () => {
      document.querySelectorAll('#power-selector-2 .power-btn').forEach(b => b.classList.remove('selected'));
      btn.classList.add('selected');
      disruptorPower2 = parseInt((btn as HTMLElement).dataset.power || '0');
    });
  });

  // LoRa greying: if radio 1 selects LoRa, grey out radio 2 and switch freq ranges
  const dModSel1 = document.getElementById('disruptorModSelect1') as HTMLSelectElement | null;
  const dModSel2 = document.getElementById('disruptorModSelect2') as HTMLSelectElement | null;
  const dCard2 = document.getElementById('disruptor-card-2') as HTMLElement | null;
  function checkDisruptorLoRa() {
    const isLoRa = dModSel1?.value === 'LoRa';
    if (dCard2) {
      dCard2.classList.toggle('greyed-out', isLoRa);
    }
    // Swap visible freq-range dropdowns between CC1101 ranges and LoRa ranges
    [1, 2].forEach(n => {
      const cc1101Sel = document.getElementById(`disruptorFreqRange${n}`);
      const loraSel = document.getElementById(`disruptorFreqRangeLoRa${n}`);
      if (cc1101Sel) cc1101Sel.style.display = isLoRa ? 'none' : '';
      if (loraSel)   loraSel.style.display   = isLoRa ? ''     : 'none';
    });
  }
  dModSel1?.addEventListener('change', checkDisruptorLoRa);

  // Start disruptor
  document.getElementById('disruptor-start')?.addEventListener('click', async () => {
    const saved = loadSavedBTDevice();
    const macaddy = (saved && saved.mac) ? saved.mac : '';
    const r1range = parseDisruptorFreqRange(1);
    const mod1 = dModSel1?.value || 'OOK';
    const isLoRa = mod1 === 'LoRa';
    const r2range = parseDisruptorFreqRange(2);
    const mod2 = dModSel2?.value || 'OOK';

    const params: any = {
      radio1: { start_freq: r1range.start, stop_freq: r1range.stop, mod: mod1, power: disruptorPower1 },
      radio2: isLoRa ? null : { start_freq: r2range.start, stop_freq: r2range.stop, mod: mod2, power: disruptorPower2 }
    };
    try {
      const result = await invoke<string>('run_action', {
        action: 'subghz.disruptor.start',
        macaddy,
        params: JSON.stringify(params)
      });
      appendLog(`disruptor start => ${result}`);
      updateDisruptorBubbles();
    } catch (e) {
      appendLog(`disruptor start error: ${String(e)}`);
    }
  });

  // Stop disruptor
  document.getElementById('disruptor-stop')?.addEventListener('click', async () => {
    const saved = loadSavedBTDevice();
    const macaddy = (saved && saved.mac) ? saved.mac : '';
    try {
      await invoke<string>('run_action', { action: 'subghz.disruptor.stop', macaddy, params: null });
      disruptorRadio1Active = false;
      disruptorRadio2Active = false;
      updateDisruptorBubbles();
      appendLog('disruptor: stopped');
    } catch (e) {
      appendLog(`disruptor stop error: ${String(e)}`);
    }
  });

  // --- Smart Disruptor controls wiring ---
  // Power selector buttons (Radio 1 only)
  document.querySelectorAll('#smart-power-selector-1 .power-btn').forEach(btn => {
    btn.addEventListener('click', () => {
      document.querySelectorAll('#smart-power-selector-1 .power-btn').forEach(b => b.classList.remove('selected'));
      btn.classList.add('selected');
      smartDisruptorPower1 = parseInt((btn as HTMLElement).dataset.power || '0');
    });
  });

  // Lazy-init smart disruptor spectrum chart (canvas is hidden at boot)
  function ensureSmartDisruptorChart() {
    if (smartDisruptorChart) {
      try { smartDisruptorChart.resize(); } catch(_) {}
      return;
    }
    const sdChartCanvas = document.getElementById('smartDisruptChart') as HTMLCanvasElement | null;
    if (!sdChartCanvas) return;
    try {
      smartDisruptorChart = new (window as any).Chart(sdChartCanvas.getContext('2d'), {
        type: 'bar',
        data: { labels: [] as string[], datasets: [{ label: 'RSSI', data: [] as number[], backgroundColor: [] as string[], borderColor: [] as string[], borderWidth: 1 }] },
        options: {
          responsive: true, maintainAspectRatio: false,
          animation: false,
          scales: { y: { min: -120, max: 0, ticks: { color: '#999' }, grid: { color: 'rgba(255,255,255,0.04)' } }, x: { ticks: { color: '#999', maxRotation: 45, font: { size: 9 } }, grid: { display: false } } },
          plugins: { legend: { display: false } }
        }
      });
    } catch (e) {
      console.warn('smart disruptor chart init failed', e);
    }
  }

  // Helper to update visible smart disruptor status
  function setSmartDisruptorStatus(msg: string, cls: 'info' | 'error' | '' = '') {
    const el = document.getElementById('smart-disruptor-status');
    if (el) { el.textContent = msg; el.className = cls; }
  }

  // Start smart disruptor
  document.getElementById('smart-disruptor-start')?.addEventListener('click', async () => {
    ensureSmartDisruptorChart();
    // Clear any old chart data from previous run
    if (smartDisruptorChart) {
      smartDisruptorChart.data.labels = [];
      smartDisruptorChart.data.datasets[0].data = [];
      if (Array.isArray(smartDisruptorChart.data.datasets[0].backgroundColor)) {
        smartDisruptorChart.data.datasets[0].backgroundColor = [];
      }
      if (Array.isArray(smartDisruptorChart.data.datasets[0].borderColor)) {
        smartDisruptorChart.data.datasets[0].borderColor = [];
      }
      smartDisruptorChart.update('none');
    }
    setSmartDisruptorStatus('Sending start command…', 'info');
    const saved = loadSavedBTDevice();
    const macaddy = (saved && saved.mac) ? saved.mac : '';
    const duration = parseInt((document.getElementById('smartDisruptDuration') as HTMLInputElement)?.value || '10');
    const unit = (document.getElementById('smartDisruptTimeUnit') as HTMLSelectElement)?.value || 'sec';
    const rssiFloor = parseInt((document.getElementById('smartDisruptRssiFloor') as HTMLInputElement)?.value || '-50');

    // Freq range from dropdown or custom inputs
    const rangeSelect = document.getElementById('smartDisruptFreqRange') as HTMLSelectElement;
    let startFreq = 423.0;
    let stopFreq = 443.0;
    if (rangeSelect) {
      const v = rangeSelect.value;
      if (v === 'custom') {
        startFreq = parseFloat((document.getElementById('smartDisruptStartFreq') as HTMLInputElement)?.value || '423');
        stopFreq = parseFloat((document.getElementById('smartDisruptStopFreq') as HTMLInputElement)?.value || '443');
      } else {
        const parts = v.split('-');
        startFreq = parseFloat(parts[0]);
        stopFreq = parseFloat(parts[1]);
      }
    }

    const params: any = {
      duration,
      unit,
      rssi_floor: rssiFloor,
      start_freq: startFreq,
      stop_freq: stopFreq,
      radio1: { power: smartDisruptorPower1 }
    };
    try {
      const result = await invoke<string>('run_action', {
        action: 'subghz.smart.disruptor.start',
        macaddy,
        params: JSON.stringify(params)
      });
      appendLog(`smart disruptor start => ${result}`);
      smartDisruptorActive = true;
      updateSmartDisruptorBubble();
      setSmartDisruptorStatus('Running — scanning…', 'info');
    } catch (e) {
      appendLog(`smart disruptor start error: ${String(e)}`);
      setSmartDisruptorStatus(`Error: ${String(e)}`, 'error');
    }
  });

  // Stop smart disruptor
  document.getElementById('smart-disruptor-stop')?.addEventListener('click', async () => {
    const saved = loadSavedBTDevice();
    const macaddy = (saved && saved.mac) ? saved.mac : '';
    try {
      setSmartDisruptorStatus('Stopping…', 'info');
      await invoke<string>('run_action', { action: 'subghz.smart.disruptor.stop', macaddy, params: null });
      smartDisruptorActive = false;
      updateSmartDisruptorBubble();
      appendLog('smart disruptor: stopped');
      setSmartDisruptorStatus('Stopped', '');
    } catch (e) {
      appendLog(`smart disruptor stop error: ${String(e)}`);
      setSmartDisruptorStatus(`Stop error: ${String(e)}`, 'error');
    }
  });

  // Smart disruptor freq range dropdown — show/hide custom inputs
  const sdFreqRange = document.getElementById('smartDisruptFreqRange') as HTMLSelectElement;
  sdFreqRange?.addEventListener('change', () => {
    const isCustom = sdFreqRange.value === 'custom';
    const d = isCustom ? '' : 'none';
    ['smartDisruptStartFreq', 'smartDisruptFreqSep', 'smartDisruptStopFreq', 'smartDisruptFreqUnit'].forEach(id => {
      const el = document.getElementById(id);
      if (el) el.style.display = d;
    });
  });

  // ─── Wireshark Capture Buttons ───────────────────────────────
  let wifiCaptureActive = false;
  let capturedFrames: Array<{t:number,src:string,dst:string,proto:string,sub:number,len:number,rssi:number,ch:number}> = [];

  document.getElementById('wifi-capture-start')?.addEventListener('click', async () => {
    const saved = loadSavedBTDevice();
    const macaddy = (saved && saved.mac) ? saved.mac : '';
    const filterVal = (document.getElementById('wifi-capture-filter') as HTMLInputElement)?.value || '';
    // Clear previous capture
    capturedFrames = [];
    const tbody = document.querySelector('#wifi-capture-table tbody');
    if (tbody) tbody.innerHTML = '';
    const logEl = document.getElementById('wifi-capture-log');
    if (logEl) logEl.textContent = 'Starting capture…';

    try {
      const result = await invoke<string>('run_action', {
        action: 'wifi-wireshark',
        macaddy,
        params: filterVal ? JSON.stringify({ filter: filterVal }) : null
      });
      wifiCaptureActive = true;
      appendLog(`wifi capture start => ${result}`);
      if (logEl) logEl.textContent = 'Capturing packets…';
    } catch (e) {
      appendLog(`wifi capture start error: ${String(e)}`);
      if (logEl) logEl.textContent = `Error: ${String(e)}`;
    }
  });

  document.getElementById('wifi-capture-stop')?.addEventListener('click', async () => {
    const saved = loadSavedBTDevice();
    const macaddy = (saved && saved.mac) ? saved.mac : '';
    try {
      await invoke<string>('run_action', { action: 'wifi-wireshark-stop', macaddy, params: null });
      wifiCaptureActive = false;
      appendLog('wifi capture: stopped');
      const logEl = document.getElementById('wifi-capture-log');
      if (logEl) logEl.textContent = `Capture stopped. ${capturedFrames.length} frames captured.`;
    } catch (e) {
      appendLog(`wifi capture stop error: ${String(e)}`);
    }
  });

  document.getElementById('wifi-capture-save')?.addEventListener('click', () => {
    if (capturedFrames.length === 0) {
      appendLog('wifi capture save: no frames to save');
      return;
    }
    // Export as CSV (lightweight alternative to PCAP for BLE-captured metadata)
    const header = 'Time,Source,Destination,Protocol,Subtype,Length,RSSI,Channel';
    const rows = capturedFrames.map(f =>
      `${f.t},${f.src},${f.dst},${f.proto},${f.sub},${f.len},${f.rssi},${f.ch}`
    );
    const csv = header + '\n' + rows.join('\n');
    const blob = new Blob([csv], { type: 'text/csv' });
    const url = URL.createObjectURL(blob);
    const a = document.createElement('a');
    a.href = url;
    a.download = `sharkos_capture_${Date.now()}.csv`;
    a.click();
    URL.revokeObjectURL(url);
    appendLog(`wifi capture: saved ${capturedFrames.length} frames as CSV`);
  });

  // Handler for incoming wifi_capture BLE notifications
  function handleWifiCaptureData(data: any) {
    if (!data || !data.wifi_capture || !Array.isArray(data.wifi_capture.frames)) return;
    const tbody = document.querySelector('#wifi-capture-table tbody');
    const filterVal = (document.getElementById('wifi-capture-filter') as HTMLInputElement)?.value?.toLowerCase() || '';
    const logEl = document.getElementById('wifi-capture-log');

    for (const f of data.wifi_capture.frames) {
      capturedFrames.push(f);
      // Apply client-side filter
      if (filterVal) {
        const haystack = `${f.src} ${f.dst} ${f.proto} ${f.len} ${f.ch}`.toLowerCase();
        if (!haystack.includes(filterVal)) continue;
      }
      if (tbody) {
        const tr = document.createElement('tr');
        // Format subtype info string
        const subtypeMap: Record<string,Record<number,string>> = {
          'MGMT': {0:'AssocReq',1:'AssocResp',2:'ReassocReq',3:'ReassocResp',4:'ProbeReq',5:'ProbeResp',8:'Beacon',9:'ATIM',10:'Disassoc',11:'Auth',12:'Deauth',13:'Action'},
          'CTRL': {8:'BAR',9:'BA',10:'PS-Poll',11:'RTS',12:'CTS',13:'ACK'},
          'DATA': {0:'Data',4:'Null',8:'QoS'}
        };
        const info = subtypeMap[f.proto]?.[f.sub] || `subtype=${f.sub}`;
        tr.innerHTML = `<td>${(f.t / 1000).toFixed(3)}</td><td>${f.src}</td><td>${f.dst}</td><td>${f.proto}</td><td>${f.len}</td><td>${info} ch=${f.ch} rssi=${f.rssi}</td>`;
        tr.style.cssText = 'border-bottom:1px solid rgba(255,255,255,0.04)';
        tbody.appendChild(tr);
        // Auto-scroll
        const container = tbody.closest('div');
        if (container) container.scrollTop = container.scrollHeight;
      }
    }
    if (logEl) logEl.textContent = `Capturing… ${capturedFrames.length} frames`;
  }
  // Expose handler on window so the BLE notification path can call it
  (window as any).__handleWifiCapture = handleWifiCaptureData;

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

