#pragma once
static const char DEBUG_LOG_PANEL_HTML[] PROGMEM = R"LOG(
<style>
#debugPanel{margin-top:14px}#debugPanel[hidden]{display:none}
#debugPanel.expanded{position:fixed;inset:12px;z-index:1000;margin:0;display:flex;flex-direction:column}
#debugPanel pre{background:#171c19;color:#d7efe5;padding:12px;height:260px;min-height:120px;max-height:70vh;overflow:auto;resize:vertical;white-space:pre-wrap;overflow-wrap:anywhere;font:13px/1.5 monospace}
#debugPanel.expanded pre{flex:1;height:auto;max-height:none;resize:none}
#debugPanel .logActions{display:flex;gap:8px;flex-wrap:wrap;margin:10px 0}
</style>
<section id="debugPanel" class="panel" hidden aria-label="DebugSerial Live-Log">
<h2>DebugSerial Live-Log</h2><p class="stamp">Serielle Firmware-Ausgabe dieses ESP32; Zeiten in Millisekunden seit Start.</p>
<div class="logActions"><button id="logExpand" type="button" aria-expanded="false">Vergrößern</button><button id="logPause" type="button">Pause</button><button id="logClear" type="button">Ansicht leeren</button><button id="logDownload" type="button">Log speichern</button><label><input id="logFollow" type="checkbox" checked> Automatisch scrollen</label></div>
<p id="logStatus" class="stamp">Verbinde …</p><pre id="logText" tabindex="0" aria-label="Serielle Ausgabe"></pre>
</section>
<script>
(()=>{
 const panel=document.getElementById('debugPanel'),view=document.getElementById('logText'),status=document.getElementById('logStatus');
 const expand=document.getElementById('logExpand'),pause=document.getElementById('logPause'),follow=document.getElementById('logFollow');
 let cursor=null,boot=null,paused=false;
 function add(text){view.textContent=(view.textContent+text).slice(-65536);if(follow.checked)view.scrollTop=view.scrollHeight;}
 function size(big){panel.classList.toggle('expanded',big);expand.textContent=big?'Verkleinern':'Vergrößern';expand.setAttribute('aria-expanded',String(big));}
 expand.onclick=()=>size(!panel.classList.contains('expanded'));
 document.addEventListener('keydown',e=>{if(e.key==='Escape')size(false);});
 pause.onclick=()=>{paused=!paused;pause.textContent=paused?'Fortsetzen':'Pause';status.textContent=paused?'Pausiert; der Gerätepuffer läuft weiter.':'Verbinde …';};
 document.getElementById('logClear').onclick=()=>{view.textContent='';};
 document.getElementById('logDownload').onclick=()=>{const url=URL.createObjectURL(new Blob([view.textContent],{type:'text/plain;charset=utf-8'}));const a=document.createElement('a');a.href=url;a.download='argus-'+location.hostname+'-debug.log';a.click();setTimeout(()=>URL.revokeObjectURL(url),1000);};
 async function poll(){try{
   if(paused||document.hidden)return;
   const params=cursor===null?'':('?cursor='+cursor+'&boot='+boot);
   const response=await fetch('/api/debug-log'+params,{cache:'no-store'});
   if(!response.ok)throw new Error('HTTP '+response.status);
   const data=await response.json();panel.hidden=!data.enabled;
   if(!data.enabled){cursor=null;boot=null;return;}
   if(boot!==null&&boot!==data.boot)add('\n--- ESP32 neu gestartet ---\n');
   if(data.lost)add('\n--- Ältere Ausgabe vom Gerätepuffer überschrieben ---\n');
   boot=data.boot;cursor=data.next;add(data.text);status.textContent='Live · '+new Date().toLocaleTimeString();
 }catch(e){status.textContent='Verbindung unterbrochen – erneuter Versuch folgt.';}finally{setTimeout(poll,1000);}}
 poll();
})();
</script>
)LOG";
