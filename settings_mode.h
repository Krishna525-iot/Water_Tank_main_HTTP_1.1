#ifndef SETTINGS_MODE_H
#define SETTINGS_MODE_H

const char* settingsModeHtml = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Device Settings</title>

<style>
:root{
  --primary:#2563eb;
  --primary2:#7c3aed;
  --danger:#dc2626;
  --success:#16a34a;
  --bg1:#eef2ff;
  --bg2:#fdf4ff;
  --border:#e5e7eb;
  --text:#0f172a;
}

*{box-sizing:border-box}

body{
  margin:0;
  font-family:Segoe UI,Roboto,Arial,sans-serif;
  background:linear-gradient(135deg,var(--bg1),var(--bg2));
  color:var(--text);
}

.wrap{max-width:1050px;margin:30px auto;padding:15px;}

.card{
  background:#ffffff;
  border-radius:20px;
  padding:32px;
  box-shadow:0 20px 40px rgba(0,0,0,.08);
}

.section{
  margin-top:28px;
  padding:24px;
  border-radius:16px;
  background:#ffffff;
  border:1px solid var(--border);
}

.section-title{
  font-size:18px;
  font-weight:700;
  margin-bottom:20px;
  color:var(--primary);
}

.setting-row{
  display:flex;
  align-items:center;
  justify-content:space-between;
  gap:20px;
  margin-bottom:16px;
  flex-wrap:wrap;
}

.setting-left{
  flex:1;
  min-width:200px;
}

.setting-right{
  display:flex;
  align-items:center;
  gap:12px;
}

input,select{
  padding:10px;
  border-radius:10px;
  border:1px solid var(--border);
  width:140px;
}

/* Toggle */
.switch {
  position: relative;
  display: inline-block;
  width: 46px;
  height: 24px;
}

.switch input { display:none; }

.slider {
  position: absolute;
  cursor: pointer;
  top: 0; left: 0; right: 0; bottom: 0;
  background-color: #ccc;
  transition: .3s;
  border-radius: 30px;
}

.slider:before {
  position: absolute;
  content: "";
  height: 18px;
  width: 18px;
  left: 3px;
  bottom: 3px;
  background-color: white;
  transition: .3s;
  border-radius: 50%;
}

input:checked + .slider {
  background:linear-gradient(135deg,var(--primary),var(--primary2));
}

input:checked + .slider:before {
  transform: translateX(22px);
}

.actions{
  display:flex;
  justify-content:space-between;
  margin-top:35px;
  gap:12px;
  flex-wrap:wrap;
}

.btn{
  padding:14px 30px;
  border-radius:14px;
  border:none;
  font-weight:700;
  cursor:pointer;
}

.btn-primary{
  background:linear-gradient(135deg,var(--primary),var(--primary2));
  color:#fff;
}

.btn-danger{
  background:#fff;
  border:2px solid var(--danger);
  color:var(--danger);
}

.btn-secondary{
  background:#fff;
  border:2px solid var(--primary);
  color:var(--primary);
}

.status{
  margin-top:20px;
  padding:14px;
  border-radius:12px;
  border:1px solid var(--border);
  background:#fff;
}

.status.success{color:var(--success)}
.status.error{color:var(--danger)}

.disabled-input{
  opacity:0.5;
  pointer-events:none;
}

.blur-disabled{
  opacity:0.6;
  filter:blur(0.3px);
  transition:0.2s ease;
}
</style>
</head>

<body>
<div class="wrap">
<div class="card">

<h2>Device Settings</h2>

<!-- DEVICE PROTECTION -->
<div class="section">
<div class="section-title">Device Protection</div>

<div class="setting-row">
<div class="setting-left">Dry Run (minutes)</div>
<div class="setting-right">
<input type="number" id="dryRunGap" min="1" max="180">
<label class="switch">
<input type="checkbox" id="dryRunGap_en" onchange="toggleInput('dryRunGap','dryRunGap_en')">
<span class="slider"></span>
</label>
</div>
</div>

