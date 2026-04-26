/*
  ╔═══════════════════════════════════════════════════════════════╗
  ║              AirSense Pro  v2.0                               ║
  ║   ESP32 Environmental Monitor + Cloud + OLED                  ║
  ╠═══════════════════════════════════════════════════════════════╣
  ║  SENSORS   : DHT11, MQ135, MQ7, MQ2                          ║
  ║  DISPLAY   : SSD1306 128x64 OLED (SPI)                       ║
  ║  CLOUD     : Blynk IoT (free tier)                            ║
  ║  LOCAL     : http://192.168.4.1  (AP mode)                   ║
  ╚═══════════════════════════════════════════════════════════════╝

  ── WIRING ────────────────────────────────────────────────────────
   DHT11  DATA  -> GPIO 4   (10k pull-up to 3.3V)
   MQ-135 AOUT  -> GPIO 34  (ADC input)
   MQ-7   AOUT  -> GPIO 35  (ADC input)
   MQ-2   AOUT  -> GPIO 32  (ADC input)
   All sensor VCC -> 5V, GND -> GND

   OLED SSD1306 (SPI) – 128x64
   ┌─────────────┬──────────────┐
   │  OLED Pin   │  ESP32 GPIO  │
   ├─────────────┼──────────────┤
   │  D0 (CLK)   │  GPIO 18     │
   │  D1 (MOSI)  │  GPIO 23     │
   │  RES (RST)  │  GPIO 16     │
   │  DC         │  GPIO 17     │
   │  CS         │  GPIO 5      │
   │  VCC        │  3.3V        │
   │  GND        │  GND         │
   └─────────────┴──────────────┘

  ── LIBRARIES (Arduino Library Manager) ──────────────────────────
   • DHT sensor library        (Adafruit)
   • Adafruit Unified Sensor   (Adafruit)
   • Adafruit SSD1306          (Adafruit)
   • Adafruit GFX Library      (Adafruit)
   • ArduinoJson               (Benoit Blanchon)
   • ESPAsyncWebServer         (Me-No-Dev)
   • AsyncTCP                  (Me-No-Dev)
   • Blynk                     (Volodymyr Shymanskyy)  ← NEW

  ── BLYNK SETUP (FREE, takes 2 min) ─────────────────────────────
   1. Download "Blynk IoT" app (iOS / Android)
   2. Create account → New Template → ESP32 → "AirSense Pro"
   3. Add Datastreams (Virtual Pins, Double type):
        V0 = Temperature (°C)
        V1 = Humidity (%RH)
        V2 = Air Quality MQ135 (ppm)
        V3 = Carbon Monoxide MQ7 (ppm)
        V4 = LPG/Smoke MQ2 (ppm)
   4. Create a Device → copy BLYNK_TEMPLATE_ID, BLYNK_DEVICE_NAME,
      and Auth Token below
   5. Build a Dashboard in the app with Gauge / Chart widgets on V0-V4
   6. Fill your WiFi credentials below → Upload → Done!

  ─────────────────────────────────────────────────────────────────
*/

// ══════════════════════════════════════════════════════════════════
//  BLYNK CONFIG  ← Fill these in before uploading
// ══════════════════════════════════════════════════════════════════
#define BLYNK_TEMPLATE_ID   "TMPLxxxxxxxx"      // from Blynk console
#define BLYNK_DEVICE_NAME   "AirSense Pro"
#define BLYNK_AUTH_TOKEN    "YOUR_AUTH_TOKEN"    // from Blynk console

// ── WiFi for cloud ──────────────────────────────────────────────
#define WIFI_SSID   "Your_WiFi_Name"
#define WIFI_PASS   "Your_WiFi_Password"

// ── AP hotspot (local dashboard, no internet needed) ────────────
#define AP_SSID     "AirSense-Pro"
#define AP_PASS     "airsense123"

// ══════════════════════════════════════════════════════════════════
//  INCLUDES
// ══════════════════════════════════════════════════════════════════
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <DHT.h>
#include <ArduinoJson.h>
#include <BlynkSimpleEsp32.h>   // Blynk cloud
#include <Wire.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// ══════════════════════════════════════════════════════════════════
//  PIN DEFINITIONS
// ══════════════════════════════════════════════════════════════════
#define DHT_PIN     4
#define DHT_TYPE    DHT11
#define MQ135_PIN   34
#define MQ7_PIN     35
#define MQ2_PIN     32

// OLED SPI pins
#define OLED_CLK    18    // D0 – SPI clock  (VSPI SCK)
#define OLED_MOSI   23    // D1 – SPI data   (VSPI MOSI)
#define OLED_RES    16    // RES – Reset
#define OLED_DC     17    // DC  – Data/Command select
#define OLED_CS     27     // CS  – Chip select

#define SCREEN_W    128
#define SCREEN_H    64

// ══════════════════════════════════════════════════════════════════
//  GLOBALS
// ══════════════════════════════════════════════════════════════════
DHT dht(DHT_PIN, DHT_TYPE);

// Hardware SPI constructor: (W, H, &SPI, DC, RST, CS)
Adafruit_SSD1306 oled(SCREEN_W, SCREEN_H, &SPI, OLED_DC, OLED_RES, OLED_CS);

AsyncWebServer server(80);

float g_temp   = 0;
float g_humid  = 0;
float g_mq135  = 0;
float g_mq7    = 0;
float g_mq2    = 0;

