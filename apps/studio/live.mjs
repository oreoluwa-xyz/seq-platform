import {decodeChordMode} from './chord-mode.mjs?v=886590c057';
import {scaleNames} from './scale.mjs?v=bcc8228f15';
const notes = bytes => Array.from({length:128}, (_,n)=>n).filter(n=>bytes[n>>3] & (1<<(n&7)));
export function decodeLive(bytes) {
  if(bytes.length!==60 || bytes[0]!==1) throw Error('Incompatible live-state format.');
  const word=i=>bytes[i]|bytes[i+1]<<8, flags=bytes[1];
  if(bytes[2]>=12 || bytes[3]>2 || bytes[4]>1 || bytes[5]>15 || (bytes[6]!==255 && bytes[6]>=12) || !word(12) || word(12)>2048 || word(10)>=word(12)) throw Error('Invalid live-state reply.');
  return {composed:!!(flags&64),rhythm:flags&64?(flags&128?'euclid':'stored'):null,active:!!(flags&1),running:!!(flags&2),armed:!!(flags&4),external:!!(flags&8),transferring:!!(flags&16),child:!!(flags&32),track:bytes[2],type:bytes[3],engine:bytes[4],chord:bytes[6],channel:bytes[5],lanesUsed:bytes[7],bpm:word(8),step:word(10),length:word(12),bar:word(14),lanes:word(16),melodicMask:word(18),wrapperMask:word(20),moduleMask:word(22),sounding:notes(bytes.slice(24,40)),selected:notes(bytes.slice(40,56)),pattern:word(56),editStep:word(58)};
}
export const visualModes=['off','drum','melodic','euclid','chord','cc','navigation','drum-edit','melodic-edit','confirmation','scale','chord-step'];
export function decodeVisual(bytes){
  if(bytes.length!==352||bytes[0]!==2||bytes[60]>=visualModes.length||bytes[62]<1||bytes[62]>127||bytes[63]!==1)throw Error('Incompatible keyboard visual state.');
  const base=bytes.slice(0,60);base[0]=1;
  return {...decodeLive(base),visual:{mode:visualModes[bytes[60]],holdEdit:!!(bytes[61]&16),grid:!!(bytes[61]&1),os:bytes[61]&8?(bytes[61]&2?'win':'mac'):null,leds:bytes[61]&4?bytes.slice(64):null}};
}
export function decodeDetailed(bytes){
  if(bytes.length!==608||bytes[0]!==3)throw Error('Incompatible step-note state.');
  const visual=bytes.slice(0,352);visual[0]=2;
  return {...decodeVisual(visual),stepNotes:Array.from({length:16},(_,i)=>notes(bytes.slice(352+i*16,368+i*16)))};
}
export function decodeRhythm(bytes){
  if(bytes.length!==896||bytes[0]!==4)throw Error('Incompatible live rhythm state.');
  const detail=bytes.slice(0,608);detail[0]=3;
  const word=i=>bytes[i]|bytes[i+1]<<8,flags=bytes[608];
  const rhythm={editable:!!(bytes[609]&1),orderedKeys:!!(bytes[609]&2),laneSupported:!!(bytes[609]&4),lane:bytes[609]&4&&bytes[619]!==255?bytes[619]:null,euclidean:!!(flags&1),enabled:!!(flags&2),template:!!(flags&4),accumulator:!!(flags&8),steps:word(610),fills:word(612),rotation:word(614),probability:bytes[616],accent:bytes[617],ratchet:bytes[618],generated:word(620)};
  if(rhythm.lane!==null&&rhythm.lane>=12)throw Error('Invalid rhythm lane.');
  if(rhythm.euclidean&&(!rhythm.steps||rhythm.steps>2048||rhythm.fills>rhythm.steps||rhythm.rotation>=rhythm.steps||rhythm.probability>100))throw Error('Invalid live rhythm parameters.');
  let scale=null;
  if(bytes[622]===1){scale={enabled:!!bytes[623],root:bytes[624],kind:bytes[625],mode:bytes[626],localRoot:bytes[627],localKind:bytes[628],direction:bytes[629],editing:!!bytes[630],pending:{enabled:!!bytes[631],root:bytes[632],kind:bytes[633]},revision:word(634),count:bytes[636]||7};
    if(scale.count>scaleNames.length||scale.count<7||bytes[623]>1||scale.root>11||scale.kind>=scale.count||scale.mode>2||scale.localRoot>11||scale.localKind>=scale.count||scale.direction>2||bytes[630]>1||bytes[631]>1||scale.pending.root>11||scale.pending.kind>=scale.count)throw Error('Invalid scale state.');
  }
  return {...decodeDetailed(detail),chordScaleBoundSupported:!!(bytes[638]&64),chordCustomSupported:!!(bytes[638]&32),chordInversionSupported:!!(bytes[638]&16),globalTempoSupported:!!(bytes[638]&8),chordSupported:!!(bytes[638]&4),harmonySupported:!!(bytes[638]&1),harmonyRequest:bytes[638]&2?bytes[639]:null,scale,euclid:rhythm,projectedNotes:flags&16?Array.from({length:16},(_,i)=>notes(bytes.slice(640+i*16,656+i*16))):null};
}
export function decodePalette(bytes){
  if(bytes.length!==960||bytes[0]!==5)throw Error('Incompatible palette snapshot.');
  const rhythm=bytes.slice(0,896);rhythm[0]=4;
  const state=decodeRhythm(rhythm),flags=bytes[896],count=bytes[897],pin=bytes[898],pending=bytes[900];
  if(count>8||pending>8||(pin!==255&&pin>=count))throw Error('Invalid palette size or pin.');
  const signed=n=>n>127?n-256:n;
  const entries=(start,notes,count)=>Array.from({length:count},(_,i)=>({offset:signed(bytes[start+i]),note:bytes[notes+i]}));
  const slots=Array.from(bytes.slice(936,952));
  if(slots.some(n=>n!==255&&n>=Math.max(1,count)))throw Error('Invalid step palette slot.');
  const saved=entries(904,912,count),capture=entries(920,928,pending);
  if([...saved,...capture].some(e=>e.note>127))throw Error('Invalid palette pitch.');
  if(bytes[957]>pending)throw Error('Invalid retained palette size.');
  if(bytes[954]>1||(bytes[954]===1&&bytes[955]>1))throw Error('Invalid palette controls.');
  return {...state,temp:bytes[958]&16?{active:!!(bytes[958]&32),queued:!!(bytes[958]&64)}:null,palette:flags&1?{removeSupported:!!(bytes[958]&8),root:bytes[959],clearShortcut:!!(bytes[958]&4),replaceShortcut:!!(bytes[958]&1),starter:!!(bytes[958]&1)&&!!(bytes[958]&2),captureEditable:!!(bytes[956]&1),replace:bytes[956]&1?!!(bytes[956]&2):true,full:!!(bytes[956]&4),retained:bytes[957],editable:bytes[954]===1,mode:bytes[954]===1&&!bytes[955]?'palette':'transpose',overrides:bytes[952]|bytes[953]<<8,held:!!(flags&2),shift:!!(flags&4),capturing:!!(flags&8),latched:!!(flags&16),pin:pin===255?null:pin,saved,capture,slots}:null,paletteSupported:true};
}
export function decodeCC(bytes){
  if(bytes[160]>1||(bytes[160]===1&&bytes[161]>1))throw Error('Invalid CC automation mode.');
  if(bytes.length!==192||bytes[0]!==1||bytes[1]&~31||bytes[2]>=12||bytes[4]>16||!bytes[62]||bytes[62]>127||bytes[63]!==1||(bytes[3]!==255&&bytes[3]>=bytes[4]))throw Error('Invalid CC snapshot.');
  const flags=bytes[1],slots=Array.from({length:bytes[4]},(_,i)=>{const n=64+i*6;if(bytes[n+2]>127||bytes[n+3]>15||bytes[n+4]>127||bytes[n+5]>1)throw Error('Invalid CC slot.');return {keycode:bytes[n]|bytes[n+1]<<8,controller:bytes[n+2],channel:bytes[n+3],value:bytes[n+4],held:!!bytes[n+5]};});
  return {active:!!(flags&1),latched:!!(flags&2),temporary:!!(flags&4),running:!!(flags&8),trackOverride:!!(flags&16),automationSupported:bytes[160]===1,automationShiftPgDn:bytes[160]===1&&!!(bytes[162]&1),manual:bytes[160]===1&&bytes[161]===1,track:bytes[2],selected:bytes[3]===255?null:bytes[3],slots};
}
export class LiveMonitor {
  constructor(connection,{onState,onStatus,canPoll=()=>true,wantVisual=()=>false}) {
    this.connection=connection;this.onState=onState;this.onStatus=onStatus;this.canPoll=canPoll;this.wantVisual=wantVisual;this.visualSupported=null;this.detailSupported=null;this.rhythmSupported=null;this.paletteSupported=null;this.stopped=true;this.wanted=null;this.flight=null;
  }
  start(){this.stopped=false;this.tick();}
  stop(){this.stopped=true;clearTimeout(this.timer);this.wanted=null;}
  async pause(){this.paused=true;await this.flight;}
  resume(){this.paused=false;}
  select(track){if(!Number.isInteger(track)||track<0||track>=12)throw Error('Invalid track.');this.wanted=track;}
  async readState(track){
    let selected=null;
    if(track!==null)selected=decodeLive(await this.connection.request(12,track));
    if(this.paletteSupported!==false&&!this.stopped&&!this.paused){
      let first;
      try{first=await this.connection.request(17);}catch(error){if(error.status!==1)throw error;this.paletteSupported=false;}
      if(first){
        if(first.length!==64||first[0]!==5||first[63]!==1||!first[62]||first[62]>127)throw Error('Invalid palette header.');
        const bytes=new Uint8Array(960);bytes.set(first);
        for(let offset=64;offset<bytes.length;offset+=64){
          if(this.stopped||this.paused||!this.canPoll())return null;
          const chunk=await this.connection.request(17,first[62],offset);
          if(chunk.length!==64)throw Error('Incomplete palette snapshot.');
          bytes.set(chunk,offset);
        }
        if(this.stopped||this.paused||!this.canPoll())return null;
        this.paletteSupported=true;
        const state=decodePalette(bytes);
        if(state.chordSupported){
          const head=await this.connection.request(22),total=head[0]>=3?448:192,chordBytes=new Uint8Array(total);
          if(head.length!==64||![1,2,3,4].includes(head[0])||!head[62]||head[63]!==1)throw Error('Invalid chord header.');
          chordBytes.set(head);
          // A normal drum/melody track only needs the mode header and track mask.
          // Fetching unused chord pages made bar changes more likely to invalidate a complete live view.
          if(head[2])for(let offset=64;offset<total;offset+=64){if(this.stopped||this.paused||!this.canPoll())return null;const chunk=await this.connection.request(22,head[62],offset);if(chunk.length!==64)throw Error('Incomplete chord snapshot.');chordBytes.set(chunk,offset)}
          if(this.stopped||this.paused||!this.canPoll())return null;
          state.chordMode=decodeChordMode(chordBytes);
          if(state.chordMode.track!==state.track)return null;
          // Separate snapshots can straddle a bar boundary. Retry promptly, without marking USB lost.
          if(state.chordMode.enabled&&state.chordMode.bar!==state.bar)return null;
        }
        if(bytes[637]===1&&state.visual.mode==='cc'){
          const head=await this.connection.request(21),ccBytes=new Uint8Array(192);
          if(head.length!==64||head[0]!==1||!head[62]||head[62]>127||head[63]!==1)throw Error('Invalid CC header.');
          ccBytes.set(head);
          for(let offset=64;offset<192;offset+=64){
            if(this.stopped||this.paused||!this.canPoll())return null;
            const chunk=await this.connection.request(21,head[62],offset);
            if(chunk.length!==64)throw Error('Incomplete CC snapshot.');
            ccBytes.set(chunk,offset);
          }
          if(this.stopped||this.paused||!this.canPoll())return null;
          state.cc=decodeCC(ccBytes);
        }
        return state;
      }
    }
    if(this.rhythmSupported!==false&&!this.stopped&&!this.paused){
      let first;
      try{first=await this.connection.request(16);}catch(error){if(error.status!==1)throw error;this.rhythmSupported=false;}
      if(first){
        if(first.length!==64||first[0]!==4||first[63]!==1||!first[62]||first[62]>127)throw Error('Invalid rhythm header.');
        const bytes=new Uint8Array(896);bytes.set(first);
        for(let offset=64;offset<bytes.length;offset+=64){
          if(this.stopped||this.paused||!this.canPoll())return null;
          const chunk=await this.connection.request(16,first[62],offset);
          if(chunk.length!==64)throw Error('Incomplete rhythm snapshot.');
          bytes.set(chunk,offset);
        }
        if(this.stopped||this.paused||!this.canPoll())return null;
        this.rhythmSupported=true;return decodeRhythm(bytes);
      }
    }
    if(this.detailSupported!==false&&!this.stopped&&!this.paused){
      let first;
      try{first=await this.connection.request(14);}catch(error){
        if(error.status!==1)throw error;
        this.detailSupported=false;
      }
      if(first){
        if(first.length!==64||first[0]!==3||first[63]!==1||!first[62]||first[62]>127)throw Error('Invalid step-note header.');
        const bytes=new Uint8Array(608);bytes.set(first);
        for(let offset=64;offset<bytes.length;offset+=64){
          if(this.stopped||this.paused||!this.canPoll())return null;
          const chunk=await this.connection.request(14,first[62],offset);
          if(chunk.length!==Math.min(64,bytes.length-offset))throw Error('Incomplete step-note snapshot.');
          bytes.set(chunk,offset);
        }
        if(this.stopped||this.paused||!this.canPoll())return null;
        this.detailSupported=true;return decodeDetailed(bytes);
      }
    }
    if(this.wantVisual()&&this.visualSupported!==false&&!this.stopped&&!this.paused){
      let first;
      try{first=await this.connection.request(13);}catch(error){
        if(error.status!==1)throw error;
        this.visualSupported=false;
      }
      if(first){
        if(first.length!==64||first[0]!==2||first[63]!==1||!first[62]||first[62]>127)throw Error('Invalid visual header.');
        const bytes=new Uint8Array(352);bytes.set(first);
        for(let offset=64;offset<bytes.length;offset+=64){
          if(this.stopped||this.paused||!this.canPoll())return null;
          const chunk=await this.connection.request(13,first[62],offset);
          if(chunk.length!==Math.min(64,bytes.length-offset))throw Error('Incomplete visual snapshot.');
          bytes.set(chunk,offset);
        }
        if(this.stopped||this.paused||!this.canPoll())return null;
        this.visualSupported=true;return decodeVisual(bytes);
      }
    }
    if(this.stopped||this.paused)return null;
    return selected||decodeLive(await this.connection.request(11));
  }
  async tick(){
    if(this.stopped)return;
    let delay=100;
    if(!this.paused && this.canPoll() && !this.connection.pending) {
      const track=this.wanted;this.wanted=null;
      this.flight=(async()=>{
        try {
          const state=await this.readState(track);
          if(state&&!this.stopped){this.onStatus('live');this.onState(state);}
        }catch(error){
          if(this.stopped)return;
          if(error.status===1){this.onStatus('unsupported');this.stop();}
          else if(error.status===2 && track!==null){this.onStatus('selection-blocked');}
          else {this.onStatus('stale');delay=1000;}
        }
      })();
      await this.flight;this.flight=null;
    }
    if(!this.stopped)this.timer=setTimeout(()=>this.tick(),delay);
  }
}
