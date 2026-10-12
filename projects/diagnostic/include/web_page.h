#pragma once
// The diagnostic page, served from flash at http://192.168.4.1
//
// Self-contained: no CDN, no fonts, no external anything. The tool runs as an
// isolated access point in a parked car with no route to the internet, so an
// external reference would simply never load.
//
// The page polls /api/state; it does not stream. A synchronous WebServer and
// SSE do not mix well, and a 400 ms poll is plenty for a scan whose steps take
// a hundred milliseconds each.

#include <Arduino.h>

static const char DIAG_PAGE[] PROGMEM = R"HTML(<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Pajero Diagnostics</title>
<style>
  :root {
    --bg:#0f1114; --panel:#171a1f; --line:#272c34; --fg:#d7dce3; --dim:#78818f;
    --ok:#4ec9a0; --warn:#d4b35a; --bad:#e05c5c; --accent:#5b9dd9;
  }
  *{box-sizing:border-box}
  body{margin:0;padding:16px;background:var(--bg);color:var(--fg);
       font:15px/1.5 -apple-system,BlinkMacSystemFont,"Segoe UI",Roboto,sans-serif}
  h1{font-size:17px;margin:0 0 2px;letter-spacing:.03em}
  .sub{color:var(--dim);font-size:13px;margin-bottom:16px}
  button{font:inherit;font-weight:500;color:#0f1114;background:var(--accent);
         border:0;border-radius:8px;padding:12px 20px;cursor:pointer;width:100%}
  button:disabled{opacity:.45;cursor:default}
  button.ghost{background:transparent;color:var(--dim);border:1px solid var(--line);
               font-weight:400;padding:10px 20px}
  .card{background:var(--panel);border:1px solid var(--line);border-radius:10px;
        padding:14px 16px;margin-bottom:12px}
  .bar{height:6px;background:var(--line);border-radius:3px;overflow:hidden;margin:12px 0 8px}
  .bar div{height:100%;background:var(--accent);width:0;transition:width .3s}
  .step{color:var(--dim);font-size:13px}
  .verdict{font-size:19px;font-weight:600;margin-bottom:2px}
  .v-ok{color:var(--ok)} .v-bad{color:var(--bad)} .v-dim{color:var(--dim)}
  .code{display:flex;align-items:flex-start;gap:10px;padding:11px 0;
        border-bottom:1px solid #1d2128}
  .code:last-child{border-bottom:0}
  .num{font:600 16px/1.3 ui-monospace,Menlo,monospace;min-width:62px}
  .n-stored{color:var(--bad)} .n-pending{color:var(--warn)} .n-permanent{color:var(--bad)}
  .desc{flex:1}
  .desc small{display:block;color:var(--dim);font-size:12px;margin-top:1px}
  .tag{font-size:10px;text-transform:uppercase;letter-spacing:.07em;
       border:1px solid var(--line);border-radius:4px;padding:2px 6px;color:var(--dim);
       white-space:nowrap}
  .rd{display:flex;justify-content:space-between;padding:5px 0;font-size:13px}
  .rd span:last-child{color:var(--dim)}
  .muted{color:var(--dim);font-size:13px}
  .hdr{font-size:11px;text-transform:uppercase;letter-spacing:.07em;
       color:var(--dim);margin-bottom:8px}
  .row{display:flex;gap:10px;margin-top:12px}
</style>
</head>
<body>

<h1>PAJERO DIAGNOSTICS</h1>
<div class="sub">Pajero Sport II &middot; KH8W &middot; 4M41 3.2 DI-D</div>

<div class="card">
  <button id="scan">Scan</button>
  <div class="bar"><div id="fill"></div></div>
  <div class="step" id="step">Ready.</div>
</div>

<div class="card" id="verdictCard" hidden>
  <div class="verdict" id="verdict"></div>
  <div class="muted" id="verdictSub"></div>
</div>

<div class="card" id="codesCard" hidden>
  <div class="hdr">Trouble codes</div>
  <div id="codes"></div>
</div>

<div class="card" id="rdCard" hidden>
  <div class="hdr">Readiness monitors</div>
  <div id="rd"></div>
  <div class="muted" style="margin-top:8px">
    Continuous monitors only. The non-continuous set differs between diesel and
    petrol, so it is not shown rather than shown wrongly.
  </div>
</div>

<div class="card">
  <div class="hdr">Scope</div>
  <div class="muted">
    Standard OBD-II against the engine ECU. ABS, airbag, 4WD and ETACS are on
    K-line on this vehicle and cannot be read over CAN.
  </div>
  <div class="row"><button class="ghost" id="clear">Clear codes</button></div>
</div>

<script>
const $ = (id) => document.getElementById(id);
let busy = false;

function render(s) {
  const pct = s.total ? Math.round((s.step / s.total) * 100) : 0;
  $('fill').style.width = (s.running ? pct : (s.step ? pct : 100)) + '%';
  $('step').textContent = s.running ? s.step_name + '…' : s.message;
  $('scan').disabled = s.running;
  busy = s.running;

  const scanned = !s.running && (s.ecu_ok || s.codes.length);
  $('verdictCard').hidden = !scanned;
  if (scanned) {
    const bad = s.codes.length > 0 || s.mil;
    $('verdict').className = 'verdict ' + (bad ? 'v-bad' : 'v-ok');
    $('verdict').textContent = bad
      ? (s.mil ? 'Check-engine lamp is ON' : 'Codes stored, lamp off')
      : 'No faults reported';
    const bits = [];
    bits.push(s.codes.length + ' code' + (s.codes.length === 1 ? '' : 's'));
    if (s.have_status) bits.push('ECU reports ' + s.dtc_count + ' confirmed');
    if (s.freeze) bits.push('freeze frame from ' + s.freeze);
    $('verdictSub').textContent = bits.join(' · ');
  }

  $('codesCard').hidden = s.codes.length === 0;
  $('codes').textContent = '';
  for (const c of s.codes) {
    const row = document.createElement('div');
    row.className = 'code';
    const num = document.createElement('div');
    num.className = 'num n-' + c.kind;
    num.textContent = c.code;
    const d = document.createElement('div');
    d.className = 'desc';
    d.textContent = c.desc || 'Manufacturer-specific — no generic description';
    if (!c.desc) {
      const s2 = document.createElement('small');
      s2.textContent = 'Look this one up in a Mitsubishi code list.';
      d.appendChild(s2);
    }
    const t = document.createElement('span');
    t.className = 'tag';
    t.textContent = c.kind;
    row.append(num, d, t);
    $('codes').appendChild(row);
  }

  $('rdCard').hidden = !s.have_status;
  if (s.have_status) {
    $('rd').textContent = '';
    const names = { misfire: 'Misfire', fuel: 'Fuel system', components: 'Components' };
    for (const k of ['misfire', 'fuel', 'components']) {
      const m = s.readiness[k];
      const row = document.createElement('div');
      row.className = 'rd';
      const a = document.createElement('span');
      a.textContent = names[k];
      const b = document.createElement('span');
      b.textContent = !m.sup ? 'not supported' : (m.ok ? 'complete' : 'not complete');
      if (m.sup) b.style.color = m.ok ? 'var(--ok)' : 'var(--warn)';
      row.append(a, b);
      $('rd').appendChild(row);
    }
  }
}

async function poll() {
  try { render(await (await fetch('/api/state')).json()); }
  catch (e) { $('step').textContent = 'Lost the connection to the adapter.'; }
}

$('scan').addEventListener('click', async () => {
  $('scan').disabled = true;
  await fetch('/api/scan', { method: 'POST' });
  poll();
});

$('clear').addEventListener('click', async () => {
  if (busy) return;
  const ok = confirm(
    'Clear all stored trouble codes?\n\n' +
    'This also erases freeze-frame data and resets the readiness monitors, ' +
    'which take a full drive cycle to complete again. If the underlying fault ' +
    'is still present the code will come back.');
  if (!ok) return;
  const r = await fetch('/api/clear?confirm=CLEAR', { method: 'POST' });
  if (!r.ok) alert(await r.text());
  poll();
});

poll();
setInterval(poll, 400);
</script>
</body>
</html>)HTML";
