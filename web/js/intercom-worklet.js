'use strict';
/* 16 kHz mono PCM, 20 ms packets. No recording, growing queues or network. */
class LibreEchoIntercom extends AudioWorkletProcessor {
  constructor() {
    super();
    this.armed=false; this.credit=0; this.capture=new Int16Array(320); this.used=0;
    this.phase=0; this.sum=0; this.samples=0;
    this.ring=new Float32Array(4096); this.read=0; this.count=0; this.playPhase=0;
    this.overrun=false;
    this.port.onmessage=({data})=>{
      if(data.type==='arm'){this.armed=true;this.credit=2;}
      else if(data.type==='credit')this.credit=Math.min(2,this.credit+1);
      else if(data.type==='pcm'){
        const pcm=new Int16Array(data.buffer);
        if(pcm.length!==320||this.count+pcm.length>1920){this.fail();return;}
        for(let i=0;i<pcm.length;i++)this.ring[(this.read+this.count++)&4095]=pcm[i]/32768;
      }
    };
  }
  fail(){if(!this.overrun){this.overrun=true;this.armed=false;this.port.postMessage({type:'overrun'});}}
  process(inputs,outputs){
    const output=outputs[0][0],input=inputs[0]&&inputs[0][0];
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
        if(this.used===320){
          if(!this.credit){this.fail();continue;}
          this.credit--;this.port.postMessage({type:'pcm',buffer:this.capture.buffer},[this.capture.buffer]);
          this.capture=new Int16Array(320);this.used=0;
        }
      }
    }
    return true;
  }
}
registerProcessor('libreecho-intercom',LibreEchoIntercom);
