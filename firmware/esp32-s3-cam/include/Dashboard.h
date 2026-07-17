#pragma once

static const char DASHBOARD_HTML[] PROGMEM = R"HTML(<!DOCTYPE html>
<html>
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Electric Sky</title>
<style>
*{box-sizing:border-box}body{margin:0;background:#080a0d;color:#dce6ee;font:12px monospace}
header{position:sticky;top:0;z-index:2;background:#080a0dee;border-bottom:1px solid #26313a;padding:12px 18px;display:flex;justify-content:space-between;gap:16px}
h1{margin:0;color:#adf;font-size:16px;letter-spacing:.12em}.live{color:#55ee88}.err{color:#ff6677}
main{display:grid;grid-template-columns:repeat(auto-fit,minmax(360px,1fr));gap:12px;padding:12px}
.scope{border:1px solid #202a32;background:#0c1015;min-width:0}.scope-head{padding:9px 11px;display:flex;justify-content:space-between;border-bottom:1px solid #202a32}
.name{color:#8ba0af;text-transform:uppercase;letter-spacing:.1em}.reading{font-size:14px;color:white}
canvas{display:block;width:100%;height:170px;background:#07090c}
#diag{grid-column:1/-1;padding:12px;display:grid;grid-template-columns:repeat(auto-fit,minmax(180px,1fr));gap:8px;border:1px solid #202a32;background:#0c1015}
.metric{color:#778894}.metric span{color:#dce6ee}.sub{color:#53616b;font-size:10px}
</style>
</head>
<body>
<header><h1>ELECTRIC SKY · SIGNALS</h1><div id="connection">connecting</div></header>
<main>
<section class="scope"><div class="scope-head"><span class="name">Temperature</span><span class="reading" id="tempValue">—</span></div><canvas id="temp"></canvas></section>
<section class="scope"><div class="scope-head"><span class="name">Humidity</span><span class="reading" id="humidityValue">—</span></div><canvas id="humidity"></canvas></section>
<section class="scope"><div class="scope-head"><span class="name">Pressure</span><span class="reading" id="pressureValue">—</span></div><canvas id="pressure"></canvas></section>
<section class="scope"><div class="scope-head"><span class="name">Power</span><span class="reading" id="powerValue">—</span></div><canvas id="power"></canvas></section>
<section class="scope"><div class="scope-head"><span class="name">Microphone RMS</span><span class="reading" id="audioValue">—</span></div><canvas id="audio"></canvas></section>
<section id="diag">
  <div class="metric">BME acquire <span id="bmeHz">—</span><div class="sub" id="bmeDetail"></div></div>
  <div class="metric">Power acquire <span id="powerHz">—</span><div class="sub" id="powerDetail"></div></div>
  <div class="metric">Audio acquire <span id="audioHz">—</span><div class="sub" id="audioDetail"></div></div>
  <div class="metric">Transport <span id="packetHz">—</span><div class="sub" id="transportDetail"></div></div>
  <div class="metric">Browser jitter <span id="jitter">—</span><div class="sub" id="browserDetail"></div></div>
</section>
</main>
<script>
class Ring {
  constructor(capacity){this.a=new Array(capacity);this.capacity=capacity;this.head=0;this.count=0;this.lastSeq=null;this.gaps=0}
  push(s){if(this.lastSeq!==null&&s.seq!==this.lastSeq+1)this.gaps+=Math.max(0,s.seq-this.lastSeq-1);this.lastSeq=s.seq;this.a[this.head]=s;this.head=(this.head+1)%this.capacity;this.count=Math.min(this.count+1,this.capacity)}
  latest(){return this.count?this.a[(this.head-1+this.capacity)%this.capacity]:null}
  visitSince(time,fn){const start=(this.head-this.count+this.capacity)%this.capacity;for(let i=0;i<this.count;i++){const s=this.a[(start+i)%this.capacity];if(s.t>=time)fn(s)}}
}
const rings={temp:new Ring(1400),humidity:new Ring(1400),pressure:new Ring(1400),power:new Ring(14000),audio:new Ring(3500)};
let ws,packetLast=null,packetGaps=0,packetCount=0,lastArrival=0,intervalEma=20,jitterEma=0,bytesReceived=0,renderStalls=0,worstFrame=0,lastFrame=0;
const connection=document.getElementById('connection');
function u64(d,o){return Number(d.getBigUint64(o,true))}
function parsePacket(buffer){
  const d=new DataView(buffer);if(d.byteLength<40||d.getUint8(0)!==69||d.getUint8(1)!==83||d.getUint8(2)!==75||d.getUint8(3)!==89)return;
  const packetSeq=d.getUint32(8,true);if(packetLast!==null&&packetSeq!==packetLast+1)packetGaps+=Math.max(0,packetSeq-packetLast-1);packetLast=packetSeq;packetCount++;bytesReceived+=d.byteLength;
  const now=performance.now();if(lastArrival){const delta=now-lastArrival;intervalEma=intervalEma*.95+delta*.05;jitterEma=jitterEma*.9+Math.abs(delta-intervalEma)*.1}lastArrival=now;
  const nb=d.getUint16(20,true),np=d.getUint16(22,true),na=d.getUint16(24,true);let o=d.getUint16(6,true);
  for(let i=0;i<nb;i++,o+=24){const seq=d.getUint32(o,true),t=u64(d,o+4);rings.temp.push({seq,t,v:d.getFloat32(o+12,true)});rings.humidity.push({seq,t,v:d.getFloat32(o+16,true)});rings.pressure.push({seq,t,v:d.getFloat32(o+20,true)})}
  for(let i=0;i<np;i++,o+=24){const seq=d.getUint32(o,true),t=u64(d,o+4);rings.power.push({seq,t,v:d.getFloat32(o+20,true)/1000})}
  for(let i=0;i<na;i++,o+=16){const seq=d.getUint32(o,true),t=u64(d,o+4);rings.audio.push({seq,t,v:d.getFloat32(o+12,true)})}
}
function connect(){
  ws=new WebSocket('ws://{{IP}}:81/');ws.binaryType='arraybuffer';
  ws.onopen=()=>{connection.textContent='● live';connection.className='live'};
  ws.onmessage=e=>{if(e.data instanceof ArrayBuffer)parsePacket(e.data)};
  ws.onclose=()=>{connection.textContent='● reconnecting';connection.className='err';setTimeout(connect,1000)};
  ws.onerror=()=>ws.close();
}
function draw(name,color,unit,decimals){
  const canvas=document.getElementById(name),rect=canvas.getBoundingClientRect(),scale=devicePixelRatio||1,w=Math.max(1,Math.floor(rect.width*scale)),h=Math.max(1,Math.floor(rect.height*scale));
  if(canvas.width!==w||canvas.height!==h){canvas.width=w;canvas.height=h}const c=canvas.getContext('2d');c.clearRect(0,0,w,h);
  const ring=rings[name],latest=ring.latest();if(!latest)return;const end=latest.t,start=end-10000000;
  let lo=Infinity,hi=-Infinity;ring.visitSince(start,s=>{if(s.v<lo)lo=s.v;if(s.v>hi)hi=s.v});let range=hi-lo;if(range<1e-9)range=1;lo-=range*.12;hi+=range*.12;
  c.strokeStyle='#1b252d';c.lineWidth=scale;c.beginPath();for(let i=1;i<4;i++){const y=h*i/4;c.moveTo(0,y);c.lineTo(w,y)}c.stroke();
  c.strokeStyle=color;c.lineWidth=1.25*scale;c.beginPath();let first=true;ring.visitSince(start,s=>{const x=(s.t-start)/10000000*w,y=h-(s.v-lo)/(hi-lo)*h;if(first){c.moveTo(x,y);first=false}else c.lineTo(x,y)});c.stroke();
  document.getElementById(name+'Value').textContent=latest.v.toFixed(decimals)+' '+unit;
}
function render(now){if(lastFrame){const dt=now-lastFrame;if(dt>40)renderStalls++;if(dt>worstFrame)worstFrame=dt}lastFrame=now;draw('temp','#66ddff','°C',4);draw('humidity','#75ee99','%',4);draw('pressure','#dd99ff','hPa',4);draw('power','#ffcc66','W',4);draw('audio','#ff6688','dBFS',2);requestAnimationFrame(render)}
let prevPackets=0,prevBytes=0;
setInterval(async()=>{
  try{const s=await fetch('/status',{cache:'no-store'}).then(r=>r.json());
    bmeHz.textContent=s.bme_actual_hz.toFixed(1)+' Hz';powerHz.textContent=s.power_actual_hz.toFixed(1)+' Hz';audioHz.textContent=s.audio_actual_hz.toFixed(1)+' Hz';
    bmeDetail.textContent='queue '+s.bme_queue+' · overruns '+s.bme_overruns;powerDetail.textContent='queue '+s.power_queue+' · overruns '+s.power_overruns;audioDetail.textContent='queue '+s.audio_queue+' · overruns '+s.audio_overruns;
  }catch(e){}
  const dp=packetCount-prevPackets,db=bytesReceived-prevBytes;prevPackets=packetCount;prevBytes=bytesReceived;
  packetHz.textContent=dp+' pkt/s';transportDetail.textContent=(db/1024).toFixed(1)+' KiB/s · gaps '+packetGaps;
  jitter.textContent=jitterEma.toFixed(1)+' ms';browserDetail.textContent='sequence loss B/P/A '+rings.temp.gaps+'/'+rings.power.gaps+'/'+rings.audio.gaps+' · render stalls '+renderStalls+' · worst '+worstFrame.toFixed(0)+'ms';
},1000);
connect();requestAnimationFrame(render);
</script>
</body>
</html>)HTML";
