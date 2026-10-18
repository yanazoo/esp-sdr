#pragma once
/* Single-page UI served at "/". Spectrum frames arrive on ws://<ip>:81/.
 * Levels are shown in dBm: dBFS plus the saved calibration offset. */
static const char INDEX_HTML[] PROGMEM = R"HTML(<!doctype html>
<html lang="ja"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>5.8GHz FPV Scanner</title>
<style>
*{box-sizing:border-box}body{margin:0;background:#0d1117;color:#e6edf3;font:14px system-ui,sans-serif}
header{display:flex;gap:8px;justify-content:space-between;padding:6px 10px;background:#161b22;font-size:12px;color:#8b949e}
header .err{flex:1;text-align:center}
#st.ok{color:#3fb950}#st.ng,.err{color:#f85149}
main{padding:6px;max-width:1100px;margin:auto}
#cv{width:100%;height:45vh;min-height:240px;background:#000;border:1px solid #30363d;touch-action:none;display:block}
.box{background:#161b22;border:1px solid #30363d;border-radius:6px;padding:6px;margin:6px 0}
.row{display:flex;flex-wrap:wrap;gap:6px;align-items:center;margin:4px 0}
input,select,button{background:#0d1117;color:#e6edf3;border:1px solid #30363d;border-radius:4px;padding:5px;font-size:14px}
input[type=number]{width:72px}button.on{background:#1f6feb}button.dirty{border-color:#d29922;color:#d29922}
table{border-collapse:collapse;width:100%;font-size:12px}td,th{border:1px solid #30363d;padding:4px 2px;text-align:center}
td.ch{cursor:pointer}#mt td:first-child,#mt th:first-child{width:1%;white-space:nowrap;padding:4px 6px}#mt td,#mt th{width:33%}
</style></head><body>
<header><span id="info">-</span><span id="err" class="err"></span><span id="st">接続中…</span></header>
<main>
<div class="row">
 <label><input type="checkbox" id="hold">ピークホールド</label><button id="preset">リセット</button><label><input type="checkbox" id="avg">平均</label>
 <label>上限 <input type="number" id="ytop"> dBm</label><label>範囲 <input type="number" id="yrng"> dB</label>
</div>
<canvas id="cv"></canvas>

<div class="box">
<div class="row">
 <button class="mb on" data-m="0" style="color:#ff6">M1</button><button class="mb" data-m="1" style="color:#6ff">M2</button>
 <button class="mb" data-m="2" style="color:#f6f">M3</button><button class="mb" data-m="3" style="color:#6f6">M4</button>
 <button id="mpk">ピーク</button><button id="mclr">消去</button><button id="msave">保存</button>
</div>
<table id="mt"><tr><th>マーカー</th><th>dBm</th><th>ch</th><th>MHz</th></tr><tbody id="mk"></tbody></table>
<div style="overflow-x:auto;margin-top:6px"><table id="fpv"></table></div>
<div class="row">表示バンド <span id="bandsel"></span><button id="applyb">適用</button></div>
</div>

<div class="box"><div class="row">
 <label>開始 <input type="number" id="start"></label><label>終了 <input type="number" id="stop"></label> MHz
 <button class="pre" data-a="5650" data-b="5850">5650-5850</button><button class="pre" data-a="5640" data-b="5950">5640-5950</button>
 <button id="applys">適用</button>
</div></div>

<div class="box"><div class="row">
 <label>ステップ <select id="rate"><option value="0">40MHz</option><option value="1">30MHz</option><option value="2">15MHz</option><option value="3">7.5MHz</option></select></label>
 <label>滞在 <input type="number" id="dwell"> ms</label>
 <label>検波 <select id="det"><option value="1">最大</option><option value="0">平均</option></select></label>
 <label>ゲイン <input type="number" id="gain"> <span id="grange" style="font-size:12px;color:#8b949e">-1=自動、2〜（小さいほど強い電波向き、大きいほど弱い電波向き）</span></label>
 <label>補正 <input type="number" id="cal" step="0.1"> dB</label>
 <button id="applyr">適用</button>
</div></div>

<div class="box"><div class="row">
 ノイズ <button id="nsave">保存</button><button id="nclr">消去</button><span id="nst"></span>
</div></div>

</main>
<script>
const BANDS={A:[5865,5845,5825,5805,5785,5765,5745,5725],B:[5733,5752,5771,5790,5809,5828,5847,5866],
E:[5705,5685,5665,5645,5885,5905,5925,5945],F:[5740,5760,5780,5800,5820,5840,5860,5880],R:[5658,5695,5732,5769,5806,5843,5880,5917]};
const BK=Object.keys(BANDS),BCOL={A:'#ff9e64',B:'#9ece6a',E:'#bb9af7',F:'#7dcfff',R:'#f7768e'},MCOL=['#ff6','#6ff','#f6f','#6f6'],MBG=['#ffff6633','#66ffff33','#ff66ff33','#66ff6633'];
let warn='';
let cfg={},data=null,peak=null,avg=null,noise=null,f0=5650,f1=5850,act=0,markers=[null,null,null,null],ws,lastMsg=0,queued=false;
const $=id=>document.getElementById(id),cv=$('cv'),ctx=cv.getContext('2d');
const dbm=c=>c/2-84.3+(cfg.cal||0)/10;
const shown=b=>((cfg.bands??17)>>BK.indexOf(b))&1;
const trace=()=>cfg.avg&&avg?avg:data;
function level(f){const t=trace();return !t||f<f0||f>=f1?null:dbm(t[Math.floor((f-f0)/(f1-f0)*t.length)])}
function nearestCh(f){let b=null;for(const k of BK)BANDS[k].forEach((c,i)=>{const d=Math.abs(c-f);if(!b||d<b.d)b={d,t:k+(i+1)}});return b.d<=10?b.t:'-'}
// ---- drawing, at most once per animation frame
function redraw(){if(!queued){queued=true;requestAnimationFrame(()=>{queued=false;draw();renderMarkers()})}}
function resize(){const r=devicePixelRatio||1;cv.width=cv.clientWidth*r;cv.height=cv.clientHeight*r;redraw()}
function draw(){
 const W=cv.width,H=cv.height,r=devicePixelRatio||1,L=36*r,T=4*r,B=18*r,pw=W-L-6*r,ph=H-T-B;
 const top=cfg.ytop??-17,yr=cfg.yrange||75,X=f=>L+(f-f0)/(f1-f0)*pw,Y=d=>Math.min(T+ph,Math.max(T,T+(top-d)/yr*ph));
 ctx.fillStyle='#000';ctx.fillRect(0,0,W,H);ctx.font=(10*r)+'px system-ui';ctx.lineWidth=1;ctx.strokeStyle='#222';ctx.fillStyle='#8b949e';
 ctx.textAlign='right';for(let d=Math.ceil((top-yr)/10)*10;d<=top;d+=10){const y=Y(d);ctx.beginPath();ctx.moveTo(L,y);ctx.lineTo(W,y);ctx.stroke();ctx.fillText(d,L-2*r,y+3*r)}
 const st=f1-f0>200?50:f1-f0>100?20:f1-f0>40?10:5;ctx.textAlign='center';
 for(let f=Math.ceil(f0/st)*st;f<=f1;f+=st){const x=X(f);ctx.beginPath();ctx.moveTo(x,T);ctx.lineTo(x,T+ph);ctx.stroke();ctx.fillText(f,x,H-4*r)}
 let lane=0;for(const b of BK){if(!shown(b))continue;ctx.strokeStyle=BCOL[b]+'66';ctx.fillStyle=BCOL[b];
  BANDS[b].forEach((f,k)=>{if(f<f0||f>f1)return;const x=X(f);ctx.beginPath();ctx.moveTo(x,T);ctx.lineTo(x,T+ph);ctx.stroke();ctx.fillText(b+(k+1),x,T+(10+lane*10)*r)});lane++}
 const line=(t,col,a,b)=>{if(!t)return;ctx.strokeStyle=col;ctx.beginPath();const n=t.length;let first=true;
  for(let i=0;i<n;i++){const f=a+(b-a)*(i+0.5)/n;if(f<f0||f>f1)continue;const x=X(f),y=Y(dbm(t[i]));first?ctx.moveTo(x,y):ctx.lineTo(x,y);first=false}ctx.stroke()};
 if(noise)line(noise.t,'#9aa4b199',noise.a,noise.b);
 if(cfg.hold)line(peak,'#ff7b72',f0,f1);
 line(trace(),cfg.avg?'#79c0ff':'#f2cc60',f0,f1);
 markers.forEach((f,m)=>{const lv=f===null?null:level(f);if(lv===null)return;const x=X(f),y=Y(lv);
  ctx.fillStyle=MCOL[m];ctx.beginPath();ctx.moveTo(x,y-2*r);ctx.lineTo(x-5*r,y-11*r);ctx.lineTo(x+5*r,y-11*r);ctx.fill();ctx.fillText('M'+(m+1),x,y-13*r)});
}
function renderMarkers(){$('mk').innerHTML=markers.map((f,m)=>{const lv=f===null?null:level(f);
 return `<tr style="color:${MCOL[m]}"><td>M${m+1}${m===act?'◀':''}</td><td>${lv===null?'-':lv.toFixed(1)}</td><td>${f===null?'-':nearestCh(f)}</td><td>${f===null?'-':f.toFixed(1)}</td></tr>`}).join('')}
function renderFpv(){$('fpv').innerHTML='<tr><th></th>'+[1,2,3,4,5,6,7,8].map(c=>'<th>'+c+'</th>').join('')+'</tr>'+BK.map(b=>`<tr><th style="color:${BCOL[b]}">${b}</th>`+
 BANDS[b].map(f=>{const m=markers.indexOf(f);return `<td class="ch" data-f="${f}"${m>=0?` style="outline:2px solid ${MCOL[m]};background:${MBG[m]}"`:''}>${f}</td>`}).join('')+'</tr>').join('')}
function placed(){renderFpv();redraw();const s=(cfg.markers||[]).map(t=>t<0?null:t/10);$('msave').classList.toggle('dirty',markers.some((f,m)=>f!==(s[m]??null)))}
// ---- settings: forms are filled from the saved values once, then edited until applied
function fillForm(s){$('start').value=s.start;$('stop').value=s.stop;$('rate').value=s.rate;$('dwell').value=s.dwell;$('det').value=s.det;
 $('gain').value=s.gain;$('cal').value=(s.cal/10).toFixed(1);$('hold').checked=!!s.hold;$('avg').checked=!!s.avg;$('ytop').value=s.ytop;$('yrng').value=s.yrange;
 document.querySelectorAll('#bandsel input').forEach(i=>i.checked=shown(i.dataset.b));markers=s.markers.map(t=>t<0?null:t/10);placed()}
function send(t){if(ws&&ws.readyState===1){ws.send(t);return true}alert('S3に接続されていません');return false}
function apply(t,btn){if(send(t))btn.classList.remove('dirty')}
const dirtyOn=(ids,btn)=>ids.forEach(id=>$(id).oninput=()=>$(btn).classList.add('dirty'));
dirtyOn(['start','stop'],'applys');dirtyOn(['rate','dwell','det','gain','cal'],'applyr');
document.querySelectorAll('.pre').forEach(b=>b.onclick=()=>{$('start').value=b.dataset.a;$('stop').value=b.dataset.b;$('applys').classList.add('dirty')});
$('applys').onclick=()=>{const a=Math.round(+$('start').value),b=Math.max(a+5,Math.round(+$('stop').value));apply(`cfg start=${a} stop=${b}`,$('applys'))};
$('applyr').onclick=()=>apply(`cfg rate=${$('rate').value} dwell=${$('dwell').value} det=${$('det').value} gain=${$('gain').value} cal=${Math.round(+$('cal').value*10)}`,$('applyr'));
// Display settings take effect at once; the S3 saves them after typing pauses.
let dispTimer,dispEdit=0;
function display(){const top=Math.round(+$('ytop').value),yr=Math.round(+$('yrng').value);if(!isFinite(top)||!(yr>=10))return;
 dispEdit=Date.now();if($('hold').checked&&!cfg.hold)peak=null;cfg.hold=+$('hold').checked;cfg.avg=+$('avg').checked;cfg.ytop=top;cfg.yrange=yr;redraw();
 clearTimeout(dispTimer);dispTimer=setTimeout(()=>{if(ws&&ws.readyState===1)ws.send(`cfg hold=${cfg.hold} avg=${cfg.avg} ytop=${top} yrange=${yr}`)},500)}
['hold','avg','ytop','yrng'].forEach(id=>$(id).oninput=display);
$('preset').onclick=()=>{peak=null;redraw()};
for(const b of BK){const l=document.createElement('label');l.innerHTML=`<input type="checkbox" data-b="${b}"><span style="color:${BCOL[b]}">${b}</span> `;
 l.firstChild.onchange=()=>$('applyb').classList.add('dirty');$('bandsel').appendChild(l)}
$('applyb').onclick=()=>{let m=0;document.querySelectorAll('#bandsel input').forEach(i=>{if(i.checked)m|=1<<BK.indexOf(i.dataset.b)});apply(`cfg bands=${m}`,$('applyb'))};
// ---- markers
document.querySelectorAll('.mb').forEach(b=>b.onclick=()=>{act=+b.dataset.m;document.querySelectorAll('.mb').forEach(x=>x.classList.toggle('on',x===b));redraw()});
$('fpv').onclick=e=>{const td=e.target.closest('td.ch');if(td){markers[act]=+td.dataset.f;placed()}};
cv.onpointerdown=e=>{const r=cv.getBoundingClientRect(),f=f0+(e.clientX-r.left-36)/(r.width-42)*(f1-f0);if(f>=f0&&f<=f1){markers[act]=Math.round(f*10)/10;placed()}};
$('mpk').onclick=()=>{const t=trace();if(!t)return;let k=0;t.forEach((v,i)=>{if(v>t[k])k=i});markers[act]=Math.round((f0+(f1-f0)*(k+0.5)/t.length)*10)/10;placed()};
$('mclr').onclick=()=>{markers[act]=null;placed()};
$('msave').onclick=()=>apply('cfg '+markers.map((f,m)=>`m${m+1}=${f===null?-1:Math.round(f*10)}`).join(' '),$('msave'));
// ---- noise reference: the running average of the current span
$('nsave').onclick=()=>{if(!avg)return;const n=avg.length,b=new ArrayBuffer(24+n),v=new DataView(b);[78,83,69,50].forEach((c,i)=>v.setUint8(i,c));
 v.setUint32(4,Math.round(f0*1000),true);v.setUint32(8,Math.round(f1*1000),true);v.setUint16(12,n,true);new Uint8Array(b,24).set(avg.map(Math.round));send(b)};
$('nclr').onclick=()=>{if(send('noise clear')){noise=null;redraw()}};
// ---- websocket; a connection silent for 5 s is replaced
function onStatus(s){
 // Until the S3 has the latest display edit, a status report must not undo it.
 if(Date.now()-dispEdit<3000)for(const k of['hold','avg','ytop','yrange'])s[k]=cfg[k];const first=!cfg.start;if(cfg.rate!==s.rate||cfg.det!==s.det||cfg.gain!==s.gain){peak=null;avg=null}
 if(!s.noise)noise=null;cfg=s;if(first)fillForm(s);
 $('st').textContent=s.linked?'C5接続':'C5未接続';$('st').className=s.linked?'ok':'ng';
 $('info').textContent=`${s.start}-${s.stop}MHz ${s.steps}step ${s.sweep_ms}ms`;$('nst').textContent=s.noise?'保存済み':'';
 gerr=s.error;showErr();if(s.error)$('gain').value=s.gain;
 if(s.gain_max)$('grange').textContent=`-1=自動、2〜${s.gain_max}（小さいほど強い電波向き、大きいほど弱い電波向き）`;placed()}
// Errors and warnings share the header bar.
let gerr='';function showErr(){$('err').textContent=[gerr,warn].filter(Boolean).join(' / ')}
function onFrame(buf){const v=new DataView(buf),magic=v.getUint32(0,true),a=v.getUint32(4,true)/1000,b=v.getUint32(8,true)/1000,t=new Uint8Array(buf,24,v.getUint16(12,true));
 if(magic===0x3245534e){noise={a,b,t:Uint8Array.from(t)};redraw();return}
 if(magic!==0x31565046)return;
 if(a!==f0||b!==f1||!data||data.length!==t.length){f0=a;f1=b;peak=null;avg=null}
 data=Uint8Array.from(t);peak=peak?peak.map((p,i)=>Math.max(p,t[i])):Uint8Array.from(t);avg=avg?avg.map((p,i)=>p*0.8+t[i]*0.2):Float32Array.from(t);
 // Wide signals near -30 dBFS per bin already add up to ADC full scale.
 const w=Math.max(...t)/2-84.3>-30?'入力が強すぎます。ゲインを下げてください':'';if(w!==warn){warn=w;showErr()}redraw()}
function connect(){ws=new WebSocket('ws://'+location.hostname+':81/');ws.binaryType='arraybuffer';lastMsg=Date.now();
 ws.onmessage=e=>{lastMsg=Date.now();typeof e.data==='string'?onStatus(JSON.parse(e.data)):onFrame(e.data)};
 ws.onclose=()=>{$('st').textContent='再接続中';$('st').className='ng';setTimeout(connect,1000)}}
setInterval(()=>{if(ws&&ws.readyState===1&&Date.now()-lastMsg>5000)ws.close()},1000);
document.addEventListener('visibilitychange',()=>{if(!document.hidden&&ws&&Date.now()-lastMsg>5000)ws.close()});
window.onresize=resize;resize();renderFpv();connect();
</script></body></html>)HTML";
