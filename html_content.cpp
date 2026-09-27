#pragma once
const char* htmlContent = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8"/>
<meta name="viewport" content="width=device-width, initial-scale=1.0"/>
<title>Water Tank Controller</title>

<style>
body {
  font-family: 'Segoe UI', sans-serif;
  background: #ffffff;
  color: #0a192f;
  margin: 0;
  padding: 20px;
  text-align: center;
}

/* Tank */
.tank-level {
  width: 130px;
  height: 200px;
  border: 3px solid #00796b;
  border-radius: 12px;
  margin: 20px auto;
  position: relative;
  overflow: hidden;
}
.tank-fill {
  position: absolute;
  bottom: 0;
  width: 100%;
  background: linear-gradient(to top, #00c6ff, #0072ff);
  color: #fff;
  text-align: center;
  font-size: 18px;
  font-weight: bold;
  transition: height .5s;
}

/* Mode text (small, read-only) */
.mode-text {
  font-size: 16px;
  font-weight: 600;
  margin-bottom: 15px;
  color: #0a192f;
}

/* ⭐ Motor status button — replaces old status div + separate motor button.
   Clicking this toggles the motor and the label itself reflects state. */
.motor-status-btn {
  display: block;
  margin: 10px auto 20px auto;
  padding: 18px 30px;
  min-width: 220px;
  border: none;
  border-radius: 14px;
  font-size: 20px;
  font-weight: 700;
  color: #fff;
  cursor: pointer;
  transition: transform 0.15s, box-shadow 0.3s;
}
.motor-status-btn:active {
  transform: scale(0.97);
}
.motor-off {
  background: linear-gradient(135deg, #ff5252, #b71c1c);
  box-shadow: 0 0 12px rgba(255, 0, 0, 0.35);
}
.motor-on {
  background: linear-gradient(135deg, #00c853, #1b5e20);
  box-shadow: 0 0 14px rgba(0, 255, 0, 0.45);
}

/* Grid */
.button-grid {
  display: grid;
  grid-template-columns: repeat(3, 1fr);
  gap: 12px;
  max-width: 520px;
  margin: 0 auto;
}

/* Buttons */
.button {
  padding: 14px;
  border: none;
  border-radius: 12px;
  font-size: 15px;
  color: #fff;
  font-weight: 600;
  cursor: pointer;
  transition: 0.3s;
  text-decoration: none;
  display: inline-block;
}

/* Mode Buttons */
.semi-off { background: #fbc02d; color: #000; }
.semi-on  { background: #f57f17; color: #000; }
.auto-off { background: #43a047; }
.auto-on  { background: #1b5e20; }

/* Other */
.timer     { background: #00acc1; color: #000; }
.countdown { background: #ef6c00; }
.twist     { background: #00695c; }
.settings  { background: #6a1b9a; }

/* Live data panel (refreshed every second) */
.live-grid {
  display: grid;
  grid-template-columns: repeat(3, 1fr);
  gap: 8px;
  max-width: 520px;
  margin: 0 auto 20px auto;
}
.live-cell {
  border: 1px solid #cfd8dc;
  border-radius: 10px;
  padding: 8px 4px;
}
.live-cell .k { font-size: 12px; color: #546e7a; }
.live-cell .v { font-size: 17px; font-weight: 700; }
.live-stale { opacity: 0.45; }

@media (max-width: 520px) {
  .button-grid { grid-template-columns: repeat(2, 1fr); }
}
</style>
</head>

<body>

<h2>Water Level</h2>
<div class="tank-level">
  <div id="levelFill" class="tank-fill" style="height:0%">0%</div>
</div>

<h2>Motor</h2>
<!-- ⭐ Motor status IS the toggle button -->
<button id="motorBtn" class="motor-status-btn motor-off" onclick="toggleMotor()">
  Motor: OFF
</button>

<div id="modeStatus" class="mode-text">Mode: UNKNOWN</div>

<div id="liveGrid" class="live-grid live-stale">
  <div class="live-cell"><div class="k">Voltage</div><div class="v" id="lvVolt">--</div></div>
  <div class="live-cell"><div class="k">Current</div><div class="v" id="lvAmp">--</div></div>
  <div class="live-cell"><div class="k">Time</div><div class="v" id="lvTime">--</div></div>
  <div class="live-cell"><div class="k">G.W</div><div class="v" id="lvGw">--</div></div>
  <div class="live-cell"><div class="k">Dry sensor</div><div class="v" id="lvDry">--</div></div>
</div>

<h2>Controls</h2>

<div class="button-grid">

  <button id="autoBtn" class="button auto-off" onclick="toggleAuto()">Auto Mode</button>
  <button id="semiBtn" class="button semi-off" onclick="toggleSemi()">Semi Auto</button>

  <a class="button timer"     href="/timer">Timer</a>
  <a class="button countdown" href="/countdown">Countdown</a>
  <a class="button twist"     href="/twist">Twist</a>
  <a class="button settings"  href="/settings">Settings</a>

</div>

<script>

function renderMotor(state) {
  const motorBtn = document.getElementById("motorBtn");
  if (state === "ON") {
    motorBtn.className = "motor-status-btn motor-on";
    motorBtn.innerHTML = "⚡ Motor: ON";
  } else {
    motorBtn.className = "motor-status-btn motor-off";
    motorBtn.innerHTML = "Motor: OFF";
  }
}

async function fetchStatus() {
  try {
    const res = await fetch("/status");
    const data = await res.json();

    // Motor (now shown on the toggle button itself)
    renderMotor(data.motor);

    // Mode
    document.getElementById("modeStatus").innerHTML = "Mode: " + data.mode;

    // Tank
    let lvl = parseInt(data.level) || 0;
    if (lvl < 0) lvl = 0;
    if (lvl > 100) lvl = 100;
    document.getElementById("levelFill").style.height = lvl + "%";
    document.getElementById("levelFill").innerHTML = lvl + "%";

    updateModeButtons(data);

  } catch (e) {}
}

function updateModeButtons(data) {
  const mode = data.mode;

  document.getElementById("autoBtn").className =
    (mode === "AUTO") ? "button auto-on" : "button auto-off";

  document.getElementById("semiBtn").className =
    (mode === "SEMIAUTO") ? "button semi-on" : "button semi-off";
}

async function toggleAuto() {
  await fetch("/auto_toggle");
  fetchStatus();
}

async function toggleSemi() {
  await fetch("/semi_toggle");
  fetchStatus();
}

async function toggleMotor() {
  try {
    const res = await fetch("/motor_toggle");
    const data = await res.json();
    renderMotor(data.motor);
  } catch (e) {
    fetchStatus(); // fallback
  }
}

/* Other data: voltage, current, G.W, dry sensor, time */
async function fetchLive() {
  try {
    const res = await fetch("/live");
    const d = await res.json();
    document.getElementById("liveGrid").className =
      d.fresh ? "live-grid" : "live-grid live-stale";
    document.getElementById("lvVolt").innerHTML = d.volts + " V";
    document.getElementById("lvAmp").innerHTML  = d.amps + " A";
    document.getElementById("lvTime").innerHTML = d.time;
    document.getElementById("lvGw").innerHTML   = d.gw ? "YES" : "NO";
    document.getElementById("lvDry").innerHTML  = d.dryWater ? "WATER" : "DRY";
  } catch (e) {}
}

/* Client DEC-16: motor / level / mode every 3 s, other data every 1 s */
setInterval(fetchStatus, 3000);
setInterval(fetchLive, 1000);
fetchStatus();
fetchLive();

</script>

</body>
</html>
)rawliteral";