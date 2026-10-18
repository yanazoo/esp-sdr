#pragma once
/* Single-page UI served at "/". Spectrum frames arrive on ws://<ip>:81/. */
static const char INDEX_HTML[] PROGMEM = R"HTML(<!doctype html>
<html lang="ja"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>5.8GHz FPV Scanner</title>
<style>
:root{--bg:#0d1117;--panel:#161b22;--line:#30363d;--text:#e6edf3;--dim:#8b949e;--trace:#f2cc60;--peak:#ff7b72;--avg:#79c0ff}
*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--text);font:14px system-ui,sans-serif}
header{display:flex;justify-content:space-between;align-items:center;padding:8px 12px;background:var(--panel);border-bottom:1px solid var(--line)}
h1{font-size:16px;margin:0}#st{font-size:12px;color:var(--dim)}#st.ok{color:#3fb950}#st.ng{color:#f85149}
main{padding:8px;max-width:1100px;margin:auto}
#cv{width:100%;height:46vh;min-height:260px;background:#000;border:1px solid var(--line);border-radius:6px;touch-action:none;display:block}
.row{display:flex;flex-wrap:wrap;gap:6px;align-items:center;margin:8px 0}
.box{background:var(--panel);border:1px solid var(--line);border-radius:6px;padding:8px;margin:8px 0}
.box h2{font-size:13px;margin:0 0 6px;color:var(--dim)}
input,select,button{background:#0d1117;color:var(--text);border:1px solid var(--line);border-radius:4px;padding:5px 6px;font-size:14px}
input[type=number]{width:78px}button{cursor:pointer}button.on{background:#1f6feb;border-color:#1f6feb}
table{border-collapse:collapse;width:100%;font-size:12px}td,th{border:1px solid var(--line);padding:3px;text-align:center}
td.ch{cursor:pointer}td.ch small{display:block;color:var(--dim)}
.mk td{font-variant-numeric:tabular-nums}.scroll{overflow-x:auto}
label{white-space:nowrap}
</style></head><body>
<header><h1>5.8GHz FPV Scanner</h1><span id="st">接続中…</span></header>
<main>
<canvas id="cv"></canvas>
<div class="row">
 <span>マーカー:</span>
 <button class="mb on" data-m="0" style="color:#ff6">M1</button>
 <button class="mb" data-m="1" style="color:#6ff">M2</button>
 <button class="mb" data-m="2" style="color:#f6f">M3</button>
 <button class="mb" data-m="3" style="color:#6f6">M4</button>
 <button id="mpk">ピークへ</button><button id="mclr">消去</button>
 <label><input type="checkbox" id="hold"> ピークホールド</label>
 <label><input type="checkbox" id="avg"> 平均</label>
</div>
<div class="box"><h2>マーカー（タップでアクティブマーカーを配置）</h2>
<table class="mk"><tr><th>#</th><th>周波数 MHz</th><th>レベル dBFS</th><th>最寄りFPV ch</th></tr><tbody id="mk"></tbody></table></div>

<div class="box"><h2>スパン</h2>
<div class="row">
 <label>開始 <input type="number" id="start" step="1"></label>
 <label>終了 <input type="number" id="stop" step="1"></label> MHz
 <button id="apply">適用</button>
</div>
<div class="row">
 <label>中心 <input type="number" id="center" step="1"></label>
 <label>スパン <input type="number" id="span" step="1"></label> MHz
 <button id="applyc">適用</button>
</div>
<div class="row">
 <button class="pre" data-a="5650" data-b="5950">5650-5950 全体</button>
 <button class="pre" data-a="5640" data-b="5950">5640-5950 (E4含む)</button>
 <button class="pre" data-a="5720" data-b="5870">5720-5870</button>
 <button id="zoom">アクティブマーカー±20MHz</button>
</div></div>

<div class="box"><h2>FPVチャンネル（セルをタップでマーカー配置／レベル = ±2MHz内の最大）</h2>
<div class="row" id="bandsel"></div>
<div class="scroll"><table id="fpv"></table></div></div>

<div class="box"><h2>受信設定</h2>
<div class="row">
 <label>サンプルレート <select id="rate"><option value="0">80 MS/s (±20MHz/step)</option><option value="1" selected>40 MS/s (±15MHz/step)</option><option value="2">20 MS/s</option><option value="3">10 MS/s</option></select></label>
 <label>滞在 <input type="number" id="dwell" min="5" max="500"> ms/step</label>
 <label>検波 <select id="det"><option value="1">最大値</option><option value="0">平均</option></select></label>
 <label>ゲイン <input type="number" id="gain" min="-1" max="90" title="-1 = 自動(AGC)"> (-1=自動)</label>
 <button id="applyr">適用</button>
</div>
<div class="row">
 <label>表示上限 <input type="number" id="ytop" value="-10"> dBFS</label>
 <label>表示範囲 <input type="number" id="yrng" value="90"> dB</label>
</div></div>
<p style="color:var(--dim);font-size:12px">レベルはESP32-C5のADC基準の相対値（dBFS概算）で、校正されたdBmではありません。ゲイン自動(AGC)ではステップごとに感度が変わるため、レベル比較にはゲイン固定（例: 40）が有効です。5895MHz超は実機で受信可否を確認してください。</p>
</main>
<script>
const BANDS={A:[5865,5845,5825,5805,5785,5765,5745,5725],B:[5733,5752,5771,5790,5809,5828,5847,5866],
E:[5705,5685,5665,5645,5885,5905,5925,5945],F:[5740,5760,5780,5800,5820,5840,5860,5880],R:[5658,5695,5732,5769,5806,5843,5880,5917]};
const BNAME={A:'Boscam A / TBS / RC305',B:'Boscam B',E:'Boscam E / DJI',F:'IRC NexWave / Fatshark',R:'Raceband'};
const BCOL={A:'#ff9e64',B:'#9ece6a',E:'#bb9af7',F:'#7dcfff',R:'#f7768e'};
const MCOL=['#ff6','#6ff','#f6f','#6f6'];
const FS=[80,40,20,10];
let cfg={},data=null,peak=null,avg=null,f0=5650,f1=5950,act=0,markers=[null,null,null,null];
let show={A:true,B:false,E:false,F:false,R:true};
const $=id=>document.getElementById(id),cv=$('cv'),ctx=cv.getContext('2d');
const db=c=>c/2-84.3;
function freqAt(i,n){return f0+(f1-f0)*(i+0.5)/n}
function idxAt(f){if(!data)return -1;const n=data.length;const i=Math.floor((f-f0)/(f1-f0)*n);return i<0||i>=n?-1:i}
function trace(){return $('avg').checked&&avg?avg:data}
function level(f){const t=trace();const i=idxAt(f);return i<0?null:db(t[i])}
function levelMax(f,w){const t=trace();if(!t)return null;let m=null;for(let x=f-w;x<=f+w;x+=0.1){const i=idxAt(x);if(i>=0&&(m===null||t[i]>m))m=t[i]}return m===null?null:db(m)}
function nearestCh(f){let best=null;for(const b in BANDS)BANDS[b].forEach((c,k)=>{const d=Math.abs(c-f);if(!best||d<best.d)best={d,t:b+(k+1)+' '+c}});return best&&best.d<=10?best.t:'-'}
// ---- drawing
function resize(){const r=window.devicePixelRatio||1;cv.width=cv.clientWidth*r;cv.height=cv.clientHeight*r;draw()}
function draw(){
 const W=cv.width,H=cv.height,r=window.devicePixelRatio||1,L=44*r,B=22*r,T=6*r,R=6*r,pw=W-L-R,ph=H-T-B;
 const ytop=+$('ytop').value,yr=+$('yrng').value||90;
 const X=f=>L+(f-f0)/(f1-f0)*pw,Y=d=>T+(ytop-d)/yr*ph;
 ctx.fillStyle='#000';ctx.fillRect(0,0,W,H);ctx.font=(11*r)+'px system-ui';ctx.lineWidth=1;
 // grid
 ctx.strokeStyle='#222';ctx.fillStyle='#8b949e';ctx.textAlign='right';
 for(let d=Math.ceil((ytop-yr)/10)*10;d<=ytop;d+=10){const y=Y(d);ctx.beginPath();ctx.moveTo(L,y);ctx.lineTo(W-R,y);ctx.stroke();ctx.fillText(d,L-3*r,y+4*r)}
 const span=f1-f0,st=span>200?50:span>100?20:span>40?10:span>15?5:1;ctx.textAlign='center';
 for(let f=Math.ceil(f0/st)*st;f<=f1;f+=st){const x=X(f);ctx.beginPath();ctx.moveTo(x,T);ctx.lineTo(x,T+ph);ctx.stroke();ctx.fillText(f,x,H-6*r)}
 // FPV channels
 let lane=0;for(const b in BANDS){if(!show[b])continue;ctx.strokeStyle=BCOL[b]+'66';ctx.fillStyle=BCOL[b];
  BANDS[b].forEach((f,k)=>{if(f<f0||f>f1)return;const x=X(f);ctx.beginPath();ctx.moveTo(x,T);ctx.lineTo(x,T+ph);ctx.stroke();ctx.fillText(b+(k+1),x,T+(11+lane*11)*r)});lane++}
 const line=(t,col)=>{if(!t)return;ctx.strokeStyle=col;ctx.lineWidth=1.2*r;ctx.beginPath();const n=t.length;
  for(let i=0;i<n;i++){const x=L+(i+0.5)/n*pw,y=Math.min(T+ph,Math.max(T,Y(db(t[i]))));i?ctx.lineTo(x,y):ctx.moveTo(x,y)}ctx.stroke();ctx.lineWidth=1};
 if($('hold').checked)line(peak,'#ff7b72');
 line(trace(),$('avg').checked?'#79c0ff':'#f2cc60');
 // markers
 markers.forEach((f,m)=>{if(f===null)return;const lv=level(f);if(lv===null)return;const x=X(f),y=Y(lv);
  ctx.fillStyle=MCOL[m];ctx.beginPath();ctx.moveTo(x,y-2*r);ctx.lineTo(x-6*r,y-12*r);ctx.lineTo(x+6*r,y-12*r);ctx.fill();
  ctx.fillText('M'+(m+1),x,y-15*r)});
}
// ---- tables
function renderMarkers(){let h='';markers.forEach((f,m)=>{const lv=f===null?null:level(f);
 h+=`<tr style="color:${MCOL[m]}"><td>M${m+1}${m===act?' ◀':''}</td><td>${f===null?'-':f.toFixed(1)}</td><td>${lv===null?'-':lv.toFixed(1)}</td><td>${f===null?'-':nearestCh(f)}</td></tr>`});$('mk').innerHTML=h}
function renderFpv(){let h='<tr><th>Band</th>'+[1,2,3,4,5,6,7,8].map(c=>'<th>CH'+c+'</th>').join('')+'</tr>';
 for(const b in BANDS){h+=`<tr><th style="color:${BCOL[b]}" title="${BNAME[b]}">${b}</th>`;
  BANDS[b].forEach(f=>{const lv=levelMax(f,2);let bg='';if(lv!==null){const a=Math.max(0,Math.min(1,(lv-(+$('ytop').value-+$('yrng').value))/(+$('yrng').value)));bg=`background:rgba(255,${Math.round(200-160*a)},0,${(0.15+0.6*a).toFixed(2)})`}
   h+=`<td class="ch" data-f="${f}" style="${bg}">${f}<small>${lv===null?'範囲外':lv.toFixed(1)}</small></td>`});h+='</tr>'}
 $('fpv').innerHTML=h;document.querySelectorAll('td.ch').forEach(td=>td.onclick=()=>{markers[act]=+td.dataset.f;refresh()})}
function refresh(){draw();renderMarkers();renderFpv()}
// ---- websocket
let ws;function connect(){ws=new WebSocket('ws://'+location.hostname+':81/');ws.binaryType='arraybuffer';
 ws.onmessage=e=>{if(typeof e.data==='string'){const s=JSON.parse(e.data);if(s.type!=='status')return;
   const first=!cfg.start;cfg=s;$('st').textContent=s.linked?`${s.info} / 1スイープ ${s.sweep_ms} ms`:'C5未接続: '+s.info;$('st').className=s.linked?'ok':'ng';
   if(first)fillForm();return}
  const v=new DataView(e.data);if(v.getUint32(0,true)!==0x31565046)return;
  const a=v.getUint32(4,true)/1000,b=v.getUint32(8,true)/1000,n=v.getUint16(12,true);
  const t=new Uint8Array(e.data,24,n);
  if(a!==f0||b!==f1||!data||data.length!==n){f0=a;f1=b;peak=null;avg=null}
  data=Uint8Array.from(t);
  peak=peak?peak.map((p,i)=>Math.max(p,t[i])):Uint8Array.from(t);
  avg=avg?avg.map((p,i)=>p*0.8+t[i]*0.2):Float32Array.from(t);
  refresh()};
 ws.onclose=()=>{$('st').textContent='切断（再接続中）';$('st').className='ng';setTimeout(connect,1500)}}
function fillForm(){$('start').value=cfg.start;$('stop').value=cfg.stop;$('center').value=(cfg.start+cfg.stop)/2;$('span').value=cfg.stop-cfg.start;
 $('rate').value=cfg.rate;$('dwell').value=cfg.dwell;$('det').value=cfg.det;$('gain').value=cfg.gain}
function send(s){if(ws&&ws.readyState===1)ws.send(s)}
function setSpan(a,b){a=Math.round(a);b=Math.round(b);if(b-a<5)b=a+5;$('start').value=a;$('stop').value=b;$('center').value=(a+b)/2;$('span').value=b-a;send(`cfg start=${a} stop=${b}`)}
// ---- controls
$('apply').onclick=()=>setSpan(+$('start').value,+$('stop').value);
$('applyc').onclick=()=>{const c=+$('center').value,s=+$('span').value;setSpan(c-s/2,c+s/2)};
document.querySelectorAll('.pre').forEach(b=>b.onclick=()=>setSpan(+b.dataset.a,+b.dataset.b));
$('zoom').onclick=()=>{const f=markers[act];if(f!==null)setSpan(f-20,f+20)};
$('applyr').onclick=()=>{send(`cfg rate=${$('rate').value} dwell=${$('dwell').value} det=${$('det').value} gain=${$('gain').value}`);peak=null};
document.querySelectorAll('.mb').forEach(b=>b.onclick=()=>{act=+b.dataset.m;document.querySelectorAll('.mb').forEach(x=>x.classList.toggle('on',x===b));renderMarkers()});
$('mpk').onclick=()=>{const t=trace();if(!t)return;let k=0;for(let i=1;i<t.length;i++)if(t[i]>t[k])k=i;markers[act]=freqAt(k,t.length);refresh()};
$('mclr').onclick=()=>{markers[act]=null;refresh()};
['hold','avg','ytop','yrng'].forEach(id=>$(id).onchange=()=>{if(id==='hold')peak=null;refresh()});
cv.addEventListener('pointerdown',e=>{const r=cv.getBoundingClientRect(),L=44,R=6,x=e.clientX-r.left;
 const f=f0+(x-L)/(r.width-L-R)*(f1-f0);if(f<f0||f>f1)return;markers[act]=Math.round(f*10)/10;refresh()});
const bs=$('bandsel');for(const b in BANDS){const l=document.createElement('label');l.innerHTML=`<input type="checkbox" ${show[b]?'checked':''}> <span style="color:${BCOL[b]}">${b}</span> `;
 l.firstChild.onchange=e=>{show[b]=e.target.checked;draw()};bs.appendChild(l)}
window.onresize=resize;resize();renderFpv();renderMarkers();connect();
</script></body></html>)HTML";
