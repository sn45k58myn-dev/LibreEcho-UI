'use strict';
/* 16 kHz mono PCM, 20 ms packets. No recording, growing queues or network. */
const PACKET_SAMPLES = 320;
const CAPTURE_CREDITS = 2;
const PLAYBACK_LIMIT = 6 * PACKET_SAMPLES;
const STALL_SECONDS = 0.4;
const CAPTURE_STALL_PACKETS = 20; // 400 ms at 20 ms per packet.

class LibreEchoIntercom extends AudioWorkletProcessor {
  constructor() {
    super();
    this.armed=false; this.credit=0; this.capture=new Int16Array(PACKET_SAMPLES); this.used=0;
    this.phase=0; this.sum=0; this.samples=0;
    this.ring=new Float32Array(4096); this.read=0; this.count=0; this.playPhase=0;
    this.overrun=false;
    this.captureLoss=0;
    this.renderedSamples=0;
    this.playbackOverflowAt=null;
    this.port.onmessage=({data})=>{
      if(this.overrun)return;
      if(data.type==='arm'){this.armed=true;this.credit=CAPTURE_CREDITS;}
      else if(data.type==='credit')this.credit=Math.min(CAPTURE_CREDITS,this.credit+1);
      else if(data.type==='pcm'){
        const pcm=new Int16Array(data.buffer);
        if(pcm.length!==PACKET_SAMPLES){this.fail('invalid-playback');return;}
        const overflow=this.count+pcm.length-PLAYBACK_LIMIT;
        if(overflow>0){
          if(this.playbackOverflowAt===null)this.playbackOverflowAt=this.renderedSamples;
          if(this.renderedSamples-this.playbackOverflowAt>=STALL_SECONDS*sampleRate){
            this.fail('playback-stalled');
            return;
          }
          // Evict stale samples, keeping the current interpolation phase.
          this.read=(this.read+overflow)&4095;
          this.count-=overflow;
        }else this.playbackOverflowAt=null;
        for(let i=0;i<pcm.length;i++)this.ring[(this.read+this.count++)&4095]=pcm[i]/32768;
      }
    };
  }
  fail(reason){
    if(this.overrun)return;
    this.overrun=true;
    this.armed=false;
    this.count=0;
    this.port.postMessage({type:'overrun',reason});
  }
  process(inputs,outputs){
    const output=outputs[0][0],input=inputs[0]&&inputs[0][0];
    this.renderedSamples+=output.length;
    for(let i=0;i<output.length;i++){
      output[i]=0;
      if(this.count>1){
        const a=this.ring[this.read],b=this.ring[(this.read+1)&4095];
        output[i]=a+(b-a)*this.playPhase;
        this.playPhase+=16000/sampleRate;
        const step=Math.floor(this.playPhase);
        if(step){this.read=(this.read+step)&4095;this.count-=step;this.playPhase-=step;}
      }
      if(!this.armed)continue;
      this.sum+=input?input[i]:0;this.samples++;this.phase+=16000;
      if(this.phase>=sampleRate){
        this.phase-=sampleRate;
        const v=Math.max(-1,Math.min(1,this.sum/this.samples));this.sum=0;this.samples=0;
        this.capture[this.used++]=v<0?v*32768:v*32767;
        if(this.used===PACKET_SAMPLES){
          this.used=0;
          if(!this.credit){
            // This packet was never sent, so discard it without creating a
            // gap in the stream's network sequence or accumulating a queue.
            if(++this.captureLoss>=CAPTURE_STALL_PACKETS)this.fail('capture-stalled');
            continue;
          }
          this.captureLoss=0;
          this.credit--;this.port.postMessage({type:'pcm',buffer:this.capture.buffer},[this.capture.buffer]);
          this.capture=new Int16Array(PACKET_SAMPLES);
        }
      }
    }
    return true;
  }
}
registerProcessor('libreecho-intercom',LibreEchoIntercom);
