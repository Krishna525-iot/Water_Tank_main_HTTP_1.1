#pragma once
const char* countdownModeHtml = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <title>Countdown Mode</title>
  <meta name="viewport" content="width=device-width, initial-scale=1.0"/>
  <style>
    body {
      margin: 0; padding: 0;
      font-family: 'Segoe UI', sans-serif;
      background: radial-gradient(circle at top, #0f2027, #203a43, #2c5364);
      color: #ffffff;
      min-height: 100vh;
      display: flex; justify-content: center; align-items: center;
    }
    .container {
      width: 90%; max-width: 420px;
      background: rgba(255,255,255,0.05);
      padding: 28px; border-radius: 14px;
      box-shadow: 0 4px 20px rgba(0,0,0,0.4);
      backdrop-filter: blur(8px);
    }
    h1 { text-align: center; color: #00bcd4; margin-bottom: 24px; font-weight: 600; }
    label { display: block; margin-bottom: 8px; font-weight: 500; color: #d1d5db; }
    input[type="number"] {
      width: 100%; padding: 12px;
      border: 1px solid #37474f; border-radius: 8px;
      margin-bottom: 16px; font-size: 15px;
      background-color: #1e272e; color: #ffffff;
      outline: none; transition: border-color 0.2s;
      box-sizing: border-box;
    }
    input[type="number"]:focus { border-color: #00bcd4; }
    .button {
      display: block; width: 100%; padding: 14px;
      font-size: 16px; border: none; border-radius: 10px;
      color: #ffffff;
      background: linear-gradient(135deg, #009688, #26c6da);
      cursor: pointer; transition: all 0.2s ease-in-out;
    }
    .button.stop  { background: linear-gradient(135deg, #f44336, #e57373); }
    .button.back  { background: linear-gradient(135deg, #1565c0, #42a5f5); margin-top: 24px; }
    .button:hover { opacity: 0.95; transform: scale(1.02); }
    #message { margin-top: 16px; font-size: 16px; text-align: center; }
    #countdownDisplay {
      margin-top: 16px; font-size: 22px; font-weight: 700;
      text-align: center; color: #00e676; min-height: 32px;
    }
  </style>
</head>
<body>
<div class="container">
  <h1>Countdown Mode</h1>
  <label>Duration (minutes)</label>
  <input type="number" id="duration" min="1" max="180" placeholder="1 – 180" />
  <button id="toggleBtn" class="button" onclick="handleToggle()">Start</button>
  <div id="countdownDisplay"></div>
  <p id="message"></p>
  <a href="/"><button class="button back" type="button">Back</button></a>
</div>
<script>
  let remaining = 0;
  let ticker    = null;
  let isActive  = false;

  /* ── restore last set duration on page load — never auto-start ── */
  window.addEventListener('load', async () => {
    try {
      const r = await fetch('/get_countdown');
      const d = await r.json();

      if (d.duration && d.duration > 0) {
        document.getElementById('duration').value = Math.round(d.duration / 60);
        document.getElementById('countdownDisplay').textContent =
          'Last set: ' + Math.round(d.duration / 60) + ' min';
      } else {
        document.getElementById('countdownDisplay').textContent = 'Waiting for input…';
      }
    } catch {
      document.getElementById('countdownDisplay').textContent = 'Waiting for input…';
    }
  });

  function tick() {
    if (remaining <= 0) {
      clearInterval(ticker);
      ticker   = null;
      isActive = false;
      setIdleUI();
      document.getElementById('countdownDisplay').textContent = 'Countdown finished.';
      return;
    }
    const m = Math.floor(remaining / 60);
    const s = remaining % 60;
    document.getElementById('countdownDisplay').textContent =
      `${m}:${s.toString().padStart(2, '0')} remaining`;
    remaining--;
  }

  async function handleToggle() {
    if (isActive) { await stopCountdown(); return; }

    const mins = parseInt(document.getElementById('duration').value);
    if (!mins || mins < 1 || mins > 180) {
      alert('Enter a value between 1 and 180 minutes.'); return;
    }
    try {
      const r   = await fetch(`/start_countdown?duration=${mins * 60}`);
      const msg = await r.text();
      document.getElementById('message').textContent = msg;

      remaining = mins * 60;
      setActiveUI();
      clearInterval(ticker);
      tick();
      ticker = setInterval(tick, 1000);
    } catch {
      document.getElementById('message').textContent = 'Failed to communicate with device.';
    }
  }

  async function stopCountdown() {
    try {
      clearInterval(ticker); ticker = null;
      const r   = await fetch('/countdown_stop');
      const msg = await r.text();
      document.getElementById('message').textContent = msg;
      document.getElementById('countdownDisplay').textContent = 'Stopped.';
      isActive = false;
      setIdleUI();
    } catch {
      document.getElementById('message').textContent = 'Failed to stop.';
    }
  }

  function setActiveUI() {
    isActive = true;
    const btn = document.getElementById('toggleBtn');
    btn.textContent = 'Stop';
    btn.className   = 'button stop';
  }

  function setIdleUI() {
    const btn = document.getElementById('toggleBtn');
    btn.textContent = 'Start';
    btn.className   = 'button';
  }
</script>
</body>
</html>
)rawliteral";