// Sensor Ro (clean-air resistance, kΩ) – calibrate if needed
float Ro135 = 10.0f, Ro7 = 10.0f, Ro2 = 9.83f;

// OLED page cycling
uint8_t  oledPage    = 0;          // 0-3
uint32_t lastOledSwitch = 0;
#define  OLED_PAGE_MS 3000         // rotate every 3 s

// Blynk cloud push interval
uint32_t lastBlynkPush = 0;
#define  BLYNK_PUSH_MS 5000        // push every 5 s

// Sensor read interval
uint32_t lastSensorRead = 0;
#define  SENSOR_READ_MS 2000

// ══════════════════════════════════════════════════════════════════
//  HTML DASHBOARD (same cyber UI, unchanged)
// ══════════════════════════════════════════════════════════════════
const char PAGE[] PROGMEM = R"rawhtml(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>AirSense Pro</title>
<style>
:root{
  --bg:#050a0f;--panel:rgba(10,20,35,0.9);--border:rgba(0,200,255,0.15);
  --bordhi:rgba(0,200,255,0.5);--cyan:#00c8ff;--green:#00ff9d;
  --yellow:#ffe600;--orange:#ff8c00;--red:#ff2d55;--txt:#c8e8ff;--dim:rgba(200,232,255,0.45);
}
*{box-sizing:border-box;margin:0;padding:0}
body{background:var(--bg);color:var(--txt);font-family:'Segoe UI',sans-serif;min-height:100vh}
body::before{content:'';position:fixed;inset:0;
  background-image:linear-gradient(rgba(0,200,255,0.04) 1px,transparent 1px),
  linear-gradient(90deg,rgba(0,200,255,0.04) 1px,transparent 1px);
  background-size:40px 40px;pointer-events:none;z-index:0}
.app{position:relative;z-index:1;max-width:1300px;margin:0 auto;padding:0 16px 40px}
header{display:flex;align-items:center;justify-content:space-between;
  padding:20px 0 16px;border-bottom:1px solid var(--border);margin-bottom:24px;flex-wrap:wrap;gap:10px}
.logo-title{font-size:22px;font-weight:900;letter-spacing:4px;
  background:linear-gradient(90deg,var(--cyan),#6af0ff);
  -webkit-background-clip:text;-webkit-text-fill-color:transparent;
  filter:drop-shadow(0 0 10px rgba(0,200,255,0.5))}
.logo-sub{font-size:10px;letter-spacing:3px;color:var(--dim);margin-top:3px;font-family:monospace}
.hdr-right{display:flex;align-items:center;gap:16px}
.dot{width:9px;height:9px;border-radius:50%;background:var(--green);
  box-shadow:0 0 8px var(--green);display:inline-block;margin-right:6px;animation:blink 2s infinite}
.dot.off{background:var(--red);box-shadow:0 0 8px var(--red);animation:none}
@keyframes blink{0%,100%{opacity:1}50%{opacity:0.5}}
#connTxt{font-family:monospace;font-size:12px;color:var(--dim)}
.badge{font-family:monospace;font-size:11px;padding:5px 12px;
  border:1px solid var(--bordhi);border-radius:4px;color:var(--cyan);background:rgba(0,200,255,0.05)}
.alert{display:none;align-items:center;gap:12px;padding:14px 20px;
  border-radius:6px;border:1px solid var(--red);background:rgba(255,45,85,0.08);
  margin-bottom:20px;font-family:monospace;font-size:12px;animation:aflash 1s infinite}
.alert.on{display:flex}
@keyframes aflash{0%,100%{border-color:var(--red)}50%{border-color:rgba(255,45,85,0.3)}}
.alert-txt{flex:1;color:var(--red)}
.alert-x{cursor:pointer;color:var(--dim);font-size:18px;background:none;border:none;color:var(--dim)}
.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(270px,1fr));gap:18px;margin-bottom:22px}
.card{background:var(--panel);border:1px solid var(--border);border-radius:10px;
  padding:22px;position:relative;overflow:hidden;backdrop-filter:blur(8px);
  transition:border-color .3s,box-shadow .3s}
.card::before{content:'';position:absolute;top:0;left:0;right:0;height:2px;
  background:linear-gradient(90deg,transparent,var(--ac,var(--cyan)),transparent);opacity:.7}
