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
.controls{display:flex;align-items:center;gap:8px;color:#8ba0af}.controls input{width:150px}.controls button{background:#17212a;color:#adf;border:1px solid #33424e;padding:4px 7px;font:11px monospace}.healthy{color:#55ee88!important}.low{color:#ffcc66!important}.loss{color:#ff6677!important}
</style>
</head>
<body>
<header><h1>ELECTRIC SKY · SIGNALS</h1><div class="controls"><label>Presentation delay <input id="delay" type="range" min="0.5" max="10" step="0.25" value="6"><span id="delayValue">6.00s</span></label><button id="useRecommended">use recommended</button><div id="connection">connecting</div></div></header>
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
  <div class="metric">Buffer health <span id="bufferHealth">waiting</span><div class="sub" id="bufferDetail"></div></div>
</section>
</main>
<script>
class Ring {
  constructor(capacity,expectedUs){this.a=new Array(capacity);this.capacity=capacity;this.expectedUs=expectedUs;this.head=0;this.count=0;this.lastSeq=null;this.gaps=0;this.gapEvents=0;this.gapUs=0}
  push(s){if(this.lastSeq!==null&&s.seq!==this.lastSeq+1){const missing=Math.max(0,s.seq-this.lastSeq-1);if(missing){this.gaps+=missing;this.gapEvents++;this.gapUs+=missing*this.expectedUs}}this.lastSeq=s.seq;this.a[this.head]=s;this.head=(this.head+1)%this.capacity;this.count=Math.min(this.count+1,this.capacity)}
  latest(){return this.count?this.a[(this.head-1+this.capacity)%this.capacity]:null}
  visitRange(startTime,endTime,fn){const start=(this.head-this.count+this.capacity)%this.capacity;for(let i=0;i<this.count;i++){const s=this.a[(start+i)%this.capacity];if(s.t>=startTime&&s.t<=endTime)fn(s)}}
}
const rings={temp:new Ring(2500,10000),humidity:new Ring(2500,10000),pressure:new Ring(2500,10000),power:new Ring(25000,1000),audio:new Ring(6250,4000)};
let ws,packetLast=null,packetGaps=0,packetGapEvents=0,packetCount=0,lastArrival=0,intervalEma=50,jitterEma=0,bytesReceived=0,renderStalls=0,worstFrame=0,lastFrame=0,displayUnderruns=0,inUnderrun=false,underrunStarted=0,underrunMs=0,observationStarted=0,longestSilence=0;
let transportQueued=0,transportDropped=0,scheduledHz=20,firmwareUptimeMs=0,baselineDrops=null,baselineUptimeMs=0;
let viewEnd=null;
let presentationDelayUs=6000000,recommendedDelaySec=.5;
const WINDOW_US=10000000;
const connection=document.getElementById('connection');
const delay=document.getElementById('delay'),delayValue=document.getElementById('delayValue');
delay.oninput=()=>{presentationDelayUs=Number(delay.value)*1000000;delayValue.textContent=Number(delay.value).toFixed(2)+'s';const newest=newestTime();if(newest)viewEnd=newest-presentationDelayUs};
useRecommended.onclick=()=>{delay.value=recommendedDelaySec;delay.oninput()};
function u64(d,o){return Number(d.getBigUint64(o,true))}
function parsePacket(buffer){
  const d=new DataView(buffer);if(d.byteLength<72||d.getUint8(0)!==69||d.getUint8(1)!==83||d.getUint8(2)!==75||d.getUint8(3)!==89)return;
  const packetSeq=d.getUint32(8,true);if(packetLast!==null&&packetSeq!==packetLast+1){const missing=Math.max(0,packetSeq-packetLast-1);if(missing){packetGaps+=missing;packetGapEvents++}}packetLast=packetSeq;packetCount++;bytesReceived+=d.byteLength;
  const now=performance.now();if(!observationStarted)observationStarted=now;if(lastArrival){const delta=now-lastArrival;if(delta>longestSilence)longestSilence=delta;intervalEma=intervalEma*.95+delta*.05;jitterEma=jitterEma*.9+Math.abs(delta-intervalEma)*.1}lastArrival=now;
  bmeHz.textContent=(d.getUint16(40,true)/10).toFixed(1)+' Hz';powerHz.textContent=(d.getUint16(42,true)/10).toFixed(1)+' Hz';audioHz.textContent=(d.getUint16(44,true)/10).toFixed(1)+' Hz';
  bmeDetail.textContent='queue '+d.getUint16(46,true)+' · overruns '+d.getUint32(28,true);powerDetail.textContent='queue '+d.getUint16(48,true)+' · overruns '+d.getUint32(32,true)+' · interval avg/max '+d.getUint16(64,true)+'/'+d.getUint16(66,true)+'µs · duplicates '+(d.getUint16(68,true)/10).toFixed(1)+'% · conversion '+d.getUint16(70,true)+'µs';audioDetail.textContent='queue '+d.getUint16(50,true)+' · overruns '+d.getUint32(36,true);transportQueued=d.getUint16(52,true);transportDropped=d.getUint32(54,true);scheduledHz=d.getUint16(58,true)/10;firmwareUptimeMs=d.getUint32(60,true);if(baselineDrops===null){baselineDrops=transportDropped;baselineUptimeMs=firmwareUptimeMs}
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
function newestTime(){let t=0;for(const ring of Object.values(rings)){const s=ring.latest();if(s&&s.t>t)t=s.t}return t}
function draw(name,color,unit,decimals,end,dt){
  const canvas=document.getElementById(name),rect=canvas.getBoundingClientRect(),scale=devicePixelRatio||1,w=Math.max(1,Math.floor(rect.width*scale)),h=Math.max(1,Math.floor(rect.height*scale));
  if(canvas.width!==w||canvas.height!==h){canvas.width=w;canvas.height=h}const c=canvas.getContext('2d');c.clearRect(0,0,w,h);
  const ring=rings[name],latest=ring.latest();if(!latest||end===null)return;const start=end-WINDOW_US,bins=Math.max(1,Math.ceil(w/2));if(!canvas._mins||canvas._mins.length!==bins){canvas._mins=new Float32Array(bins);canvas._maxs=new Float32Array(bins)}const mins=canvas._mins,maxs=canvas._maxs;mins.fill(Infinity);maxs.fill(-Infinity);
  let lo=Infinity,hi=-Infinity;ring.visitRange(start,end,s=>{if(s.v<lo)lo=s.v;if(s.v>hi)hi=s.v;const b=Math.min(bins-1,Math.max(0,Math.floor((s.t-start)/WINDOW_US*bins)));if(s.v<mins[b])mins[b]=s.v;if(s.v>maxs[b])maxs[b]=s.v});if(lo===Infinity)return;const dataLo=lo,dataHi=hi,peakToPeak=dataHi-dataLo;let range=peakToPeak;if(range<1e-9)range=1;const wantedLo=dataLo-range*.12,wantedHi=dataHi+range*.12;if(canvas._axisLo===undefined){canvas._axisLo=wantedLo;canvas._axisHi=wantedHi;canvas._axisRecheckMs=0}else{canvas._axisRecheckMs+=dt;let expanded=false;if(wantedLo<canvas._axisLo){canvas._axisLo=wantedLo;expanded=true}if(wantedHi>canvas._axisHi){canvas._axisHi=wantedHi;expanded=true}if(expanded)canvas._axisRecheckMs=0;else if(canvas._axisRecheckMs>=60000){canvas._axisLo=wantedLo;canvas._axisHi=wantedHi;canvas._axisRecheckMs%=60000}}lo=canvas._axisLo;hi=canvas._axisHi;
  c.strokeStyle='#1b252d';c.lineWidth=scale;c.beginPath();for(let i=1;i<4;i++){const y=h*i/4;c.moveTo(0,y);c.lineTo(w,y)}c.stroke();
  c.strokeStyle=color;c.lineWidth=1.25*scale;c.beginPath();let previous=-2;for(let b=0;b<bins;b++){if(mins[b]===Infinity)continue;const x=(b+.5)/bins*w,y1=h-(mins[b]-lo)/(hi-lo)*h,y2=h-(maxs[b]-lo)/(hi-lo)*h;if(b!==previous+1)c.moveTo(x,y1);else c.lineTo(x,y1);if(y2!==y1)c.lineTo(x,y2);previous=b}c.stroke();
  c.fillStyle='#738592';c.font=(10*scale)+'px monospace';c.textBaseline='top';c.fillText('max '+dataHi.toFixed(decimals),5*scale,4*scale);c.textAlign='right';c.fillText('time window '+(WINDOW_US/1000000).toFixed(0)+'s',w-5*scale,4*scale);c.textBaseline='bottom';c.textAlign='left';c.fillText('min '+dataLo.toFixed(decimals),5*scale,h-4*scale);c.textAlign='right';c.fillText('peak-to-peak range '+peakToPeak.toFixed(decimals),w-5*scale,h-4*scale);c.textAlign='left';
  document.getElementById(name+'Value').textContent=latest.v.toFixed(decimals)+' '+unit;
}
function render(now){const newest=newestTime(),dt=lastFrame?now-lastFrame:0;if(lastFrame){if(dt>40)renderStalls++;if(dt>worstFrame)worstFrame=dt}if(viewEnd===null&&newest)viewEnd=newest-presentationDelayUs;else if(lastFrame&&viewEnd!==null)viewEnd+=dt*1000;const underrun=viewEnd!==null&&viewEnd>newest;if(underrun&&!inUnderrun){displayUnderruns++;underrunStarted=now}else if(!underrun&&inUnderrun){underrunMs+=now-underrunStarted;underrunStarted=0}inUnderrun=underrun;lastFrame=now;draw('temp','#66ddff','°C',4,viewEnd,dt);draw('humidity','#75ee99','%',4,viewEnd,dt);draw('pressure','#dd99ff','hPa',4,viewEnd,dt);draw('power','#ffcc66','W',4,viewEnd,dt);draw('audio','#ff6688','dBFS',2,viewEnd,dt);requestAnimationFrame(render)}
let prevPackets=0,prevBytes=0;
setInterval(async()=>{
  const now=performance.now(),dp=packetCount-prevPackets,db=bytesReceived-prevBytes,observedSec=Math.max(.001,(now-observationStarted)/1000),avg=packetCount/observedSec,currentSilence=lastArrival?now-lastArrival:0,totalUnderrun=underrunMs+(inUnderrun?now-underrunStarted:0),dropDelta=baselineDrops===null?0:transportDropped-baselineDrops,dropSeconds=Math.max(.001,(firmwareUptimeMs-baselineUptimeMs)/1000),dropMin=dropDelta/(dropSeconds/60);prevPackets=packetCount;prevBytes=bytesReceived;
  packetHz.textContent=dp+' pkt/s';transportDetail.textContent='scheduled '+scheduledHz.toFixed(1)+' · avg '+avg.toFixed(1)+' · '+(db/1024).toFixed(1)+' KiB/s · observed '+observedSec.toFixed(0)+'s · packet gaps '+packetGapEvents+'/'+packetGaps+' · queue '+transportQueued+' · dropped since connect '+dropDelta+' ('+dropMin.toFixed(1)+'/min)';
  jitter.textContent=jitterEma.toFixed(1)+' ms';browserDetail.textContent='silence now/max '+currentSilence.toFixed(0)+'/'+longestSilence.toFixed(0)+'ms · sample gaps B/P/A events '+rings.temp.gapEvents+'/'+rings.power.gapEvents+'/'+rings.audio.gapEvents+' missing '+rings.temp.gaps+'/'+rings.power.gaps+'/'+rings.audio.gaps+' duration '+(rings.temp.gapUs/1e6).toFixed(1)+'/'+(rings.power.gapUs/1e6).toFixed(1)+'/'+(rings.audio.gapUs/1e6).toFixed(1)+'s · underruns '+displayUnderruns+' for '+(totalUnderrun/1000).toFixed(1)+'s · render stalls '+renderStalls+' · worst '+worstFrame.toFixed(0)+'ms';
  recommendedDelaySec=Math.min(10,Math.max(.5,Math.ceil((longestSilence+500)/250)*.25));const lead=viewEnd===null?0:(newestTime()-viewEnd)/1e6,loss=packetGaps>0||dropDelta>0,adequate=Number(delay.value)>=recommendedDelaySec;if(loss){bufferHealth.textContent='loss detected';bufferHealth.className='loss'}else if(inUnderrun||!adequate){bufferHealth.textContent=inUnderrun?'underrun':'increase delay';bufferHealth.className='low'}else{bufferHealth.textContent='healthy';bufferHealth.className='healthy'}bufferDetail.textContent='buffered '+lead.toFixed(2)+'s · configured '+Number(delay.value).toFixed(2)+'s · recommended '+recommendedDelaySec.toFixed(2)+'s from observed '+(longestSilence/1000).toFixed(2)+'s max silence'+(loss?' · permanent packet loss cannot be filled by latency':' · all observed delays covered');
},1000);
connect();requestAnimationFrame(render);
</script>
</body>
</html>)HTML";
