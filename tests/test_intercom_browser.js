'use strict';
const assert=require('node:assert/strict'),fs=require('node:fs'),vm=require('node:vm');
let Processor;
class Base {constructor(){this.port={onmessage:null,postMessage:()=>{}};}}
vm.runInNewContext(fs.readFileSync('web/js/intercom-worklet.js','utf8'),{
  AudioWorkletProcessor:Base,sampleRate:48000,Int16Array,Float32Array,Math,
  registerProcessor:(name,type)=>{assert.equal(name,'libreecho-intercom');Processor=type;}
});
const p=new Processor();let packets=0;
p.port.postMessage=data=>{if(data.type==='pcm'){assert.equal(data.buffer.byteLength,640);packets++;p.port.onmessage({data:{type:'credit'}});}};
p.port.onmessage({data:{type:'arm'}});
for(let i=0;i<375;i++)p.process([[new Float32Array(128).fill(.25)]],[[new Float32Array(128)]]);
assert.equal(packets,50); // 48k native audio -> exactly 16k / 320 20ms frames.
const blocked=new Processor();let posted=0;
blocked.port.postMessage=()=>posted++;blocked.port.onmessage({data:{type:'arm'}});
for(let i=0;i<100;i++)blocked.process([[new Float32Array(128)]],[[new Float32Array(128)]]);
assert.equal(posted,3);assert.equal(blocked.armed,false); // Two credits then one terminal overrun.
const receive=new Processor();
for(let i=0;i<7;i++)receive.port.onmessage({data:{type:'pcm',buffer:new Int16Array(320).buffer}});
assert.equal(receive.overrun,true);assert.ok(receive.count<=1920);

(async()=>{
  const elements=new Map(),requests=[],listeners={};let grantResolve,pageResolve,captures=0,stopped=0,closed=0;
  for(const id of ['intercom-status','intercom-start','intercom-end','intercom-mute'])elements.set(id,{});
  elements.set('intercom-permission',{checked:true});
  class Context {constructor(){this.state='running';this.audioWorklet={addModule:async()=>{}};this.destination={};}
    async resume(){}async close(){closed++;this.state='closed';}createMediaStreamSource(){return {connect(){},disconnect(){}};}}
  class Node {constructor(){this.port={postMessage(){}};}connect(){}disconnect(){}}
  const track={stop(){stopped++;},enabled:true};
  const sandbox={console,AudioContext:Context,AudioWorkletNode:Node,WebSocket:class{},state:{page:'Intercom',renderGeneration:1},content:{innerHTML:'unchanged'},
    window:{isSecureContext:true,AudioWorkletNode:Node,addEventListener:(name,fn)=>{listeners[name]=fn;}},
    document:{getElementById:id=>elements.get(id),addEventListener:(name,fn)=>{listeners[name]=fn;},hidden:false},
    location:{protocol:'https:',host:'speaker.test'},navigator:{mediaDevices:{getUserMedia:async()=>{captures++;return {getTracks:()=>[track],getAudioTracks:()=>[track]};}}},
    api:(path,opts={})=>{requests.push([path,opts]);if(!opts.method)return new Promise(resolve=>{pageResolve=resolve;});if(opts.method==='POST')return new Promise(resolve=>{grantResolve=resolve;});return Promise.resolve({});},
    setInterval,clearInterval,setTimeout,clearTimeout,Date,JSON,ArrayBuffer,DataView,Uint8Array};
  vm.createContext(sandbox);vm.runInContext(fs.readFileSync('web/js/intercom-ui.js','utf8'),sandbox);
  assert.equal(captures,0); // Loading a page cannot activate the microphone.
  sandbox.window.isSecureContext=false;await sandbox.startIntercom();assert.equal(captures,0);assert.match(elements.get('intercom-status').textContent,/trusted HTTPS/);sandbox.window.isSecureContext=true;
  const starting=sandbox.startIntercom();
  for(let i=0;i<10&&!grantResolve;i++)await new Promise(resolve=>setImmediate(resolve));
  assert.equal(captures,1);assert.ok(grantResolve);
  sandbox.stopIntercom();grantResolve({ticket:'old-ticket',id:'old-call',path:'/api/v1/intercom/stream'});
  await starting;
  const deletion=requests.find(([,opts])=>opts.method==='DELETE');
  assert.equal(JSON.parse(deletion[1].body).id,'old-call');assert.equal(stopped,1);assert.equal(closed,1);
  listeners.pagehide(); // Repeated departure is idempotent.
  let resumeResolve;
  sandbox.AudioContext=class extends Context {async resume(){await new Promise(resolve=>{resumeResolve=resolve;});}};
  const resuming=sandbox.startIntercom();assert.ok(resumeResolve);
  sandbox.document.hidden=true;listeners.visibilitychange();resumeResolve();await resuming;
  assert.equal(captures,1);assert.equal(closed,2); // Departure during resume cannot request capture later.
  sandbox.document.hidden=false;sandbox.AudioContext=Context;const previousGrant=grantResolve;
  const active=sandbox.startIntercom();
  for(let i=0;i<10&&grantResolve===previousGrant;i++)await new Promise(resolve=>setImmediate(resolve));
  assert.equal(captures,2);
  const rendering=sandbox.intercomPage();assert.equal(stopped,2);assert.equal(closed,3);
  sandbox.state.page='Audio';sandbox.state.renderGeneration++;
  pageResolve({enabled:true,busy:false});await rendering;assert.equal(sandbox.content.innerHTML,'unchanged');
  grantResolve({ticket:'late',id:'late',path:'/api/v1/intercom/stream'});await active;
  elements.get('intercom-permission').checked=false;elements.get('intercom-start').disabled=true;
  listeners.pagehide();assert.equal(elements.get('intercom-start').disabled,true);
  console.log('intercom browser synthetic rate conversion, bounded queues, explicit capture and scoped late cleanup: PASS');
})().catch(error=>{console.error(error);process.exitCode=1;});