<div class="setting-row">
<div class="setting-left">Testing Gap (minutes)</div>
<div class="setting-right">
<input type="number" id="testingGap" min="1" max="180">
</div>
</div>

<div class="setting-row">
<div class="setting-left">Maximum Run Time (minutes)</div>
<div class="setting-right">
<input type="number" id="maxRun" min="1" max="1440">
<label class="switch">
<input type="checkbox" id="maxRun_en" onchange="toggleInput('maxRun','maxRun_en')">
<span class="slider"></span>
</label>
</div>
</div>

<div class="setting-row">
<div class="setting-left">Retry Count</div>
<div class="setting-right">
<input type="number" id="retryCount" min="1" max="20">
<label class="switch">
<input type="checkbox" id="retryCount_en" onchange="toggleInput('retryCount','retryCount_en')">
<span class="slider"></span>
</label>
</div>
</div>

</div>

<!-- VOLTAGE SETTINGS -->
<div class="section">
<div class="section-title">Voltage and Load Limits</div>

<div class="setting-row">
<div class="setting-left">Low Voltage Cutoff</div>
<div class="setting-right">
<input type="number" id="lowVolt">
<label class="switch">
<input type="checkbox" id="lowVolt_en" onchange="toggleInput('lowVolt','lowVolt_en')">
<span class="slider"></span>
</label>
</div>
</div>

<div class="setting-row">
<div class="setting-left">High Voltage Cutoff</div>
<div class="setting-right">
<input type="number" id="highVolt">
<label class="switch">
<input type="checkbox" id="highVolt_en" onchange="toggleInput('highVolt','highVolt_en')">
<span class="slider"></span>
</label>
</div>
</div>

<div class="setting-row">
<div class="setting-left">Over Load Current (A)</div>
<div class="setting-right">
<input type="number" id="overLoad">
<label class="switch">
<input type="checkbox" id="overLoad_en" onchange="toggleInput('overLoad','overLoad_en')">
<span class="slider"></span>
</label>
</div>
</div>

<div class="setting-row">
<div class="setting-left">Under Load Current (A)</div>
<div class="setting-right">
<input type="number" id="underLoad">
<label class="switch">
<input type="checkbox" id="underLoad_en" onchange="toggleInput('underLoad','underLoad_en')">
<span class="slider"></span>
</label>
</div>
</div>

</div>

<!-- Power Restore removed: the device always restores the last mode (client decision) -->

<!-- BUZZER SETTINGS -->
<div class="section">
<div class="section-title">Buzzer Control</div>

<div class="setting-row">
<div class="setting-left">During pump on (as long as pump runs)</div>
<div class="setting-right">
<label class="switch">
<input type="checkbox" id="buzzerPump">
<span class="slider"></span>
</label>
</div>
</div>

<div class="setting-row">
<div class="setting-left">Tank full (30 s)</div>
<div class="setting-right">
<label class="switch">
<input type="checkbox" id="buzzerFull">
<span class="slider"></span>
</label>
</div>
</div>

<div class="setting-row">
<div class="setting-left">Tank empty (15 s)</div>
<div class="setting-right">
<label class="switch">
<input type="checkbox" id="buzzerEmpty">
<span class="slider"></span>
</label>
</div>
</div>

</div>

<div class="actions">
<button class="btn btn-danger"    onclick="factoryReset()">Factory Reset</button>
<button id="applyBtn" class="btn btn-primary" onclick="apply()">Apply Settings</button>
</div>

<button type="button" class="btn btn-secondary"
        style="width:100%;margin-top:14px;"
        onclick="location.href='/'">Back</button>

<div id="status" class="status">Reading device...</div>

</div>
</div>

<script>

/* Every field the page shows, as returned by /get_settings */
const FIELDS = [
  "dryRunGap",  "dryRunGap_en",
  "testingGap",
  "maxRun",     "maxRun_en",
  "retryCount", "retryCount_en",
  "lowVolt",    "lowVolt_en",
  "highVolt",   "highVolt_en",
  "overLoad",   "overLoad_en",
  "underLoad",  "underLoad_en",
  "buzzerPump", "buzzerFull", "buzzerEmpty"
];

