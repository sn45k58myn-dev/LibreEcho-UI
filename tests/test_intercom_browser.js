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
function render(processor,quanta,value=0){
  let output;
  for(let i=0;i<quanta;i++){
    output=new Float32Array(128);
    processor.process([[new Float32Array(128).fill(value)]],[[output]]);
    assert.ok(processor.count>=0&&processor.count<=1920);assert.equal(processor.ring.length,4096);
    assert.ok(processor.used>=0&&processor.used<=(processor.overrun?320:319));assert.ok(processor.credit>=0&&processor.credit<=2);
  }
  return output;
}
function receivePacket(processor,number){
  const pcm=Int16Array.from({length:320},(_,i)=>1000+number*320+i);
  processor.port.onmessage({data:{type:'pcm',buffer:pcm.buffer}});
  assert.ok(processor.count>=0&&processor.count<=1920);assert.equal(processor.ring.length,4096);
}
// A foreground main-thread pause leaves two packets in flight. Audio continues
// in the worklet: discard unsent old capture rather than record a growing queue.
const delayed=new Processor(),captured=[];let outstanding=0,maximumOutstanding=0,acknowledge=false,delayErrors=0;
delayed.port.postMessage=data=>{
  if(data.type==='overrun'){delayErrors++;return;}
  if(data.type!=='pcm')return;
  captured.push(new Int16Array(data.buffer)[0]);outstanding++;maximumOutstanding=Math.max(maximumOutstanding,outstanding);
  if(acknowledge){outstanding--;delayed.port.onmessage({data:{type:'credit'}});}
};
delayed.port.onmessage({data:{type:'arm'}});render(delayed,38,.25); // 101 ms without main-thread credit delivery.
assert.equal(captured.length,2);assert.equal(delayErrors,0);assert.equal(delayed.armed,true);assert.equal(outstanding,2);
acknowledge=true;
while(outstanding){outstanding--;delayed.port.onmessage({data:{type:'credit'}});}
assert.equal(captured.length,2); // A credit cannot flush any saved capture backlog.
render(delayed,83,.75);
assert.equal(delayErrors,0);assert.equal(maximumOutstanding,2);assert.ok(captured.length>=12);
assert.ok(captured.slice(3).every(value=>value===24575)); // Recovery delivers current audio, not the discarded gap.
for(let i=0;i<5;i++){
  acknowledge=false;render(delayed,38);assert.equal(delayErrors,0);
  acknowledge=true;while(outstanding){outstanding--;delayed.port.onmessage({data:{type:'credit'}});}render(delayed,8);
}
assert.equal(delayErrors,0);assert.equal(maximumOutstanding,2); // Separate brief gaps are not cumulative overload.
const blocked=new Processor();let blockedPackets=0,blockedErrors=0,blockedReason;
blocked.port.postMessage=data=>{if(data.type==='pcm')blockedPackets++;if(data.type==='overrun'){blockedErrors++;blockedReason=data.reason;}};
blocked.port.onmessage({data:{type:'arm'}});render(blocked,750); // Two seconds is sustained, rather than a scheduling hiccup.
assert.equal(blockedPackets,2);assert.equal(blockedErrors,1);assert.equal(blocked.armed,false);
assert.equal(blockedReason,'capture-stalled');
blocked.port.onmessage({data:{type:'credit'}});blocked.port.onmessage({data:{type:'arm'}});render(blocked,38);
assert.equal(blockedPackets,2);assert.equal(blockedErrors,1); // Terminal overload cannot restart capture.

// Eight packets can arrive together after a 160 ms scheduling pause. Retain
// the latest six, including correct ring position and interpolation phase.
const receive=new Processor();let receiveErrors=0;
receive.port.postMessage=data=>{if(data.type==='overrun')receiveErrors++;};
for(let i=0;i<8;i++)receivePacket(receive,i);
assert.equal(receiveErrors,0);assert.equal(receive.count,1920);
assert.equal(receive.ring[receive.read],1640/32768);
render(receive,1);const phase=receive.playPhase;
assert.ok(phase>0&&phase<1);
receivePacket(receive,8);receivePacket(receive,9);
assert.equal(receive.count,1920);assert.equal(receive.playPhase,phase);
const expected=(2280+phase)/32768;
assert.ok(Math.abs(render(receive,1)[0]-expected)<1e-7);assert.equal(receiveErrors,0);
render(receive,100); // Normal playback drains the bounded burst and recovers.
receivePacket(receive,10);assert.equal(receiveErrors,0);
const flooded=new Processor();let floodErrors=0,floodReason;
flooded.port.postMessage=data=>{if(data.type==='overrun'){floodErrors++;floodReason=data.reason;}};
for(let i=0;i<200;i++){receivePacket(flooded,i%30);render(flooded,1);}
assert.equal(floodErrors,1);assert.equal(flooded.armed,false);
assert.equal(floodReason,'playback-stalled');assert.ok(render(flooded,1).every(value=>value===0));
const failedCount=flooded.count;
receivePacket(flooded,0);assert.equal(flooded.count,failedCount);assert.equal(floodErrors,1);
const malformed=new Processor();let malformedErrors=0,malformedReason;
malformed.port.postMessage=data=>{if(data.type==='overrun'){malformedErrors++;malformedReason=data.reason;}};
malformed.port.onmessage({data:{type:'pcm',buffer:new Int16Array(319).buffer}});
assert.equal(malformedErrors,1);assert.equal(malformed.count,0);
assert.equal(malformedReason,'invalid-playback');

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
