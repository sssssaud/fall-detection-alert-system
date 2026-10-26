// The dashboard page, served from the ESP32. Fully self-contained (no internet on the hotspot).
#pragma once

const char DASHBOARD_HTML[] = R"rawliteral(<!doctype html>
<html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Fall Detector</title>
<style>
:root{--bg:#f4f5f7;--card:#fff;--text:#1d1f23;--muted:#5f636a;--line:#e2e4e8;--ok:#1a7f37;--bad:#c62828;--accent:#2457c5;--on-accent:#fff}
@media (prefers-color-scheme:dark){:root{--bg:#121417;--card:#1c1f24;--text:#e8eaed;--muted:#a0a6ad;--line:#2c3036;--ok:#2e9e4f;--bad:#d93a32;--accent:#7aa2ff;--on-accent:#0d1117}}
*{box-sizing:border-box}
body{margin:0;font:15px/1.45 system-ui,-apple-system,Segoe UI,Roboto,sans-serif;background:var(--bg);color:var(--text)}
main{max-width:920px;margin:0 auto;padding:16px}
header{display:flex;justify-content:space-between;align-items:center;gap:8px;flex-wrap:wrap}
h1{font-size:20px;margin:0}
.pill{padding:4px 12px;border-radius:99px;font-weight:600;font-size:13px;background:var(--line)}
.pill.ok{background:var(--ok);color:#fff}.pill.bad{background:var(--bad);color:#fff}
.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(250px,1fr));gap:12px;margin-top:12px}
.card{background:var(--card);border:1px solid var(--line);border-radius:12px;padding:14px;min-width:0}
.card h2{font-size:12px;text-transform:uppercase;letter-spacing:.05em;color:var(--muted);margin:0 0 8px}
.big{font-size:22px;font-weight:700}
.muted{color:var(--muted)}
.pre{white-space:pre-line;overflow-wrap:anywhere}
#alert{display:none;margin-top:12px;background:var(--bad);color:#fff;border-radius:12px;padding:18px}
#alert.on{display:block;animation:pulse .8s infinite alternate}
@keyframes pulse{to{filter:brightness(1.25)}}
@media (prefers-reduced-motion:reduce){#alert.on{animation:none}}
#alert .big{font-size:30px}
#alert a{color:#fff}
button{font:inherit;font-weight:600;border:0;border-radius:10px;padding:12px 20px;cursor:pointer;min-height:44px}
button:focus-visible{outline:3px solid var(--accent);outline-offset:2px}
#ack{background:#fff;color:#a01818;margin-top:12px}
#sound{background:var(--accent);color:var(--on-accent)}
a{color:var(--accent)}
canvas{width:100%;height:190px;display:block}
ul{list-style:none;padding:0;margin:0}
li{padding:6px 0;border-top:1px solid var(--line)}li:first-child{border-top:0}
table{width:100%;border-collapse:collapse}
td{padding:6px 4px;border-top:1px solid var(--line);font-size:14px;vertical-align:top}
tr:first-child td{border-top:0}
</style></head><body><main>
<header><h1>Fall Detector</h1><span id="online" class="pill">connecting…</span></header>

<section id="alert" role="alert" aria-live="assertive">
  <div class="big" id="aTitle"></div>
  <div id="aInfo"></div>
  <div id="aLoc"></div>
  <button id="ack">Acknowledge</button>
</section>

<div class="grid">
  <div class="card"><h2>Device</h2><div class="big" id="state">–</div><div class="muted" id="dev"></div></div>
  <div class="card"><h2>GPS</h2><div class="big" id="gps">–</div><div class="muted" id="gpsInfo"></div></div>
  <div class="card"><h2>Network</h2><div class="muted pre" id="net"></div></div>
</div>

<div class="card" style="margin-top:12px">
  <h2>Live movement — |a| in g, last 20 s</h2>
  <canvas id="graph" role="img" aria-label="Live acceleration graph"></canvas>
  <div class="muted">Dashed lines: free-fall and impact thresholds.</div>
</div>

<div class="grid">
  <div class="card"><h2>Detector log — why each drop did or didn't count</h2><ul id="log"></ul></div>
  <div class="card"><h2>Alert history</h2><table><tbody id="hist"></tbody></table></div>
</div>

<p><button id="sound" aria-pressed="false">Tap to enable alert sound</button></p>
</main>
<script>
const $ = id => document.getElementById(id);
let seq = 0, pts = [], lastOk = 0, th = [0.5, 2.5], audio = null, soundOn = false, beepTimer = null;

const ago = s => s < 60 ? s + ' s ago' : s < 3600 ? Math.floor(s / 60) + ' min ago' : Math.floor(s / 3600) + ' h ago';
const upTime = s => Math.floor(s / 3600) + 'h ' + Math.floor(s / 60) % 60 + 'm';
const mapLink = (lat, lon) => `<a href="https://maps.google.com/?q=${lat},${lon}" target="_blank" rel="noopener">${lat.toFixed(6)}, ${lon.toFixed(6)}</a>`;
const where = a => a.fix ? mapLink(a.lat, a.lon) + (a.fix_age > 60 ? ' · position from ' + ago(a.fix_age) : '') : 'No GPS fix yet';
function item(tag, text, cls) { const e = document.createElement(tag); e.textContent = text; if (cls) e.className = cls; return e; }

function tone() {
  if (!soundOn) return;
  const o = audio.createOscillator(), g = audio.createGain();
  o.frequency.value = 880; g.gain.value = 0.25;
  o.connect(g); g.connect(audio.destination);
  o.start(); o.stop(audio.currentTime + 0.3);
}
function beep(on) {
  if (on && !beepTimer) { tone(); beepTimer = setInterval(tone, 700); }
  if (!on && beepTimer) { clearInterval(beepTimer); beepTimer = null; }
}
$('sound').onclick = () => {
  soundOn = !soundOn;
  if (soundOn) { audio = audio || new (window.AudioContext || window.webkitAudioContext)(); audio.resume(); tone(); }
  $('sound').textContent = soundOn ? 'Alert sound on (tap to mute)' : 'Tap to enable alert sound';
  $('sound').setAttribute('aria-pressed', soundOn);
};
$('ack').onclick = async () => {
  $('ack').disabled = true;
  try { await fetch('/api/ack', { method: 'POST' }); } catch (e) {}
  $('ack').disabled = false;
};

function draw() {
  const c = $('graph'), dpr = window.devicePixelRatio || 1, w = c.clientWidth, h = c.clientHeight;
  if (c.width !== Math.round(w * dpr)) { c.width = Math.round(w * dpr); c.height = Math.round(h * dpr); }
  const x = c.getContext('2d'), css = getComputedStyle(document.documentElement);
  x.setTransform(dpr, 0, 0, dpr, 0, 0);
  x.clearRect(0, 0, w, h);
  const top = Math.max(3, ...pts) * 1.05, Y = v => h - 6 - (v / top) * (h - 12);
  x.font = '11px system-ui'; x.fillStyle = x.strokeStyle = css.getPropertyValue('--muted');
  x.setLineDash([4, 4]); x.lineWidth = 1;
  for (const t of th) { x.beginPath(); x.moveTo(0, Y(t)); x.lineTo(w, Y(t)); x.stroke(); x.fillText(t + ' g', 4, Y(t) - 3); }
  x.setLineDash([]); x.strokeStyle = css.getPropertyValue('--accent'); x.lineWidth = 2; x.beginPath();
  pts.forEach((v, i) => { const px = w - (pts.length - 1 - i) * (w / 499); i ? x.lineTo(px, Y(v)) : x.moveTo(px, Y(v)); });
  x.stroke();
}

function render(d) {
  th = d.th;
  pts.push(...d.g);
  if (pts.length > 500) pts.splice(0, pts.length - 500);  // 20 s at 25 Hz
  seq = d.seq;
  draw();

  $('state').textContent = d.imu.ok ? d.imu.state : 'IMU ERROR';
  $('dev').textContent = 'g = ' + d.imu.g.toFixed(2) + ' · running ' + upTime(d.up);

  const p = d.gps;
  $('gps').textContent = !p.data ? 'No data' : p.fix ? 'Fix' : 'Searching';
  $('gpsInfo').innerHTML = p.sats + ' satellites' + (p.has_last ? '<br>Last: ' + mapLink(p.lat, p.lon) + ' · ' + ago(p.age) : '');

  const n = d.net;
  $('net').textContent = `Hotspot "${n.ap_ssid}": http://${n.ap_ip} (${n.clients} connected)\n` +
    (n.sta ? `WiFi "${n.ssid}": http://${n.ip} (${n.rssi} dBm)` : 'WiFi: not connected') + '\nhttp://falldetector.local';

  const a = d.alerts[0], active = d.alert_active && a;
  $('alert').className = active ? 'on' : '';
  if (active) {
    $('aTitle').textContent = a.type === 'FALL' ? 'FALL DETECTED' : 'SOS PRESSED';
    $('aInfo').textContent = ago(a.ago) + (a.type === 'FALL' ? ' · impact ' + a.peak.toFixed(1) + ' g' : '');
    $('aLoc').innerHTML = where(a);
  }
  beep(active);
  document.title = active ? 'ALERT – Fall Detector' : 'Fall Detector';

  $('log').replaceChildren(...(d.log.length ? d.log.map(e => item('li', e.text + ' — ' + ago(e.ago)))
                                             : [item('li', 'No events yet. Drop it and watch here.', 'muted')]));
  const rows = d.alerts.map(x => {
    const tr = document.createElement('tr');
    tr.append(item('td', '#' + x.id + ' ' + x.type), item('td', ago(x.ago)));
    const td = document.createElement('td'); td.innerHTML = where(x); tr.append(td);
    return tr;
  });
  $('hist').replaceChildren(...(rows.length ? rows : [item('tr', '')]));
  if (!rows.length) $('hist').firstChild.append(item('td', 'No alerts yet', 'muted'));
}

async function poll() {
  try {
    const r = await fetch('/api/state?since=' + seq, { cache: 'no-store' });
    render(await r.json());
    lastOk = Date.now();
  } catch (e) {}
  const on = Date.now() - lastOk < 3000;
  $('online').textContent = on ? 'online' : 'DEVICE OFFLINE';
  $('online').className = 'pill ' + (on ? 'ok' : 'bad');
  setTimeout(poll, 500);
}
poll();
</script></body></html>)rawliteral";
