const fs=require('fs'),vm=require('vm'),assert=require('assert/strict');
const source=fs.readFileSync('source/ESP32-S3/V1.7/debug_log_panel.h','utf8');
const js=source.match(/<script>([\s\S]*?)<\/script>/)[1];
const elements={},events={},timers=[],responses=[],requests=[];
function element(id){return elements[id]??=(function(){const classes=new Set();return {textContent:'',hidden:true,checked:true,scrollHeight:42,scrollTop:0,attrs:{},classList:{contains:c=>classes.has(c),toggle(c,on){if(on)classes.add(c);else classes.delete(c);}},setAttribute(k,v){this.attrs[k]=v;},click(){this.clicked=true;}};})();}
let downloaded;
const document={hidden:false,getElementById:element,addEventListener:(name,fn)=>events[name]=fn,createElement:()=>downloaded=element('download')};
const context={document,location:{hostname:'reader-test'},Blob,URL:{createObjectURL:()=> 'blob:test',revokeObjectURL:()=>{}},Date,setTimeout:fn=>timers.push(fn),fetch:async url=>{requests.push(url);const data=responses.shift();if(data instanceof Error)throw data;assert(data,'unexpected fetch');return {ok:true,json:async()=>data};}};
async function settle(){for(let i=0;i<8;i++)await Promise.resolve();}
async function tick(data){if(data)responses.push(data);assert(timers.length);timers.shift()();await settle();}
(async()=>{
 responses.push({enabled:false});vm.runInNewContext(js,context);await settle();assert(element('debugPanel').hidden);
 await tick({enabled:true,boot:1,next:10,lost:false,text:'<script>not HTML</script>\n'});
 assert(!element('debugPanel').hidden);assert.equal(element('logText').textContent,'<script>not HTML</script>\n');
 element('logExpand').onclick();assert(element('debugPanel').classList.contains('expanded'));assert.equal(element('logExpand').attrs['aria-expanded'],'true');events.keydown({key:'Escape'});assert(!element('debugPanel').classList.contains('expanded'));
 element('logPause').onclick();const n=requests.length;await tick();assert.equal(requests.length,n);element('logPause').onclick();
 await tick({enabled:true,boot:1,next:20,lost:true,text:'new\n'});assert(element('logText').textContent.includes('überschrieben'));assert(requests.at(-1).includes('cursor=10&boot=1'));
 await tick(new Error('offline'));assert(element('logStatus').textContent.includes('unterbrochen'));
 await tick({enabled:true,boot:2,next:3,lost:false,text:'boot\n'});assert(element('logText').textContent.includes('neu gestartet'));
 element('logClear').onclick();assert.equal(element('logText').textContent,'');
 await tick({enabled:true,boot:2,next:70000,lost:false,text:'x'.repeat(70000)});assert.equal(element('logText').textContent.length,65536);
 element('logDownload').onclick();assert(downloaded.clicked);assert.equal(downloaded.download,'argus-reader-test-debug.log');
 console.log('PASS: disabled/enabled, literal log text, expand/Escape, pause/resume, cursor, overflow, reconnect/reboot, clear, bounded view and download');
})().catch(e=>{console.error(e);process.exitCode=1;});
