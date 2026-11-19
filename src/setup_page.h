#pragma once
// The setup page served on the panel's setup hotspot (net.cpp). One self-contained file: no external
// CSS, JS or fonts, because the phone has no internet while it is on the hotspot.
// Field names must match the keys in config.cpp. Values are only ever put into the page with
// .value / .textContent (never innerHTML), so names from Home Assistant can't inject markup.
#include <pgmspace.h>

static const char SETUP_HTML[] PROGMEM = R"HTML(<!doctype html>
<html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Panel setup</title>
<style>
:root{--bg:#f4f5f7;--card:#fff;--fg:#1d2330;--dim:#667085;--line:#d6dae1;--acc:#1f6feb;--ok:#1a7f37;--bad:#c62828}
@media (prefers-color-scheme:dark){:root{--bg:#111418;--card:#1b2027;--fg:#e8ebef;--dim:#9aa3af;--line:#323a45;--acc:#4c8dff;--ok:#3fb950;--bad:#ff6b6b}}
*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--fg);font:16px/1.4 system-ui,-apple-system,Segoe UI,Roboto,sans-serif}
main{max-width:640px;margin:0 auto;padding:12px 16px 120px}
h1{font-size:22px;margin:8px 0 4px}h2{font-size:18px;margin:0 0 8px}p{margin:4px 0 10px}
.card{background:var(--card);border:1px solid var(--line);border-radius:12px;padding:14px;margin:14px 0}
label{display:block;font-weight:600;margin:10px 0 4px}small,.dim{color:var(--dim);font-weight:400}
input,select{width:100%;font:inherit;padding:10px;border:1px solid var(--line);border-radius:8px;background:var(--bg);color:var(--fg)}
input[type=checkbox]{width:auto;margin-right:6px}
button{font:inherit;font-weight:600;padding:10px 16px;border-radius:8px;border:1px solid var(--line);background:var(--card);color:var(--fg);margin:8px 8px 0 0}
button.pri{background:var(--acc);border-color:var(--acc);color:#fff}
details{border-top:1px solid var(--line);padding:6px 0}summary{cursor:pointer;font-weight:600;padding:6px 0}
.msg{margin-top:8px;font-weight:600}.ok{color:var(--ok)}.bad{color:var(--bad)}
.row{display:flex;gap:8px}.row>*{flex:1}
#bar{position:fixed;left:0;right:0;bottom:0;background:var(--card);border-top:1px solid var(--line);padding:8px 16px 12px;text-align:center}
#bar button{margin:4px}
</style></head><body><main>
<h1>Panel setup</h1>
<p class="dim">Fill in what you have. Empty boxes are not used. Nothing changes on the panel until you press <b>Save and restart</b>.</p>

<div class="card"><h2>1. Wi-Fi</h2>
<p class="dim">The home Wi-Fi the panel should join (2.4 GHz only).</p>
<label for="ssid">Network</label>
<select id="pick"><option value="">Searching for networks...</option></select>
<input id="ssid" maxlength="32" placeholder="or type the network name" autocomplete="off" style="margin-top:6px">
<button type="button" id="rescan">Search again</button>
<label for="wpass">Password</label>
<input id="wpass" type="password" maxlength="64" autocomplete="new-password">
<label><input type="checkbox" id="open">This network has no password</label>
<button type="button" class="pri" id="join">Join this Wi-Fi</button>
<div class="msg" id="wmsg"></div></div>

<div class="card"><h2>2. Home Assistant</h2>
<label for="host">IP address <small>(e.g. 192.168.1.50 - "homeassistant.local" doesn't work)</small></label>
<input id="host" maxlength="63" inputmode="decimal" autocomplete="off">
<label for="port">Port</label><input id="port" type="number" min="1" max="65535">
<label for="token">Long-lived access token</label>
<input id="token" type="password" maxlength="300" autocomplete="off">
<p class="dim">In Home Assistant: your profile (bottom left) &rarr; Security &rarr; Long-lived access tokens &rarr; Create token.</p>
<button type="button" class="pri" id="test">Test connection</button>
<div class="msg" id="tmsg"></div>
<p class="dim">Once the test works, the boxes below suggest your entities as you type.</p></div>

<div id="groups"></div>

<div class="card"><h2>Start over</h2>
<p class="dim">Forget everything saved here and go back to the settings the panel was built with. Screen rotation and brightness are kept.</p>
<button type="button" id="reset">Reset to file defaults</button></div>
</main>
<div id="bar"><div class="msg" id="smsg"></div><button type="button" class="pri" id="save" disabled>Save and restart</button></div>

<datalist id="dl_sensor"></datalist><datalist id="dl_switch"></datalist>
<datalist id="dl_climate"></datalist><datalist id="dl_weather"></datalist>

<script>
const $=id=>document.getElementById(id);
const S='sensor',W='switch';
const G=[
 ['Home page',[['e_tin','Indoor temperature',S],['e_tout','Outdoor temperature',S],['e_wthr','Weather','weather'],['e_clim','Heating thermostat','climate']]],
 ['Energy (Octopus Energy integration)',[['e_rate','Current rate',S],['e_next','Next rate',S],['e_kwh',"Today's usage (kWh)",S],['e_cost',"Today's cost",S],['e_dem','Live demand (needs a Home Mini)',S]]]
];
const NUMS=['tp1','tp2','tp3','tp4','alertp'];
let cfg={v:{},has:{}};

function msg(id,t,ok){const m=$(id);m.textContent=t||'';m.className='msg '+(ok===true?'ok':ok===false?'bad':'');}
function field(box,key,label,dom,hint){
  const l=document.createElement('label');l.htmlFor=key;l.textContent=label;
  if(hint){const s=document.createElement('small');s.textContent=' '+hint;l.appendChild(s);}
  let i;
  if(NUMS.includes(key)){
    i=document.createElement('select');
    for(let n=0;n<=10;n++){const o=document.createElement('option');o.value=n;o.textContent=n?'Plug '+n:'None';i.appendChild(o);}
  }else{
    i=document.createElement('input');i.autocomplete='off';i.autocapitalize='off';i.spellcheck=false;
    if(dom){i.setAttribute('list','dl_'+dom);i.maxLength=100;i.placeholder=dom+'.';i.dataset.ent=1;}else i.maxLength=31;
  }
  i.id=key;i.dataset.k=key;box.appendChild(l);box.appendChild(i);
}
function card(title,note){const c=document.createElement('div');c.className='card';const h=document.createElement('h2');h.textContent=title;c.appendChild(h);
  if(note){const p=document.createElement('p');p.className='dim';p.textContent=note;c.appendChild(p);}$('groups').appendChild(c);return c;}
function sub(c,title){const d=document.createElement('details');const s=document.createElement('summary');s.textContent=title;d.appendChild(s);c.appendChild(d);return d;}

function build(){
  for(const [t,fs] of G){const c=card(t);for(const f of fs)field(c,f[0],f[1],f[2]);}
  let c=card('Smart plugs','Name = what the screen shows (about 13 letters max). Power = the plug\'s power sensor in watts. Cost = optional Octopus cost tracker.');
  for(let n=1;n<=10;n++){const d=sub(c,'Plug '+n);d.dataset.p='pn'+n;
    field(d,'pn'+n,'Name');field(d,'p_p'+n,'Power sensor',S);field(d,'c_p'+n,'Cost sensor',S,'(optional)');}
  c=card('Controls page: 4 buttons','Only things that are safe to switch off by accident (not a fridge or freezer!).');
  for(let n=1;n<=4;n++){const d=sub(c,'Button '+n);d.dataset.p='tn'+n;
    field(d,'tn'+n,'Name');field(d,'s_t'+n,'Switch',W);field(d,'tp'+n,'Show watts from');}
  c=card('"Appliance stopped" alert (optional)','Warns when this switch is on but its plug uses almost no power (e.g. a dehumidifier with a full tank). The panel only watches it, it never switches it.');
  field(c,'s_alert','Switch to watch',W);field(c,'alertp','Plug that measures it');
}
function fill(){
  for(const el of document.querySelectorAll('[data-k]')){const v=cfg.v[el.dataset.k];if(v!==undefined)el.value=v;}
  for(const d of document.querySelectorAll('details[data-p]'))d.open=!!cfg.v[d.dataset.p];
  $('ssid').value=cfg.v.ssid||'';$('host').value=cfg.v.host||'';$('port').value=cfg.v.port||8123;
  $('wpass').placeholder=cfg.has.wpass?'saved - leave empty to keep':'';
  $('token').placeholder=cfg.has.token?'saved - leave empty to keep':'paste the token here';
  sta(cfg.sta);
}
function why(d){if(!d.key)return d.err||'Could not save.';const el=$(d.key);let n=d.key;
  if(el){const l=document.querySelector('label[for="'+d.key+'"]');if(l)n=l.firstChild.textContent.trim();const dt=el.closest('details');if(dt){dt.open=true;n=dt.firstChild.textContent+' - '+n;}el.focus();}
  return '"'+n+'" '+d.err;}
function sta(s){if(!s)return;
  if(s.state==='connected')msg('wmsg','On "'+s.ssid+'" ('+s.ip+')',true);
  else if(s.state==='connecting')msg('wmsg','Not connected to "'+s.ssid+'" yet.');
  else msg('wmsg','No Wi-Fi set up yet.');}

async function j(url,opt){const r=await fetch(url,opt);let d={};try{d=await r.json();}catch(e){}d._s=r.status;return d;}
function post(url,p){return j(url,{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded','X-Setup':'1'},body:new URLSearchParams(p).toString()});}
const sleep=ms=>new Promise(r=>setTimeout(r,ms));

async function scan(again){
  const pk=$('pick');
  for(let t=0;t<20;t++){
    const d=await j('/scan'+(again&&t==0?'?again=1':''));
    if(d.state==='done'){
      pk.textContent='';const o0=document.createElement('option');o0.value='';o0.textContent='Pick a network ('+d.nets.length+' found)';pk.appendChild(o0);
      const seen={};d.nets.sort((a,b)=>b.r-a.r);
      for(const n of d.nets){if(seen[n.s])continue;seen[n.s]=1;const o=document.createElement('option');o.value=n.s;
        o.textContent=n.s+(n.l?'':' (open)')+(n.r>-60?'  - strong':n.r>-75?'  - ok':'  - weak');pk.appendChild(o);}
      return;}
    await sleep(1500);
  }
}
$('pick').onchange=()=>{if($('pick').value)$('ssid').value=$('pick').value;};
$('rescan').onclick=()=>{$('pick').innerHTML='<option>Searching...</option>';scan(true);};

$('join').onclick=async()=>{
  const p={ssid:$('ssid').value};
  if($('open').checked)p.open='1';else if($('wpass').value)p.wpass=$('wpass').value;
  else if(p.ssid!==cfg.v.ssid||!cfg.has.wpass){msg('wmsg','Type the Wi-Fi password (or tick "no password").',false);return;}
  msg('wmsg','Joining... your phone may drop off this hotspot for a moment; reconnect to it if it does.');
  const d=await post('/wifi',p);
  if(!d.ok){msg('wmsg',why(d),false);return;}
  cfg.v.ssid=p.ssid;cfg.has.wpass=!p.open;$('wpass').value='';
  for(let t=0;t<20;t++){await sleep(1500);try{const s=await j('/status');if(s.sta&&s.sta.state==='connected'){sta(s.sta);return;}}catch(e){}}
  msg('wmsg','Could not join "'+p.ssid+'". Check the password and that it is a 2.4 GHz network.',false);
};

$('test').onclick=async()=>{
  msg('tmsg','Testing...');
  const d=await post('/test',{host:$('host').value,port:$('port').value,token:$('token').value});
  if(!d.ok){msg('tmsg',d.err||'Test failed.',false);return;}
  for(let t=0;t<40;t++){
    await sleep(1000);let s;try{s=await j('/status');}catch(e){continue;}
    if(s.test==='running'){if(t==3)msg('tmsg','Connected, loading your entities...');continue;}
    msg('tmsg',s.msg||'',s.test==='ok');
    if(s.test==='ok'&&s.n)loadEnts();
    return;
  }
  msg('tmsg','No answer. Check the IP address and port.',false);
};
async function loadEnts(){
  const d=await j('/entities');if(d._s!==200)return;
  for(const dom of ['sensor','switch','climate','weather']){
    const dl=$('dl_'+dom);dl.textContent='';
    for(const [id,name] of (d[dom+'.']||[])){const o=document.createElement('option');o.value=id;if(name)o.label=name;dl.appendChild(o);}
  }
}

$('save').onclick=async()=>{
  const p={};
  for(const el of document.querySelectorAll('[data-k]')){let v=el.value.trim();if(el.dataset.ent)v=v.toLowerCase();p[el.dataset.k]=v;}
  p.ssid=$('ssid').value;p.host=$('host').value.trim();p.port=$('port').value.trim();
  if($('token').value.trim())p.token=$('token').value.trim();
  if($('open').checked)p.open='1';else if($('wpass').value)p.wpass=$('wpass').value;
  if(p.ssid&&p.ssid!==cfg.v.ssid&&!p.open&&!p.wpass){msg('smsg','Type the Wi-Fi password for "'+p.ssid+'" (or tick "no password").',false);return;}
  msg('smsg','Saving...');
  const d=await post('/save',p);
  if(d.ok){msg('smsg','Saved. The panel is restarting and this hotspot will close.',true);$('save').disabled=true;}
  else msg('smsg',why(d),false);
};
$('reset').onclick=async()=>{
  if(!confirm('Forget everything saved on this page and go back to the built-in settings? The panel will restart.'))return;
  const d=await post('/reset',{confirm:'yes'});
  msg('smsg',d.ok?'Reset. The panel is restarting.':'Reset failed.',!!d.ok);
};

build();
j('/cfg').then(d=>{if(d._s!==200){msg('smsg','Could not load the current settings. Reload this page.',false);return;}cfg=d;fill();$('save').disabled=false;});
scan(false);
</script></body></html>)HTML";
