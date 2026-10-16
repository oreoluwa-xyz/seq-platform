import {checksum} from './module.mjs?v=8c4bcd2da4';
const PREFIX=[0xF0,0x7D,83,69,81,1];
export class SequencerConnection {
  constructor(input,output) { this.input=input; this.output=output; this.pending=null; this.sysex=[]; this.receivedBytes=0; this.listener=e=>this.receive(e.data); input.addEventListener('midimessage',this.listener); }
  async close() {
    this.input.removeEventListener('midimessage',this.listener);this.sysex=[];
    if(this.pending) this.pending.fail(Error('MIDI connection closed.'));
    await Promise.allSettled([this.input.close?.(),this.output.close?.()]);
  }
  receive(data) {
    if(this.pending) this.receivedBytes+=data.length;
    // CoreMIDI may deliver a SysEx reply in separate USB-sized fragments.
    // Keep framing across events and ignore interleaved realtime messages.
    for (const byte of data) {
      if (byte >= 0xF8) continue;
      if (byte === 0xF0) { this.sysex=[byte]; continue; }
      if (!this.sysex.length) continue;
      if (byte === 0xF7) {
        const packet=Uint8Array.from([...this.sysex,byte]);this.sysex=[];
        this.receivePacket(packet);
      } else if (byte >= 0x80 || this.sysex.length >= 256) this.sysex=[];
      else this.sysex.push(byte);
    }
  }
  receivePacket(data) {
    const p=this.pending;
    if(!p||data.length<12||!PREFIX.every((v,i)=>data[i]===v)||data.at(-1)!==0xF7||data[6]!== (p.command|64)||data[7]!==p.object||data[8]!== (p.offset&127)||data[9]!== (p.offset>>7)) return;
    if(data[10]) {const error=Error(`Keyboard rejected the operation (status ${data[10]}). No successful install was confirmed.`);error.status=data[10];p.fail(error);return;}
    const bytes=data.slice(11,-1);
    if(bytes.length%2||[...bytes].some(b=>b>15)) {p.fail(Error('Invalid MIDI response.'));return;}
    p.done(Uint8Array.from({length:bytes.length/2},(_,i)=>bytes[2*i]|bytes[2*i+1]<<4));
  }
  request(command,object=0,offset=0,payload=new Uint8Array()) {
    if(this.pending) return Promise.reject(Error('Another MIDI operation is running.'));
    return new Promise((resolve,reject)=>{
      const finish=(fn,value)=>{clearTimeout(timer);this.pending=null;fn(value);};
      const timer=setTimeout(()=>finish(reject,Error(this.receivedBytes ? `Received ${this.receivedBytes} MIDI bytes, but no matching SEQ reply. Check that both ports belong to the keyboard.` : 'The browser sent the request but received no MIDI data. Check both keyboard ports. Chrome on macOS may be affected by a SysEx bug; see Connection help below.')),4000);
      this.sysex=[];this.receivedBytes=0;
      this.pending={command,object,offset,done:v=>finish(resolve,v),fail:e=>finish(reject,e)};
      try { this.output.send([...PREFIX,command,object,offset&127,offset>>7,...[...payload].flatMap(b=>[b&15,b>>4]),0xF7]); }
      catch(error){this.pending.fail(error);}
    });
  }
  async capabilities(){const c=await this.request(10);if(c.length!==21||c[0]!==1||c[1]!==12||![0,12].includes(c[2])||c[3]!==8||![1,3].includes(c[6])) throw Error('Firmware module capabilities are incompatible.');try{c.components=await this.request(15);}catch(error){if(error.status!==1)throw error;c.components=null;}return c;}
  async read(object) {
    let expected=null, bytes=[];
    do {
      const reply=await this.request(1,object,bytes.length);
      const size=reply[0]|reply[1]<<8;
      if(reply.length<3||size<1||size>6144||(expected!==null&&expected!==size)||bytes.length+reply.length-2>size) throw Error('Inconsistent snapshot.');
      expected=size;bytes.push(...reply.slice(2));
    } while(bytes.length<expected);
    return Uint8Array.from(bytes);
  }
  async upload(object,bytes,validate=false) {
    await this.request(2,object,bytes.length);
    for(let offset=0;offset<bytes.length;offset+=32) await this.request(3,object,offset,bytes.slice(offset,offset+32));
    return this.request(validate?5:4,object,0,checksum(bytes));
  }
  async finish(){await this.request(7);}
}
export const sameBytes=(a,b)=>a.length===b.length&&a.every((v,i)=>v===b[i]);
export async function projectFile(objects) {
  return {format:'seq_test_project',version:1,objects:await Promise.all(objects.map(async data=>({data:[...data].map(v=>v.toString(16).padStart(2,'0')).join(''),sha256:[...new Uint8Array(await crypto.subtle.digest('SHA-256',data))].map(v=>v.toString(16).padStart(2,'0')).join('')})))};
}