.card[data-s="temp"]{--ac:#ff6b6b}.card[data-s="hum"]{--ac:var(--cyan)}
.card[data-s="mq135"]{--ac:var(--green)}.card[data-s="mq7"]{--ac:#ffb347}.card[data-s="mq2"]{--ac:#da70d6}
.card.danger{border-color:rgba(255,45,85,.5);animation:cdanger 2s infinite}
.card.warn{border-color:rgba(255,140,0,.5)}
@keyframes cdanger{0%,100%{box-shadow:0 0 30px rgba(255,45,85,.1)}50%{box-shadow:0 0 50px rgba(255,45,85,.25)}}
.chdr{display:flex;justify-content:space-between;align-items:flex-start;margin-bottom:16px}
.sname{font-size:10px;letter-spacing:3px;color:var(--dim);font-family:monospace;text-transform:uppercase}
.stitle{font-size:17px;font-weight:600;margin-top:3px}
.icon{width:40px;height:40px;border-radius:8px;display:flex;align-items:center;
  justify-content:center;font-size:18px;border:1px solid var(--border);background:rgba(0,200,255,0.07)}
.valrow{display:flex;align-items:baseline;gap:6px;margin-bottom:4px}
.val{font-size:50px;font-weight:700;line-height:1;color:var(--ac,var(--cyan));
  filter:drop-shadow(0 0 10px var(--ac,var(--cyan)));font-variant-numeric:tabular-nums;
  font-family:monospace}
.unit{font-family:monospace;font-size:13px;color:var(--dim)}
.vlabel{font-family:monospace;font-size:10px;color:var(--dim);letter-spacing:1px;
  margin-bottom:14px;min-height:14px;text-transform:uppercase}
.sbadge{display:inline-flex;align-items:center;gap:6px;padding:4px 10px;
  border-radius:20px;font-family:monospace;font-size:10px;letter-spacing:1px;
  text-transform:uppercase;margin-bottom:16px;transition:all .3s}
.sbadge.good{background:rgba(0,255,157,.1);border:1px solid rgba(0,255,157,.3);color:var(--green)}
.sbadge.warn{background:rgba(255,230,0,.1);border:1px solid rgba(255,230,0,.3);color:var(--yellow)}
.sbadge.danger{background:rgba(255,45,85,.1);border:1px solid rgba(255,45,85,.4);color:var(--red);animation:blink 1s infinite}
.bdot{width:6px;height:6px;border-radius:50%;background:currentColor}
.gtrack{height:6px;border-radius:3px;background:rgba(255,255,255,.06);overflow:hidden;margin-top:6px}
.gfill{height:100%;border-radius:3px;transition:width .8s cubic-bezier(.4,0,.2,1);
  background:linear-gradient(90deg,var(--green),var(--yellow),var(--orange),var(--red));
  background-size:400% 100%}
.glabels{display:flex;justify-content:space-between;font-family:monospace;font-size:10px;color:var(--dim)}
.hist{background:var(--panel);border:1px solid var(--border);border-radius:10px;
  padding:22px;margin-bottom:22px;backdrop-filter:blur(8px)}
.sechdr{display:flex;align-items:center;justify-content:space-between;margin-bottom:18px;flex-wrap:wrap;gap:8px}
.sectitle{font-size:12px;letter-spacing:3px;color:var(--cyan);text-transform:uppercase;font-family:monospace}
.tabs{display:flex;gap:4px}
.tab{padding:5px 11px;border-radius:4px;border:1px solid var(--border);background:transparent;
  color:var(--dim);font-family:monospace;font-size:10px;cursor:pointer;transition:all .2s;letter-spacing:1px}
.tab.active,.tab:hover{border-color:var(--cyan);color:var(--cyan);background:rgba(0,200,255,.07)}
.ccanvas{position:relative;height:170px}
#hchart{width:100%!important;height:170px!important}
.thgrid{display:grid;grid-template-columns:repeat(auto-fit,minmax(240px,1fr));gap:14px;margin-bottom:22px}
.thcard{background:var(--panel);border:1px solid var(--border);border-radius:10px;
  padding:18px;backdrop-filter:blur(8px)}
.thtitle{font-family:monospace;font-size:10px;letter-spacing:2px;color:var(--dim);
  text-transform:uppercase;margin-bottom:14px}
.throw{display:flex;align-items:center;justify-content:space-between;margin-bottom:9px;gap:8px}
.throw label{font-family:monospace;font-size:10px;color:var(--dim);min-width:85px}
.throw input{flex:1;background:rgba(0,200,255,.05);border:1px solid var(--border);
  border-radius:4px;color:var(--cyan);font-family:monospace;font-size:12px;
  padding:4px 8px;text-align:right;outline:none;max-width:90px}
.throw input:focus{border-color:var(--cyan)}
.throw .u{font-family:monospace;font-size:10px;color:var(--dim);min-width:26px}
footer{border-top:1px solid var(--border);padding-top:14px;
  display:flex;align-items:center;justify-content:space-between;flex-wrap:wrap;gap:10px}
.finfo{font-family:monospace;font-size:10px;color:var(--dim);letter-spacing:1px}
.finfo span{color:var(--cyan)}
.rbtn{padding:7px 18px;border:1px solid var(--bordhi);border-radius:4px;
  background:rgba(0,200,255,.06);color:var(--cyan);font-family:monospace;
  font-size:11px;cursor:pointer;letter-spacing:1px;transition:all .2s}
.rbtn:hover{background:rgba(0,200,255,.15);box-shadow:0 0 16px rgba(0,200,255,.2)}
</style>
</head>
<body>
<div class="app">
<header>
  <div>
    <div class="logo-title">AIRSENSE PRO</div>
    <div class="logo-sub">ESP32 / DHT11 / MQ135 / MQ7 / MQ2 / BLYNK CLOUD</div>
  </div>
  <div class="hdr-right">
    <div><span class="dot" id="dot"></span><span id="connTxt">CONNECTING</span></div>
    <div class="badge">192.168.4.1</div>
  </div>
</header>
<div class="alert" id="alertBox">
  <span class="alert-txt" id="alertMsg">DANGER THRESHOLD EXCEEDED!</span>
  <button class="alert-x" onclick="document.getElementById('alertBox').classList.remove('on')">X</button>
</div>
<div class="grid">
  <div class="card" data-s="temp" id="c-temp">
    <div class="chdr"><div><div class="sname">DHT11</div><div class="stitle">Temperature</div></div><div class="icon">TMP</div></div>
    <div class="valrow"><span class="val" id="v-temp">--</span><span class="unit">C</span></div>
    <div class="vlabel">Ambient Temperature</div>
    <div class="sbadge good" id="b-temp"><span class="bdot"></span><span>NORMAL</span></div>
    <div class="glabels"><span>0</span><span>50C</span></div>
    <div class="gtrack"><div class="gfill" id="g-temp" style="width:0%"></div></div>
  </div>
  <div class="card" data-s="hum" id="c-hum">
    <div class="chdr"><div><div class="sname">DHT11</div><div class="stitle">Humidity</div></div><div class="icon">HUM</div></div>
    <div class="valrow"><span class="val" id="v-hum">--</span><span class="unit">%RH</span></div>
    <div class="vlabel">Relative Humidity</div>
    <div class="sbadge good" id="b-hum"><span class="bdot"></span><span>NORMAL</span></div>
    <div class="glabels"><span>0</span><span>100%</span></div>
    <div class="gtrack"><div class="gfill" id="g-hum" style="width:0%"></div></div>
  </div>
  <div class="card" data-s="mq135" id="c-mq135">
    <div class="chdr"><div><div class="sname">MQ-135</div><div class="stitle">Air Quality</div></div><div class="icon">AIR</div></div>
    <div class="valrow"><span class="val" id="v-mq135">--</span><span class="unit">ppm</span></div>
    <div class="vlabel">CO2 / NH3 / Benzene</div>
    <div class="sbadge good" id="b-mq135"><span class="bdot"></span><span>CLEAN</span></div>
    <div class="glabels"><span>0</span><span>1000</span></div>
    <div class="gtrack"><div class="gfill" id="g-mq135" style="width:0%"></div></div>
  </div>
  <div class="card" data-s="mq7" id="c-mq7">
    <div class="chdr"><div><div class="sname">MQ-7</div><div class="stitle">Carbon Monoxide</div></div><div class="icon">CO</div></div>
    <div class="valrow"><span class="val" id="v-mq7">--</span><span class="unit">ppm</span></div>
    <div class="vlabel">CO Concentration</div>
    <div class="sbadge good" id="b-mq7"><span class="bdot"></span><span>SAFE</span></div>
    <div class="glabels"><span>0</span><span>200</span></div>
    <div class="gtrack"><div class="gfill" id="g-mq7" style="width:0%"></div></div>
  </div>
  <div class="card" data-s="mq2" id="c-mq2">
    <div class="chdr"><div><div class="sname">MQ-2</div><div class="stitle">LPG / Smoke</div></div><div class="icon">GAS</div></div>
    <div class="valrow"><span class="val" id="v-mq2">--</span><span class="unit">ppm</span></div>
    <div class="vlabel">LPG / Smoke / H2</div>
    <div class="sbadge good" id="b-mq2"><span class="bdot"></span><span>SAFE</span></div>
    <div class="glabels"><span>0</span><span>10000</span></div>
    <div class="gtrack"><div class="gfill" id="g-mq2" style="width:0%"></div></div>
  </div>
</div>
<div class="hist">
  <div class="sechdr">
    <div class="sectitle">TREND HISTORY</div>
    <div class="tabs">
      <button class="tab active" onclick="sw('temp',this)">TEMP</button>
      <button class="tab" onclick="sw('hum',this)">HUMID</button>
      <button class="tab" onclick="sw('mq135',this)">MQ135</button>
      <button class="tab" onclick="sw('mq7',this)">MQ7</button>
      <button class="tab" onclick="sw('mq2',this)">MQ2</button>
    </div>
  </div>
  <div class="ccanvas"><canvas id="hchart"></canvas></div>
</div>
<div class="thgrid">
  <div class="thcard"><div class="thtitle">Temperature Thresholds</div>
    <div class="throw"><label>WARN above</label><input type="number" id="tw-temp" value="35"><span class="u">C</span></div>
    <div class="throw"><label>DANGER above</label><input type="number" id="td-temp" value="45"><span class="u">C</span></div></div>
  <div class="thcard"><div class="thtitle">Humidity Thresholds</div>
    <div class="throw"><label>WARN above</label><input type="number" id="tw-hum" value="70"><span class="u">%</span></div>
    <div class="throw"><label>DANGER above</label><input type="number" id="td-hum" value="90"><span class="u">%</span></div></div>
  <div class="thcard"><div class="thtitle">MQ-135 Thresholds</div>
    <div class="throw"><label>WARN above</label><input type="number" id="tw-mq135" value="400"><span class="u">ppm</span></div>
    <div class="throw"><label>DANGER above</label><input type="number" id="td-mq135" value="700"><span class="u">ppm</span></div></div>
  <div class="thcard"><div class="thtitle">MQ-7 CO Thresholds</div>
    <div class="throw"><label>WARN above</label><input type="number" id="tw-mq7" value="50"><span class="u">ppm</span></div>
    <div class="throw"><label>DANGER above</label><input type="number" id="td-mq7" value="100"><span class="u">ppm</span></div></div>
  <div class="thcard"><div class="thtitle">MQ-2 LPG Thresholds</div>
    <div class="throw"><label>WARN above</label><input type="number" id="tw-mq2" value="1000"><span class="u">ppm</span></div>
    <div class="throw"><label>DANGER above</label><input type="number" id="td-mq2" value="5000"><span class="u">ppm</span></div></div>
</div>
<footer>
  <div class="finfo">
    UPDATE: <span id="ts">--:--:--</span> &nbsp;|&nbsp;
    UPTIME: <span id="upt">00:00:00</span> &nbsp;|&nbsp;
    READS: <span id="rc">0</span> &nbsp;|&nbsp;
    CLOUD: <span id="cld">BLYNK</span>
  </div>
  <button class="rbtn" onclick="poll()">REFRESH</button>
</footer>
</div>
<script>
var API='http://192.168.4.1/data',INTERVAL=2000;
var hist={temp:[],hum:[],mq135:[],mq7:[],mq2:[],lbl:[]};
var MAX=60,rc=0,t0=Date.now(),cur='temp',chart=null;
var COLORS={
  temp:{b:'#ff6b6b',bg:'rgba(255,107,107,0.08)'},
  hum:{b:'#00c8ff',bg:'rgba(0,200,255,0.08)'},
  mq135:{b:'#00ff9d',bg:'rgba(0,255,157,0.08)'},
  mq7:{b:'#ffb347',bg:'rgba(255,179,71,0.08)'},
  mq2:{b:'#da70d6',bg:'rgba(218,112,214,0.08)'}
};
function buildChart(){
  var ctx=document.getElementById('hchart').getContext('2d');
  if(chart)chart.destroy();
  var c=COLORS[cur];
  chart=new Chart(ctx,{type:'line',data:{labels:hist.lbl.slice(),datasets:[{data:hist[cur].slice(),
    borderColor:c.b,backgroundColor:c.bg,borderWidth:2,pointRadius:2,tension:0.4,fill:true}]},
    options:{responsive:true,maintainAspectRatio:false,animation:{duration:300},
    plugins:{legend:{display:false},tooltip:{backgroundColor:'rgba(5,10,15,0.95)',
      borderColor:c.b,borderWidth:1,titleColor:c.b,bodyColor:'#c8e8ff',
      titleFont:{family:'monospace',size:10},bodyFont:{family:'monospace',size:11}}},
    scales:{x:{grid:{color:'rgba(0,200,255,0.05)'},ticks:{color:'rgba(200,232,255,0.3)',font:{size:9},maxTicksLimit:8}},
      y:{grid:{color:'rgba(0,200,255,0.05)'},ticks:{color:'rgba(200,232,255,0.3)',font:{size:9}}}}}});
}
function sw(k,btn){cur=k;document.querySelectorAll('.tab').forEach(function(t){t.classList.remove('active')});btn.classList.add('active');buildChart();}
function updateChart(){if(!chart)return;chart.data.labels=hist.lbl.slice();chart.data.datasets[0].data=hist[cur].slice();chart.update('none');}
function th(id){return parseFloat(document.getElementById(id).value)||0;}
function updateCard(s,val,min,max){
  if(isNaN(val))return;
  var el=document.getElementById('v-'+s);
  el.textContent=val.toFixed(val%1!==0?1:0);
  document.getElementById('g-'+s).style.width=Math.min(100,Math.max(0,((val-min)/(max-min))*100))+'%';
  var w=th('tw-'+s),d=th('td-'+s);
  var card=document.getElementById('c-'+s),badge=document.getElementById('b-'+s);
  card.classList.remove('danger','warn');
  if(val>=d){badge.className='sbadge danger';badge.innerHTML='<span class="bdot"></span><span>DANGER</span>';card.classList.add('danger');showAlert(s,val);}
  else if(val>=w){badge.className='sbadge warn';badge.innerHTML='<span class="bdot"></span><span>WARNING</span>';card.classList.add('warn');}
  else{badge.className='sbadge good';var lbl=(s==='mq135')?'CLEAN':(s==='mq7'||s==='mq2')?'SAFE':'NORMAL';badge.innerHTML='<span class="bdot"></span><span>'+lbl+'</span>';}
}
var alerts=[];
function showAlert(s,val){
  if(alerts.indexOf(s)<0)alerts.push(s);
  var names={temp:'Temperature',hum:'Humidity',mq135:'Air Quality',mq7:'Carbon Monoxide',mq2:'LPG/Smoke'};
  document.getElementById('alertMsg').textContent='DANGER: '+alerts.map(function(x){return names[x]}).join(', ')+' exceeded threshold!';
  document.getElementById('alertBox').classList.add('on');
}
function setConn(ok){
  document.getElementById('dot').className='dot'+(ok?'':' off');
  document.getElementById('connTxt').textContent=ok?'LIVE / ESP32':'DEMO MODE';
}
function poll(){
  fetch(API,{signal:AbortSignal.timeout(3000)})
    .then(function(r){return r.json()})
    .then(function(d){
      setConn(true);alerts=[];
      updateCard('temp',d.temperature,0,50);updateCard('hum',d.humidity,0,100);
      updateCard('mq135',d.mq135,0,1000);updateCard('mq7',d.mq7,0,200);updateCard('mq2',d.mq2,0,10000);
      pushHistory(d.temperature,d.humidity,d.mq135,d.mq7,d.mq2);
    })
    .catch(function(){
      setConn(false);
      var d={temperature:+(20+Math.random()*20).toFixed(1),humidity:+(40+Math.random()*40).toFixed(1),
        mq135:+(100+Math.random()*600)|0,mq7:+(10+Math.random()*120)|0,mq2:+(200+Math.random()*4000)|0};
      alerts=[];
      updateCard('temp',d.temperature,0,50);updateCard('hum',d.humidity,0,100);
      updateCard('mq135',d.mq135,0,1000);updateCard('mq7',d.mq7,0,200);updateCard('mq2',d.mq2,0,10000);
      pushHistory(d.temperature,d.humidity,d.mq135,d.mq7,d.mq2);
    });
}
function pushHistory(t,h,a,co,g){
  var ts=new Date().toLocaleTimeString('en',{hour12:false,hour:'2-digit',minute:'2-digit',second:'2-digit'});
  hist.temp.push(t);hist.hum.push(h);hist.mq135.push(a);hist.mq7.push(co);hist.mq2.push(g);hist.lbl.push(ts);
  ['temp','hum','mq135','mq7','mq2','lbl'].forEach(function(k){if(hist[k].length>MAX)hist[k].shift()});
  updateChart();rc++;
  document.getElementById('rc').textContent=rc;
  document.getElementById('ts').textContent=ts;
  var e=Math.floor((Date.now()-t0)/1000);
  document.getElementById('upt').textContent=String(Math.floor(e/3600)).padStart(2,'0')+':'+String(Math.floor((e%3600)/60)).padStart(2,'0')+':'+String(e%60).padStart(2,'0');
}
var s=document.createElement('script');
s.src='https://cdn.jsdelivr.net/npm/chart.js@4.4.0/dist/chart.umd.min.js';
s.onload=function(){buildChart();poll();setInterval(poll,INTERVAL);};
document.head.appendChild(s);
</script>
</body>
</html>
)rawhtml";

// ══════════════════════════════════════════════════════════════════
//  OLED HELPERS
// ══════════════════════════════════════════════════════════════════

// Draw the AirSense Pro logo/splash screen
void oledSplash() {
  oled.clearDisplay();

  // Top decorative line
  oled.drawFastHLine(0, 0, 128, SSD1306_WHITE);
  oled.drawFastHLine(0, 1, 128, SSD1306_WHITE);

  // "AIRSENSE" big text
  oled.setTextSize(2);
  oled.setTextColor(SSD1306_WHITE);
  oled.setCursor(4, 6);
  oled.print(F("AIRSENSE"));

  // "PRO" right-aligned accent
  oled.setTextSize(2);
  oled.setCursor(88, 6);
  oled.print(F("PRO"));

  // Thin separator line
  oled.drawFastHLine(0, 27, 128, SSD1306_WHITE);

  // Subtitle
  oled.setTextSize(1);
  oled.setCursor(10, 31);
  oled.print(F("ESP32 AIR MONITOR v2"));

  // Animated dots row
  oled.setCursor(22, 43);
  oled.print(F("[ BOOTING... ]"));

  // Bottom sensor tags
  oled.setCursor(0, 56);
  oled.print(F("DHT11 MQ135 MQ7 MQ2"));

  // Bottom border
  oled.drawFastHLine(0, 63, 128, SSD1306_WHITE);

  oled.display();
}

// Animated boot progress bar
void oledBootProgress(int pct, const char* msg) {
  oled.clearDisplay();
  oled.drawFastHLine(0, 0, 128, SSD1306_WHITE);
  oled.drawFastHLine(0, 1, 128, SSD1306_WHITE);

  oled.setTextSize(2);
  oled.setTextColor(SSD1306_WHITE);
  oled.setCursor(4, 6);
  oled.print(F("AIRSENSE"));
  oled.setCursor(88, 6);
  oled.print(F("PRO"));

  oled.drawFastHLine(0, 27, 128, SSD1306_WHITE);

  oled.setTextSize(1);
  oled.setCursor(0, 32);
  oled.print(msg);

  // Progress bar
  oled.drawRect(0, 46, 128, 10, SSD1306_WHITE);
  int fill = map(pct, 0, 100, 2, 126);
  if (fill > 2) oled.fillRect(1, 47, fill, 8, SSD1306_WHITE);

  // Percentage
  oled.setCursor(50, 56);
  oled.print(pct);
  oled.print(F("%"));

  oled.display();
}

// Page 0: Temperature + Humidity
void oledPageTH() {
  oled.clearDisplay();
  oled.drawFastHLine(0, 0, 128, SSD1306_WHITE);

  oled.setTextSize(1);
  oled.setCursor(0, 3);
  oled.print(F("TEMP & HUMIDITY"));

  oled.drawFastHLine(0, 13, 128, SSD1306_WHITE);

  // Temperature large
  oled.setTextSize(3);
  oled.setCursor(0, 18);
  oled.print(g_temp, 1);
  oled.setTextSize(1);
  oled.setCursor(95, 18);
  oled.print(F("*C"));

  // Divider
  oled.drawFastVLine(63, 40, 22, SSD1306_WHITE);

  // Humidity
  oled.setTextSize(2);
  oled.setCursor(0, 42);
  oled.print(g_humid, 0);
  oled.setTextSize(1);
  oled.setCursor(35, 42);
  oled.print(F("%RH"));

  oled.setCursor(68, 42);
  oled.print(F("HUMID"));

  oled.drawFastHLine(0, 63, 128, SSD1306_WHITE);
  oled.display();
}

// Page 1: MQ135 Air Quality
void oledPageMQ135() {
  oled.clearDisplay();
  oled.drawFastHLine(0, 0, 128, SSD1306_WHITE);

  oled.setTextSize(1);
  oled.setCursor(0, 3);
  oled.print(F("AIR QUALITY MQ-135"));
  oled.drawFastHLine(0, 13, 128, SSD1306_WHITE);

  oled.setTextSize(3);
  oled.setCursor(0, 18);
  oled.print((int)g_mq135);
  oled.setTextSize(1);
  oled.setCursor(85, 18);
  oled.print(F("ppm"));

  oled.setCursor(0, 44);
  oled.print(F("CO2/NH3/Benzene"));

  // Status
  oled.setCursor(0, 54);
  if (g_mq135 > 700)      oled.print(F("STATUS: DANGER!"));
  else if (g_mq135 > 400) oled.print(F("STATUS: WARNING"));
  else                     oled.print(F("STATUS: CLEAN"));

  oled.drawFastHLine(0, 63, 128, SSD1306_WHITE);
  oled.display();
}

// Page 2: MQ7 CO
void oledPageMQ7() {
  oled.clearDisplay();
  oled.drawFastHLine(0, 0, 128, SSD1306_WHITE);

  oled.setTextSize(1);
  oled.setCursor(0, 3);
  oled.print(F("CARBON MONOXIDE MQ-7"));
  oled.drawFastHLine(0, 13, 128, SSD1306_WHITE);

  oled.setTextSize(3);
  oled.setCursor(0, 18);
  oled.print((int)g_mq7);
  oled.setTextSize(1);
  oled.setCursor(85, 18);
  oled.print(F("ppm"));
  oled.setCursor(85, 28);
  oled.print(F("CO"));

  oled.setCursor(0, 44);
  oled.print(F("CO Concentration"));

  oled.setCursor(0, 54);
  if (g_mq7 > 100)      oled.print(F("STATUS: DANGER!"));
  else if (g_mq7 > 50)  oled.print(F("STATUS: WARNING"));
  else                   oled.print(F("STATUS: SAFE"));

  oled.drawFastHLine(0, 63, 128, SSD1306_WHITE);
  oled.display();
}

// Page 3: MQ2 LPG/Smoke
void oledPageMQ2() {
  oled.clearDisplay();
  oled.drawFastHLine(0, 0, 128, SSD1306_WHITE);

  oled.setTextSize(1);
  oled.setCursor(0, 3);
  oled.print(F("LPG / SMOKE  MQ-2"));
  oled.drawFastHLine(0, 13, 128, SSD1306_WHITE);

  oled.setTextSize(3);
  oled.setCursor(0, 18);
  oled.print((int)g_mq2);
  oled.setTextSize(1);
  oled.setCursor(85, 18);
  oled.print(F("ppm"));
  oled.setCursor(85, 28);
  oled.print(F("LPG"));

  oled.setCursor(0, 44);
  oled.print(F("LPG / Smoke / H2"));

  oled.setCursor(0, 54);
  if (g_mq2 > 5000)       oled.print(F("STATUS: DANGER!"));
  else if (g_mq2 > 1000)  oled.print(F("STATUS: WARNING"));
  else                     oled.print(F("STATUS: SAFE"));

  oled.drawFastHLine(0, 63, 128, SSD1306_WHITE);
  oled.display();
}

// Dispatch current page
void oledUpdate() {
  switch (oledPage) {
    case 0: oledPageTH();    break;
    case 1: oledPageMQ135(); break;
    case 2: oledPageMQ7();   break;
    case 3: oledPageMQ2();   break;
  }
}

// ══════════════════════════════════════════════════════════════════
//  MQ SENSOR MATH
// ══════════════════════════════════════════════════════════════════
float readAvg(int pin) {
  long sum = 0;
  for (int i = 0; i < 8; i++) { sum += analogRead(pin); delay(2); }
  return sum / 8.0f;
}

float toRs(float raw, float RL) {
  float v = raw * (3.3f / 4095.0f);
  if (v < 0.01f) return 999.0f;
  return RL * (3.3f - v) / v;
}

float mq135ppm(float raw) { return constrain(116.6f * pow(toRs(raw,20.0f)/Ro135, -2.769f), 0, 5000); }
float mq7ppm  (float raw) { return constrain(99.0f  * pow(toRs(raw,10.0f)/Ro7,   -1.518f), 0, 1000); }
float mq2ppm  (float raw) { return constrain(574.25f* pow(toRs(raw,20.0f)/Ro2,   -2.222f), 0, 20000);}

// ══════════════════════════════════════════════════════════════════
//  SENSOR READ
// ══════════════════════════════════════════════════════════════════
void readSensors() {
  float t = dht.readTemperature();
  float h = dht.readHumidity();
  if (!isnan(t)) g_temp  = t;
  if (!isnan(h)) g_humid = h;
  g_mq135 = mq135ppm(readAvg(MQ135_PIN));
  g_mq7   = mq7ppm  (readAvg(MQ7_PIN));
  g_mq2   = mq2ppm  (readAvg(MQ2_PIN));
}

// ══════════════════════════════════════════════════════════════════
//  BLYNK PUSH  (virtual pins V0-V4)
// ══════════════════════════════════════════════════════════════════
void pushToBlynk() {
  if (Blynk.connected()) {
    Blynk.virtualWrite(V0, g_temp);
    Blynk.virtualWrite(V1, g_humid);
    Blynk.virtualWrite(V2, (int)g_mq135);
    Blynk.virtualWrite(V3, (int)g_mq7);
    Blynk.virtualWrite(V4, (int)g_mq2);
    Serial.println("[BLYNK] Data pushed to cloud.");
  }
}

// ══════════════════════════════════════════════════════════════════
//  SETUP
// ══════════════════════════════════════════════════════════════════
void setup() {
  Serial.begin(115200);
  Serial.println("\n╔══════════════════════════╗");
  Serial.println("║   AirSense Pro  v2.0     ║");
  Serial.println("╚══════════════════════════╝");

  // ── OLED init ───────────────────────────────────────────────
  SPI.begin(OLED_CLK, -1, OLED_MOSI, OLED_CS);
  if (!oled.begin(SSD1306_SWITCHCAPVCC)) {
    Serial.println("[OLED] FAIL – check wiring");
    // Continue without OLED
  } else {
    Serial.println("[OLED] OK");
    oled.setRotation(0);
    oledSplash();
    delay(2000);
  }

  // ── DHT + ADC ───────────────────────────────────────────────
  dht.begin();
  analogSetAttenuation(ADC_11db);
  analogSetWidth(12);

  // ── MQ warm-up with progress bar ────────────────────────────
  Serial.println("[MQ] Warming up sensors (20s)...");
  for (int i = 20; i >= 0; i--) {
    int pct = map(20 - i, 0, 20, 0, 100);
    char msg[22];
    snprintf(msg, sizeof(msg), "MQ WARMUP... %2ds left ", i);
    oledBootProgress(pct, msg);
    Serial.printf("  %ds\n", i);
    delay(1000);
  }

  // ── WiFi: connect to router for cloud ───────────────────────
  oledBootProgress(60, "CONNECTING WiFi...");
  Serial.printf("[WiFi] Connecting to: %s\n", WIFI_SSID);

  // Start AP first (always available)
  WiFi.mode(WIFI_AP_STA);   // ← AP + Station simultaneously
  WiFi.softAP(AP_SSID, AP_PASS);
  WiFi.softAPConfig(
    IPAddress(192,168,4,1),
    IPAddress(192,168,4,1),
    IPAddress(255,255,255,0)
  );
  Serial.printf("[AP] Started: %s | IP: %s\n", AP_SSID, WiFi.softAPIP().toString().c_str());

  // Try connecting to internet WiFi (non-blocking, 10s timeout)
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  int wTries = 20;
  while (WiFi.status() != WL_CONNECTED && wTries-- > 0) {
    delay(500); Serial.print('.');
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("[WiFi] Connected! IP: %s\n", WiFi.localIP().toString().c_str());
    oledBootProgress(80, "WiFi OK! Starting Blynk");

    // ── Blynk init ──────────────────────────────────────────
    Blynk.config(BLYNK_AUTH_TOKEN);
    Blynk.connect(3000);
    if (Blynk.connected()) {
      Serial.println("[BLYNK] Connected to cloud!");
      oledBootProgress(90, "BLYNK CLOUD CONNECTED!");
    } else {
      Serial.println("[BLYNK] Cloud not reached, offline mode.");
      oledBootProgress(90, "BLYNK OFFLINE - LOCAL OK");
    }
  } else {
    Serial.println("[WiFi] Not connected – cloud disabled, local AP active.");
    oledBootProgress(80, "NO WIFI - AP MODE ONLY");
  }
  delay(1200);

  // ── Web server ──────────────────────────────────────────────
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *req) {
    req->send_P(200, "text/html", PAGE);
  });

  server.on("/data", HTTP_GET, [](AsyncWebServerRequest *req) {
    StaticJsonDocument<256> doc;
    doc["temperature"] = round(g_temp  * 10.0f) / 10.0f;
    doc["humidity"]    = round(g_humid * 10.0f) / 10.0f;
    doc["mq135"]       = (int)round(g_mq135);
    doc["mq7"]         = (int)round(g_mq7);
    doc["mq2"]         = (int)round(g_mq2);
    doc["uptime"]      = millis();
    doc["cloud"]       = Blynk.connected() ? "online" : "offline";
    String json;
    serializeJson(doc, json);
    AsyncWebServerResponse* r = req->beginResponse(200, "application/json", json);
    r->addHeader("Access-Control-Allow-Origin", "*");
    r->addHeader("Cache-Control", "no-cache");
    req->send(r);
    Serial.printf("[DATA] T=%.1f H=%.0f AQ=%d CO=%d GAS=%d BLYNK=%s\n",
      g_temp, g_humid, (int)g_mq135, (int)g_mq7, (int)g_mq2,
      Blynk.connected() ? "UP" : "DOWN");
  });

  server.onNotFound([](AsyncWebServerRequest *req){
    req->send(404, "text/plain", "Not found");
  });

  server.begin();

  oledBootProgress(100, "AIRSENSE PRO READY!");
  delay(1200);

  Serial.println("[READY] AirSense Pro running.");
  Serial.printf("  Local dashboard : http://192.168.4.1\n");
  Serial.printf("  Cloud (Blynk)   : %s\n", Blynk.connected() ? "CONNECTED" : "OFFLINE");
}

// ══════════════════════════════════════════════════════════════════
//  LOOP
// ══════════════════════════════════════════════════════════════════
void loop() {
  // Blynk keep-alive (non-blocking)
  if (Blynk.connected()) Blynk.run();

  uint32_t now = millis();

  // Read sensors every 2 s
  if (now - lastSensorRead >= SENSOR_READ_MS) {
    lastSensorRead = now;
    readSensors();
  }

  // Push to Blynk cloud every 5 s
  if (now - lastBlynkPush >= BLYNK_PUSH_MS) {
    lastBlynkPush = now;
    pushToBlynk();
  }

  // Rotate OLED page every 3 s
  if (now - lastOledSwitch >= OLED_PAGE_MS) {
    lastOledSwitch = now;
    oledPage = (oledPage + 1) % 4;
    oledUpdate();
  }
}
