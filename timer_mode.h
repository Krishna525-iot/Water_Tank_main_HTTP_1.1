#ifndef TIMER_MODE_H
#define TIMER_MODE_H
#include <Arduino.h>

#if defined(ESP8266)
#include <pgmspace.h>

const char timerModeHtml[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<title>Timer Mode</title>
<meta name="viewport" content="width=device-width, initial-scale=1.0"/>
<style>
:root{
  --primary:#0dcaf0;
  --bg:#0f172a;
  --card:#020617;
  --soft:#1e293b;
  --text:#e5e7eb;
  --danger:#ef4444;
  --success:#22c55e;
}
*{box-sizing:border-box}
body{
  margin:0;
  font-family:Inter,Segoe UI,Arial;
  background:#020617;
  color:var(--text);
}
.wrap{
  max-width:720px;
  margin:auto;
  padding:18px;
}
h1{
  text-align:center;
  color:var(--primary);
  margin-bottom:18px;
}
.card{
  background:var(--card);
  border-radius:14px;
  padding:18px;
  margin-bottom:14px;
  box-shadow:0 0 18px rgba(13,202,240,.25);
}
.slot{
  border-left:4px solid var(--primary);
  padding-left:14px;
  margin-bottom:16px;
}
.slot-header{
  display:flex;
  justify-content:space-between;
  align-items:center;
}
.slot-header h3{
  margin:0;
  font-size:18px;
  color:var(--primary);
}
.toggle{
  appearance:none;
  width:44px;
  height:24px;
  background:#334155;
  border-radius:999px;
  position:relative;
  cursor:pointer;
}
.toggle:checked{background:var(--primary)}
.toggle:before{
  content:'';
  position:absolute;
  width:18px;
  height:18px;
  background:#fff;
  border-radius:50%;
  top:3px;
  left:3px;
  transition:.2s;
}
.toggle:checked:before{left:23px}
.days{
  display:flex;
  flex-wrap:wrap;
  gap:6px;
  margin:10px 0;
}
.day{
  padding:6px 10px;
  border-radius:999px;
  background:#1e293b;
  cursor:pointer;
  font-size:13px;
  border:1px solid #334155;
}
.day.active{
  background:var(--primary);
  color:#000;
  font-weight:700;
}
/* ── Time row: two groups side by side ── */
.time-row{
  display:flex;
  gap:16px;
  margin-top:10px;
}
.time-group{
  display:flex;
  flex-direction:column;
  flex:1;
  gap:4px;
}
.time-label{
  font-size:11px;
  font-weight:600;
  letter-spacing:.06em;
  text-transform:uppercase;
  color:var(--primary);
  opacity:.85;
}
.time-group input{
  width:100%;
  padding:10px;
  border-radius:8px;
  border:1px solid #334155;
  background:#020617;
  color:#fff;
  font-size:15px;
}
.time-group input:disabled{
  opacity:.4;
}
button{
  width:100%;
  margin-top:20px;
  padding:14px;
  font-size:16px;
  font-weight:700;
  border:none;
  border-radius:12px;
  cursor:pointer;
  background:linear-gradient(135deg,var(--primary),#0284c7);
  color:#000;
}
button.secondary{
  background:#020617;
  color:var(--primary);
  border:1px solid var(--primary);
  margin-top:10px;
}
.result{
  margin-top:14px;
  padding:12px;
  border-radius:10px;
  font-size:14px;
}
.success{border:1px solid var(--success);color:var(--success)}
.error{border:1px solid var(--danger);color:var(--danger)}
</style>
</head>
<body>
<div class="wrap">
<h1>Timer Mode</h1>
<form id="timerForm"></form>
<button type="submit" form="timerForm">Save Timer Settings</button>
<button type="button" class="secondary" onclick="location.href='/'">Back</button>
<div id="result" class="result"></div>
</div>

<script>
const DAYS=["mon","tue","wed","thu","fri","sat","sun"];
const form=document.getElementById("timerForm");

function makeSlot(n){
  return `
  <div class="card slot">
    <div class="slot-header">
      <h3>Slot ${n}</h3>
      <input type="checkbox" class="toggle" id="en${n}">
    </div>
    <div class="days" id="days${n}">
      ${DAYS.map(d=>`<div class="day" data-d="${d}">${d.toUpperCase()}</div>`).join("")}
    </div>
    <div class="time-row">
      <div class="time-group">
        <span class="time-label">ON Time</span>
        <input type="time" id="on${n}" disabled>
      </div>
      <div class="time-group">
        <span class="time-label">OFF Time</span>
        <input type="time" id="off${n}" disabled>
      </div>
    </div>
  </div>`;
}

for(let i=1;i<=5;i++)
  form.insertAdjacentHTML("beforeend",makeSlot(i));

document.addEventListener("change", function(e){
  if(e.target.classList.contains("toggle")){
    const n = e.target.id.replace("en","");
    document.getElementById(`on${n}`).disabled = !e.target.checked;
    document.getElementById(`off${n}`).disabled = !e.target.checked;
  }
});

document.addEventListener("click", function(e){
  if(e.target.classList.contains("day")){
    e.target.classList.toggle("active");
  }
});

window.onload = function(){
  fetch('/get_timer')
  .then(res => res.json())
  .then(data => {
    for(let n=1;n<=5;n++){
      let slot = data["slot"+n];
      if(!slot) continue;

      if(slot.enabled == 1){
        document.getElementById(`en${n}`).checked = true;
        document.getElementById(`on${n}`).disabled = false;
        document.getElementById(`off${n}`).disabled = false;
      }

      if(slot.on)  document.getElementById(`on${n}`).value = slot.on;
      if(slot.off) document.getElementById(`off${n}`).value = slot.off;

      if(slot.days){
        let arr = slot.days.split(",");
        arr.forEach(d=>{
          let el = document.querySelector(`#days${n} .day[data-d='${d}']`);
          if(el) el.classList.add("active");
        });
      }
    }
  })
  .catch(err => console.log("Timer load error:", err));
};

form.onsubmit = e => {
  e.preventDefault();
  let q = [], any = false, bad = [];

  for (let n = 1; n <= 5; n++) {
    if (!document.getElementById(`en${n}`).checked) continue;

    any = true;
    let days = [...document.querySelectorAll(`#days${n} .active`)]
                .map(d => d.dataset.d);
    let on = document.getElementById(`on${n}`).value;
    let off = document.getElementById(`off${n}`).value;

    if (!days.length || !on || !off) {
      bad.push(n);
      continue;
    }

    q.push(`slot${n}=1`);
    q.push(`days${n}=${days.join(",")}`);
    q.push(`on${n}=${on}`);
    q.push(`off${n}=${off}`);
  }

  if (!any) return show("Enable at least one slot", "error");
  if (bad.length) return show("Incomplete slot(s): " + bad.join(","), "error");

  fetch("/timer/set?" + q.join("&"))
    .then(r => r.text())
    .then(t => show(t, "success"))
    .catch(e => show(e.message, "error"));
};

function show(msg,type){
  let r=document.getElementById("result");
  r.className="result "+type;
  r.innerText=msg;
}
</script>

</body>
</html>
)rawliteral";

#else
const char* timerModeHtml = "";
#endif
#endif