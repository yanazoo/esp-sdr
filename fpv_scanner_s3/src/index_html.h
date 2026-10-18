#pragma once
/* Single-page UI served at "/". Spectrum frames arrive on ws://<ip>:81/.
 * Levels are shown in dBm: dBFS plus the saved calibration offset. */
static const char INDEX_HTML[] PROGMEM = R"HTML(<!doctype html>
<html lang="ja"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>5.8GHz FPV Scanner</title>
<style>
:root{--bg:#0d1117;--panel:#161b22;--line:#30363d;--text:#e6edf3;--dim:#8b949e}
*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--text);font:14px system-ui,sans-serif}
header{display:flex;justify-content:space-between;align-items:center;padding:8px 12px;background:var(--panel);border-bottom:1px solid var(--line)}
h1{font-size:16px;margin:0}#st{font-size:12px;color:var(--dim)}#st.ok{color:#3fb950}#st.ng{color:#f85149}
#applied{font-size:12px;color:var(--dim);padding:4px 12px;background:var(--panel);border-bottom:1px solid var(--line)}
#applied b{color:var(--text)}.err{color:#f85149}.wait{color:#d29922}
main{padding:8px;max-width:1100px;margin:auto}
#cv{width:100%;height:46vh;min-height:260px;background:#000;border:1px solid var(--line);border-radius:6px;touch-action:none;display:block}
.row{display:flex;flex-wrap:wrap;gap:6px;align-items:center;margin:8px 0}
.box{background:var(--panel);border:1px solid var(--line);border-radius:6px;padding:8px;margin:8px 0}
.box h2{font-size:13px;margin:0 0 6px;color:var(--dim)}
input,select,button{background:#0d1117;color:var(--text);border:1px solid var(--line);border-radius:4px;padding:5px 6px;font-size:14px}
input[type=number]{width:78px}button{cursor:pointer}button.on{background:#1f6feb;border-color:#1f6feb}
button.dirty{border-color:#d29922;color:#d29922}
table{border-collapse:collapse;width:100%;font-size:12px}td,th{border:1px solid var(--line);padding:4px 2px;text-align:center}
td.ch{cursor:pointer}td.mk{font-weight:bold}
.mt td{font-variant-numeric:tabular-nums}.scroll{overflow-x:auto}
label{white-space:nowrap}.note{color:var(--dim);font-size:12px}
</style></head><body>
<header><h1>5.8GHz FPV Scanner</h1><span id="st">接続中…</span></header>
<div id="applied"></div>
<main>
<canvas id="cv"></canvas>
<div id="warn" class="err note" style="display:none">入力が強すぎる可能性があります（受信機が飽和すると、実在しない位置に電波が表示されます）。ゲインを下げるか、送信機から離してください。</div>

<div class="box"><h2>マーカー（M1〜M4を選び、グラフまたは下のチャネル表をタップして配置。「保存」で次回起動時も同じ位置）</h2>
<div class="row">
 <button class="mb on" data-m="0" style="color:#ff6">M1</button>
 <button class="mb" data-m="1" style="color:#6ff">M2</button>
 <button class="mb" data-m="2" style="color:#f6f">M3</button>
 <button class="mb" data-m="3" style="color:#6f6">M4</button>
 <button id="mpk">ピークへ</button><button id="mclr">消去</button><button id="msave">保存</button>
</div>
<table class="mt"><tr><th>#</th><th>周波数 MHz</th><th>受信レベル dBm</th><th>最寄りFPV ch</th></tr><tbody id="mk"></tbody></table>
</div>

<div class="box"><h2>FPVチャネル（タップでアクティブマーカーをその周波数へ）</h2>
<div class="scroll"><table id="fpv"></table></div>
<div class="row"><span>グラフに表示するバンド:</span><span id="bandsel"></span><button id="applyb">適用</button></div>
</div>

<div class="box"><h2>スパン（適用で反映・保存）</h2>
<div class="row">
 <label>開始 <input type="number" id="start" step="1"></label>
 <label>終了 <input type="number" id="stop" step="1"></label> MHz
</div>
<div class="row">
 <label>中心 <input type="number" id="center" step="1"></label>
 <label>幅 <input type="number" id="span" step="1"></label> MHz
</div>
<div class="row">
 <button class="pre" data-a="5650" data-b="5850">5650-5850 (既定)</button>
 <button class="pre" data-a="5640" data-b="5950">5640-5950 (全FPV ch)</button>
 <button id="zoom">アクティブマーカー±20MHz</button>
</div>
<div class="row"><button id="applys">適用</button></div>
</div>

<div class="box"><h2>受信設定（適用で反映・保存）</h2>
<div class="row">
 <label>ステップ幅 <select id="rate"><option value="0">40 MHz（80MS/s・速い）</option><option value="1">30 MHz（80MS/s・標準）</option><option value="2">15 MHz（40MS/s）</option><option value="3">7.5 MHz（20MS/s・細かい）</option></select></label>
 <label>滞在 <input type="number" id="dwell" min="5" max="500"> ms/step</label>
 <label>検波 <select id="det"><option value="1">最大値</option><option value="0">平均</option></select></label>
 <label>ゲイン <input type="number" id="gain" min="-1" max="90"> <span id="grange">(-1=自動)</span></label>
 <label>dBm補正値 <input type="number" id="cal" step="0.1"> dB</label>
</div>
<div class="row"><button id="applyr">適用</button></div>
<p class="note">dBm = ADC基準のレベル + 補正値。強さのわかっている電波を受けて、表示が正しいdBmになるよう補正値を合わせてください。dBmで比べるときはゲインを固定（例: 40）してください。</p>
</div>

<div class="box"><h2>ノイズレベル（使う場所で保存）</h2>
<div class="row"><button id="nsave">今のノイズを保存</button><button id="nclr">保存したノイズを消去</button><span id="nst" class="note"></span></div>
<p class="note">VTXを止めた状態でしばらく表示させてから保存してください。直近の平均値を保存し、グラフに灰色の基準線として表示します。ゲイン・サンプルレート・検波を変えると、条件が合わなくなるため自動で消去されます。</p>
</div>

<div class="box"><h2>表示（適用で反映・保存）</h2>
<div class="row">
 <label><input type="checkbox" id="hold"> ピークホールド</label>
 <label><input type="checkbox" id="avg"> 平均</label>
 <label>上限 <input type="number" id="ytop" value="-17"> dBm</label>
 <label>範囲 <input type="number" id="yrng" value="75"> dB</label>
</div>
<div class="row"><button id="applyd">適用</button></div></div>
<p class="note">5895MHz超は実機で受信可否を確認してください。</p>
</main>
<script>
const BANDS={A:[5865,5845,5825,5805,5785,5765,5745,5725],B:[5733,5752,5771,5790,5809,5828,5847,5866],
E:[5705,5685,5665,5645,5885,5905,5925,5945],F:[5740,5760,5780,5800,5820,5840,5860,5880],R:[5658,5695,5732,5769,5806,5843,5880,5917]};
const BNAME={A:'Boscam A / TBS / RC305',B:'Boscam B',E:'Boscam E / DJI',F:'IRC NexWave / Fatshark',R:'Raceband'};
const BKEYS=Object.keys(BANDS),BCOL={A:'#ff9e64',B:'#9ece6a',E:'#bb9af7',F:'#7dcfff',R:'#f7768e'};
const MCOL=['#ff6','#6ff','#f6f','#6f6'],FS=[80,80,40,20];
let cfg={},data=null,peak=null,avg=null,noise=null,f0=5650,f1=5850,act=0,markers=[null,null,null,null];
const $=id=>document.getElementById(id),cv=$('cv'),ctx=cv.getContext('2d');
const cal=()=>(cfg.cal||0)/10;
const dbm=c=>c/2-84.3+cal();
const shown=b=>((cfg.bands??17)>>BKEYS.indexOf(b))&1;
const useAvg=()=>!!cfg.avg,useHold=()=>!!cfg.hold;
function freqAt(i,n){return f0+(f1-f0)*(i+0.5)/n}
function trace(){return useAvg()&&avg?avg:data}
function level(f){const t=trace();if(!t||f<f0||f>=f1)return null;return dbm(t[Math.floor((f-f0)/(f1-f0)*t.length)])}
function nearestCh(f){let best=null;for(const b of BKEYS)BANDS[b].forEach((c,k)=>{const d=Math.abs(c-f);if(!best||d<best.d)best={d,t:b+(k+1)+' '+c}});return best&&best.d<=10?best.t:'-'}
// ---- drawing (per frame: canvas + marker table only)
function resize(){const r=window.devicePixelRatio||1;cv.width=cv.clientWidth*r;cv.height=cv.clientHeight*r;draw()}
function draw(){
 const W=cv.width,H=cv.height,r=window.devicePixelRatio||1,L=44*r,B=22*r,T=6*r,R=10*r,pw=W-L-R,ph=H-T-B;
 const ytop=cfg.ytop??-17,yr=cfg.yrange||75;
 const X=f=>L+(f-f0)/(f1-f0)*pw,Y=d=>Math.min(T+ph,Math.max(T,T+(ytop-d)/yr*ph));
 ctx.fillStyle='#000';ctx.fillRect(0,0,W,H);ctx.font=(11*r)+'px system-ui';ctx.lineWidth=1;
 ctx.strokeStyle='#222';ctx.fillStyle='#8b949e';ctx.textAlign='right';
 for(let d=Math.ceil((ytop-yr)/10)*10;d<=ytop;d+=10){const y=Y(d);ctx.beginPath();ctx.moveTo(L,y);ctx.lineTo(W-R,y);ctx.stroke();ctx.fillText(d,L-3*r,y+4*r)}
 const span=f1-f0,st=span>200?50:span>100?20:span>40?10:span>15?5:1;ctx.textAlign='center';
 for(let f=Math.ceil(f0/st)*st;f<=f1;f+=st){const x=X(f);ctx.beginPath();ctx.moveTo(x,T);ctx.lineTo(x,T+ph);ctx.stroke();ctx.fillText(f,x,H-6*r)}
 let lane=0;for(const b of BKEYS){if(!shown(b))continue;ctx.strokeStyle=BCOL[b]+'66';ctx.fillStyle=BCOL[b];
  BANDS[b].forEach((f,k)=>{if(f<f0||f>f1)return;const x=X(f);ctx.beginPath();ctx.moveTo(x,T);ctx.lineTo(x,T+ph);ctx.stroke();ctx.fillText(b+(k+1),x,T+(11+lane*11)*r)});lane++}
 const line=(t,col,a,b)=>{if(!t)return;ctx.strokeStyle=col;ctx.lineWidth=1.2*r;ctx.beginPath();const n=t.length;let first=true;
  for(let i=0;i<n;i++){const f=a+(b-a)*(i+0.5)/n;if(f<f0||f>f1)continue;const x=X(f),y=Y(dbm(t[i]));first?ctx.moveTo(x,y):ctx.lineTo(x,y);first=false}ctx.stroke();ctx.lineWidth=1};
 if(noise)line(noise.t,'#9aa4b199',noise.a,noise.b);
 if(useHold())line(peak,'#ff7b72',f0,f1);
 line(trace(),useAvg()?'#79c0ff':'#f2cc60',f0,f1);
 markers.forEach((f,m)=>{if(f===null)return;const lv=level(f);if(lv===null)return;const x=X(f),y=Y(lv);
  ctx.fillStyle=MCOL[m];ctx.beginPath();ctx.moveTo(x,y-2*r);ctx.lineTo(x-6*r,y-12*r);ctx.lineTo(x+6*r,y-12*r);ctx.fill();ctx.fillText('M'+(m+1),x,y-15*r)});
}
function renderMarkers(){let h='';markers.forEach((f,m)=>{const lv=f===null?null:level(f);
 h+=`<tr style="color:${MCOL[m]}"><td>M${m+1}${m===act?' ◀':''}</td><td>${f===null?'-':f.toFixed(1)}</td><td>${lv===null?'-':lv.toFixed(1)}</td><td>${f===null?'-':nearestCh(f)}</td></tr>`});$('mk').innerHTML=h}
// ---- FPV table: frequencies only; rebuilt when markers move, not per frame
function renderFpv(){let h='<tr><th>Band</th>'+[1,2,3,4,5,6,7,8].map(c=>'<th>CH'+c+'</th>').join('')+'</tr>';
 for(const b of BKEYS){h+=`<tr><th style="color:${BCOL[b]}" title="${BNAME[b]}">${b}</th>`;
  BANDS[b].forEach(f=>{const m=markers.indexOf(f);h+=`<td class="ch${m>=0?' mk':''}" data-f="${f}"${m>=0?` style="color:${MCOL[m]};outline:2px solid ${MCOL[m]}"`:''}>${f}</td>`});h+='</tr>'}
 $('fpv').innerHTML=h}
$('fpv').onclick=e=>{const td=e.target.closest('td.ch');if(!td)return;markers[act]=+td.dataset.f;placed()};
function placed(){renderFpv();renderMarkers();draw();
 const saved=(cfg.markers||[]).map(t=>t<0?null:t/10);$('msave').classList.toggle('dirty',markers.some((f,m)=>f!==(saved[m]??null)))}
// ---- applied settings line
let pending=null;
function showApplied(){const s=cfg;if(!s.start)return;
 // Values the S3 clamps or rejects never match exactly, so a second report also ends the wait.
 const val=k=>/^m[1-4]$/.test(k)?s.markers[k[1]-1]:+s[k];
 if(pending&&(s.error||++pending.n>=2||Object.keys(pending.v).every(k=>val(k)===pending.v[k])))pending=null;
 const g=s.gain<0?'ゲイン自動(AGC)':'ゲイン '+s.gain;
 let h=`適用中: <b>${s.start}〜${s.stop} MHz</b> · <b>${FS[s.rate]} MS/s</b> · ${s.step_khz/1000} MHz幅 × <b>${s.steps}</b> ステップ · 滞在 ${s.dwell} ms · ${s.det?'最大値':'平均'} · ${g} · 補正 ${(s.cal/10).toFixed(1)} dB · 1スイープ <b>${s.sweep_ms}</b> ms`;
 if(pending)h+=' <span class="wait">（変更を反映中…）</span>';
 if(s.error)h+=` <span class="err">${s.error}</span>`;
 $('applied').innerHTML=h;$('nst').textContent=s.noise?'保存済み':'未保存'}
// ---- forms: filled from the saved settings once, then edited locally until applied
function fillForm(){const s=cfg;$('start').value=s.start;$('stop').value=s.stop;syncCenter();
 $('rate').value=s.rate;$('dwell').value=s.dwell;$('det').value=s.det;$('gain').value=s.gain;$('cal').value=(s.cal/10).toFixed(1);
 document.querySelectorAll('#bandsel input').forEach(i=>i.checked=shown(i.dataset.b));
 $('hold').checked=!!s.hold;$('avg').checked=!!s.avg;$('ytop').value=s.ytop;$('yrng').value=s.yrange;
 if(s.markers)markers=s.markers.map(t=>t<0?null:t/10);renderFpv();
 document.querySelectorAll('.dirty').forEach(b=>b.classList.remove('dirty'))}
function syncCenter(){$('center').value=(+$('start').value + +$('stop').value)/2;$('span').value=$('stop').value-$('start').value;$('applys').classList.add('dirty')}
function syncEdges(){const c=+$('center').value,w=+$('span').value;$('start').value=Math.round(c-w/2);$('stop').value=Math.round(c+w/2);$('applys').classList.add('dirty')}
function send(text){if(!(ws&&ws.readyState===1)){alert('S3に接続されていません');return false}ws.send(text);return true}
function apply(text,btn){if(!send(text))return;btn.classList.remove('dirty');
 pending={n:0,v:{}};text.slice(4).split(' ').forEach(kv=>{const [k,v]=kv.split('=');pending.v[k]=+v});showApplied()}
$('start').oninput=$('stop').oninput=syncCenter;$('center').oninput=$('span').oninput=syncEdges;
document.querySelectorAll('.pre').forEach(b=>b.onclick=()=>{$('start').value=b.dataset.a;$('stop').value=b.dataset.b;syncCenter()});
$('zoom').onclick=()=>{const f=markers[act];if(f===null)return;$('center').value=Math.round(f);$('span').value=40;syncEdges()};
$('applys').onclick=()=>{let a=Math.round(+$('start').value),b=Math.round(+$('stop').value);if(b-a<5)b=a+5;apply(`cfg start=${a} stop=${b}`,$('applys'))};
['rate','dwell','det','gain','cal'].forEach(id=>$(id).oninput=()=>$('applyr').classList.add('dirty'));
$('applyr').onclick=()=>apply(`cfg rate=${$('rate').value} dwell=${$('dwell').value} det=${$('det').value} gain=${$('gain').value} cal=${Math.round(+$('cal').value*10)}`,$('applyr'));
const bs=$('bandsel');for(const b of BKEYS){const l=document.createElement('label');l.innerHTML=`<input type="checkbox" data-b="${b}"> <span style="color:${BCOL[b]}">${b}</span> `;
 l.firstChild.onchange=()=>$('applyb').classList.add('dirty');bs.appendChild(l)}
$('applyb').onclick=()=>{let m=0;document.querySelectorAll('#bandsel input').forEach(i=>{if(i.checked)m|=1<<BKEYS.indexOf(i.dataset.b)});apply(`cfg bands=${m}`,$('applyb'))};
document.querySelectorAll('.mb').forEach(b=>b.onclick=()=>{act=+b.dataset.m;document.querySelectorAll('.mb').forEach(x=>x.classList.toggle('on',x===b));renderMarkers()});
$('mpk').onclick=()=>{const t=trace();if(!t)return;let k=0;for(let i=1;i<t.length;i++)if(t[i]>t[k])k=i;markers[act]=Math.round(freqAt(k,t.length)*10)/10;placed()};
$('mclr').onclick=()=>{markers[act]=null;placed()};
['hold','avg','ytop','yrng'].forEach(id=>$(id).oninput=()=>$('applyd').classList.add('dirty'));
$('applyd').onclick=()=>{if($('hold').checked&&!useHold())peak=null;
 apply(`cfg hold=${+$('hold').checked} avg=${+$('avg').checked} ytop=${Math.round(+$('ytop').value)} yrange=${Math.round(+$('yrng').value)}`,$('applyd'))};
$('msave').onclick=()=>apply('cfg '+markers.map((f,m)=>`m${m+1}=${f===null?-1:Math.round(f*10)}`).join(' '),$('msave'));
cv.addEventListener('pointerdown',e=>{const r=cv.getBoundingClientRect(),L=44,R=10,x=e.clientX-r.left;
 const f=f0+(x-L)/(r.width-L-R)*(f1-f0);if(f<f0||f>f1)return;markers[act]=Math.round(f*10)/10;placed()});
// ---- noise reference: the running average of the current span
$('nsave').onclick=()=>{if(!avg){alert('スペクトラムを受信してから保存してください');return}
 const n=avg.length,b=new ArrayBuffer(24+n),v=new DataView(b);[78,83,69,50].forEach((c,i)=>v.setUint8(i,c));
 v.setUint32(4,Math.round(f0*1000),true);v.setUint32(8,Math.round(f1*1000),true);v.setUint16(12,n,true);
 const t=new Uint8Array(b,24);avg.forEach((x,i)=>t[i]=Math.round(x));if(send(b))$('nst').textContent='保存中…'};
$('nclr').onclick=()=>{if(send('noise clear'))noise=null;draw()};
// ---- websocket
let ws;function connect(){ws=new WebSocket('ws://'+location.hostname+':81/');ws.binaryType='arraybuffer';
 ws.onmessage=e=>{if(typeof e.data==='string'){const s=JSON.parse(e.data);if(s.type!=='status')return;
   const first=!cfg.start;if(cfg.rate!==s.rate||cfg.det!==s.det||cfg.gain!==s.gain||cfg.dwell!==s.dwell){peak=null;avg=null}
   if(!s.noise)noise=null;cfg=s;$('st').textContent=s.linked?s.info:'C5未接続';$('st').className=s.linked?'ok':'ng';
   if(first)fillForm();if(s.error)$('gain').value=s.gain;if(s.gain_max)$('grange').textContent=`(-1=自動, 2〜${s.gain_max})`;
   showApplied();draw();renderMarkers();if(first)placed();return}
  const v=new DataView(e.data),magic=v.getUint32(0,true);
  const a=v.getUint32(4,true)/1000,b=v.getUint32(8,true)/1000,n=v.getUint16(12,true),t=new Uint8Array(e.data,24,n);
  if(magic===0x3245534e){noise={a,b,t:Uint8Array.from(t)};draw();return}
  if(magic!==0x31565046)return;
  if(a!==f0||b!==f1||!data||data.length!==n){f0=a;f1=b;peak=null;avg=null}
  data=Uint8Array.from(t);
  // Wide signals near -30 dBFS per bin already add up to ADC full scale.
  let top=0;for(let i=0;i<n;i++)if(t[i]>top)top=t[i];$('warn').style.display=top/2-84.3>-30?'':'none';
  peak=peak?peak.map((p,i)=>Math.max(p,t[i])):Uint8Array.from(t);
  avg=avg?avg.map((p,i)=>p*0.8+t[i]*0.2):Float32Array.from(t);
  draw();renderMarkers()};
 ws.onclose=()=>{$('st').textContent='切断（再接続中）';$('st').className='ng';setTimeout(connect,1500)}}
window.onresize=resize;resize();renderFpv();renderMarkers();connect();
</script></body></html>)HTML";