/* Values come from the device itself (the ESP asks the STM32 first) */
function loadSettings(msg){
  const status = document.getElementById("status");
  status.className = "status";
  status.innerText = "Reading device...";

  return fetch('/get_settings')
  .then(res => res.json())
  .then(data => {
    FIELDS.forEach(id => {
      if (data[id] === undefined) return;
      const el = document.getElementById(id);
      if (el.type === "checkbox") el.checked = data[id] == 1;
      else                        el.value   = data[id];
    });

    FIELDS.forEach(id => {
      if (id.endsWith("_en")) toggleInput(id.replace("_en", ""), id);
    });

    if (data.fromDevice == 1) {
      status.className = "status success";
      status.innerText = msg || "Showing current device settings";
    } else {
      status.className = "status error";
      status.innerText = "Device not responding - showing last known values";
    }
  })
  .catch(() => {
    status.className = "status error";
    status.innerText = "Failed to load settings";
  });
}

window.onload = function(){ loadSettings(); };

function toggleInput(inputId, toggleId){
  let input  = document.getElementById(inputId);
  let toggle = document.getElementById(toggleId);
  if (!toggle.checked)
    input.classList.add("disabled-input");
  else
    input.classList.remove("disabled-input");
}

/* page field -> STM32 key. The number is always sent with its *E toggle;
   the ESP sends 0 (= disabled) to the device when a toggle is off and
   keeps the number for when it is switched back on. */
const keyMap = {
  dryRunGap  : "D",
  testingGap : "T",
  maxRun     : "M",
  retryCount : "RC",
  lowVolt    : "LV",
  highVolt   : "HV",
  overLoad   : "OL",
  underLoad  : "UL"
};

const enableMap = {
  dryRunGap  : "dryRunGap_en",
  maxRun     : "maxRun_en",
  retryCount : "retryCount_en",
  lowVolt    : "lowVolt_en",
  highVolt   : "highVolt_en",
  overLoad   : "overLoad_en",
  underLoad  : "underLoad_en"
};

const enableKey = {
  dryRunGap_en : "DE",  maxRun_en   : "ME",  retryCount_en : "RCE",
  lowVolt_en   : "LVE", highVolt_en : "HVE", overLoad_en   : "OLE",
  underLoad_en : "ULE"
};

function apply(){

  const btn    = document.getElementById("applyBtn");
  const status = document.getElementById("status");

  let p = [];

  Object.keys(keyMap).forEach(id => {
    p.push(keyMap[id] + "=" + document.getElementById(id).value);
  });

  Object.keys(enableKey).forEach(id => {
    p.push(enableKey[id] + "=" + (document.getElementById(id).checked ? 1 : 0));
  });

  p.push("BZ=" + (document.getElementById("buzzerPump").checked  ? 1 : 0));
  p.push("BF=" + (document.getElementById("buzzerFull").checked  ? 1 : 0));
  p.push("BE=" + (document.getElementById("buzzerEmpty").checked ? 1 : 0));

  btn.disabled = true;
  status.className = "status";
  status.innerText = "Saving settings...";

  fetch("/settings/set?data=" + encodeURIComponent(p.join(";")))
    .then(r => r.text())
    .then(t => loadSettings(t))       /* show what the device now holds */
    .catch(() => {
      status.className = "status error";
      status.innerText = "Failed to save settings";
    })
    .finally(() => btn.disabled = false);
}

function factoryReset(){
  if (!confirm("Reset device to factory default?")) return;
  const status = document.getElementById("status");
  status.className = "status";
  status.innerText = "Resetting...";
  fetch("/factory_reset")
    .then(r => r.text())
    .then(t => loadSettings(t))
    .catch(() => {
      status.className = "status error";
      status.innerText = "Factory reset failed";
    });
}

</script>

</body>
</html>
)rawliteral";

#endif