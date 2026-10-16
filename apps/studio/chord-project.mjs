import {scaleNotes,scaleNames} from './scale.mjs?v=bcc8228f15';
import {decodeModule,encodeModule,defaultModule,composeModule} from './module.mjs?v=8c4bcd2da4';
import {chordLabel,scaleChordTypes,scaleChordNotes} from './chord-mode.mjs?v=e16e3e7465';
const intervals=[[0,4,7],[0,3,7],[0,3,6],[0,4,8],[0,2,7],[0,5,7],[0,4,7,10],[0,4,7,11],[0,3,7,10],[0,3,6,10]];
const integer=(n,min,max)=>Number.isInteger(n)&&n>=min&&n<=max;
export function recipeNotes(c){
  if(!integer(c.root,0,127)||!integer(c.type,0,16)||!integer(c.scaleRoot,0,11)||!integer(c.scaleKind,0,scaleNames.length-1)||!integer(c.velocity,1,127)||!integer(c.length,1,2048))throw Error('Invalid chord recipe.');
  let notes;
  if(c.scaleFree!==undefined&&![false,true,0,1].includes(c.scaleFree))throw Error('Invalid chord scale policy.');
  if(c.quantize&&(!integer(c.quantizeRoot??c.scaleRoot,0,11)||!integer(c.quantizeKind??c.scaleKind,0,scaleNames.length-1)))throw Error('Invalid quantizer scale.');
  if(c.quantize!==undefined&&![false,true,0,1].includes(c.quantize))throw Error('Invalid chord quantization.');
  if(c.type===12){
    if(!Array.isArray(c.intervals)||c.intervals.length<2||c.intervals.length>8||c.intervals.some((n,i)=>!integer(n,-127,127)||(i&&n<=c.intervals[i-1])))throw Error('Use 2–8 unique ascending intervals, between −127 and 127.');
    if(typeof c.name!=='string'||!/^[ -~]{1,12}$/.test(c.name)||!c.name.trim())throw Error('Use a chord name of 1–12 plain characters.');
    notes=c.intervals.map(n=>c.root+n);
  }
  else if(c.type===16)notes=[0,3,6,9].map(n=>c.root+n);
  else if(c.type<10)notes=intervals[c.type].map(n=>c.root+n);
  else{if(!scaleNotes(c.scaleRoot,c.scaleKind).includes(c.root%12))throw Error('The root must belong to the chord’s chosen scale.');notes=scaleChordNotes(c);if(notes.some(n=>n===undefined))throw Error('Chord exceeds the MIDI note range.');}
  const inversion=c.inversion??0;
  if(!integer(inversion,0,notes.length-1))throw Error('Invalid chord inversion.');
  if(notes.some(n=>n<0||n>127))throw Error('Chord exceeds the MIDI note range.');
  for(let i=0;i<inversion;i++){let raised=notes.shift()+12;while(notes.includes(raised))raised+=12;notes.push(raised);notes.sort((a,b)=>a-b)}
  if(notes.some(n=>n>127)||new Set(notes).size!==notes.length)throw Error('Chord voicing exceeds the MIDI note range or repeats a pitch.');
  return c.quantize?[...new Set(notes.map(n=>quantizeNote(n,c.quantizeRoot??c.scaleRoot,c.quantizeKind??c.scaleKind)))]:notes;
}
export function quantizeNote(note,root,kind){
  const allowed=scaleNotes(root,kind);
  for(let d=0;d<128;d++){if(note-d>=0&&allowed.includes((note-d)%12))return note-d;if(note+d<128&&allowed.includes((note+d)%12))return note+d}
  throw Error('No playable scale note.');
}
export function customRecipe(notes,root,name='Custom',extra={}){
  if(!integer(root,0,127))throw Error('Choose the chord root explicitly.');
  const c={start:0,length:4,velocity:96,scaleRoot:0,scaleKind:0,inversion:0,quantize:false,...extra,type:12,root,name,intervals:[...notes].sort((a,b)=>a-b).map(n=>n-root)};recipeNotes(c);return c;
}
export function setChordScaleFree(c,free,scale){
  if(free){const notes=recipeNotes(c);return customRecipe(notes,c.root,'Free',{...c,inversion:0,quantize:false,scaleFree:true});}
  if(!scale?.enabled)throw Error('Enable a key and scale before binding this chord.');
  const next={...c,scaleFree:false,quantize:false,root:quantizeNote(c.root,scale.root,scale.kind),type:recipeNotes(c).length>=4?11:10,scaleRoot:scale.root,scaleKind:scale.kind};
  next.inversion=Math.min(c.inversion??0,next.type===10?2:3);recipeNotes(next);return next;
}
export function quantizeRecipesToScale(recipes,length,scale){
  if(!scale?.enabled||!integer(scale.root,0,11)||!integer(scale.kind,0,scaleNames.length-1))throw Error('Enable a global key and scale first with Ctrl + Space.');
  const next=recipes.map(c=>({...c,quantize:true,quantizeRoot:scale.root,quantizeKind:scale.kind}));recipeEvents(next,length);return next;
}
export function recipeEvents(recipes,length){
  if(!integer(length,1,2048)||recipes.length>64)throw Error('Use at most 64 chord recipes within a 2048-step loop.');
  const sorted=recipes.slice().sort((a,b)=>a.start-b.start);let end=-1;
  const events=sorted.flatMap(c=>{if(!integer(c.start,0,length-1)||c.start<=end||c.start+c.length>length)throw Error('Chords must fit inside the loop without overlapping.');end=c.start+c.length-1;return recipeNotes(c).map(pitch=>({start:c.start,end,pitch,velocity:c.velocity}))});
  if(events.length>128)throw Error('Chords exceed the track limit of 128 note events.');return events;
}
export function encodeRecipes(recipes,draft,enabled=true){
  const version=[draft,...recipes].some(c=>c.scaleFree||c.type>12)?4:[draft,...recipes].some(c=>c.type===12||c.quantize)?3:[draft,...recipes].some(c=>c.inversion)?2:1;
  const out=[67,version,Number(enabled),recipes.length],byte=n=>out.push(n&255),word=n=>out.push(n&255,n>>8);
  function body(c,isDraft){recipeNotes(c);if(isDraft){byte(c.root);byte(c.type);word(c.length)}else{word(c.start);word(c.length);byte(c.root);byte(c.type)}byte(c.velocity);byte(c.scaleRoot);byte(c.scaleKind);if(version>=2)byte(c.inversion??0);
    if(version>=4)byte(Number(!!c.scaleFree));
    if(version>=3){byte(Number(!!c.quantize));if(c.quantize){byte(c.quantizeRoot??c.scaleRoot);byte(c.quantizeKind??c.scaleKind)}if(c.type===12){byte(c.intervals.length);c.intervals.forEach(byte);byte(c.name.length);[...c.name].forEach(ch=>byte(ch.charCodeAt(0)))}}
  }
  body(draft,true);recipes.forEach(c=>body(c,false));return out;
}
const identity=e=>`${e.start}/${e.end}/${e.pitch}/${e.velocity}`;
// Read the track object by field boundaries, never search note bytes for a tag.
export function decodeChordProject(input,{allowUnowned=false}={}){
  const bytes=Uint8Array.from(input);let pos=0;
  const take=n=>{if(pos+n>bytes.length)throw Error('Truncated track project.');const v=bytes.slice(pos,pos+n);pos+=n;return v},byte=()=>take(1)[0],word=()=>{const b=take(2);return b[0]|b[1]<<8};
  const length=word();if(!integer(length,1,2048))throw Error('Invalid loop length.');
  take(51); // Fixed track, Euclid, accumulator and CC values.
  for(let i=0;i<12;i++){byte();const size=word();if(size>256)throw Error('Invalid drum lane.');take(size);const mods=byte();take(mods*4)}
  const notesStart=pos,count=byte();if(count>128)throw Error('Invalid note count.');
  const events=Array.from({length:count},()=>({start:word(),end:word(),pitch:byte(),velocity:byte()}));
  if(events.some(e=>!integer(e.start,0,2047)||!integer(e.end,e.start,2047)||e.pitch>127||!integer(e.velocity,1,127)))throw Error('Invalid note event.');
  const notesEnd=pos,cc=word();take(cc*4);
  if(bytes[pos]===65){byte();if(byte()!==1||byte()>1)throw Error('Unsupported CC automation mode.');}
  if(bytes[pos]===76){byte();if(byte()!==1)throw Error('Unsupported lane rhythm extension.');const mask=word();if(!mask||mask>4095)throw Error('Invalid lane rhythm mask.');for(let n=0;n<12;n++)if(mask&(1<<n))take(26);}
  if(bytes[pos]===83){byte();if(byte()!==1)throw Error('Unsupported scale extension.');take(4)}
  if(bytes[pos]===80){byte();if(byte()!==1||byte()!==0)throw Error('Unsupported palette extension.')}
  const recipeStart=pos;
  let enabled=0,draft={root:60,type:0,length:4,velocity:100,scaleRoot:0,scaleKind:0,inversion:0},recipes=[];
  const hasRecipes=bytes[pos]===67;
  if(hasRecipes){
    byte();const version=byte();if(![1,2,3,4].includes(version))throw Error('Unsupported chord recipe version.');
    enabled=byte();const size=byte();if(enabled>1||size>64)throw Error('Unsupported chord track.');
    const extra=c=>{if(version>=4){const free=byte();if(free>1)throw Error('Invalid chord scale policy.');c.scaleFree=!!free;}else if(c.type>12)throw Error('Extended chords need recipe version 4.');if(version>=3){const quantize=byte();if(quantize>1)throw Error('Invalid quantization.');c.quantize=!!quantize;if(quantize){c.quantizeRoot=byte();c.quantizeKind=byte()}if(c.type===12){const count=byte();if(count<2||count>8)throw Error('Invalid custom chord size.');c.intervals=Array.from({length:count},()=>{const n=byte();return n>127?n-256:n});const length=byte();if(!length||length>12)throw Error('Invalid chord name.');c.name=String.fromCharCode(...take(length));}}else if(c.type===12)throw Error('Custom chords need recipe version 3.');return c};
    draft=extra({root:byte(),type:byte(),length:word(),velocity:byte(),scaleRoot:byte(),scaleKind:byte(),inversion:version>=2?byte():0});recipeNotes(draft);
    recipes=Array.from({length:size},()=>extra({start:word(),length:word(),root:byte(),type:byte(),velocity:byte(),scaleRoot:byte(),scaleKind:byte(),inversion:version>=2?byte():0}));
  }else if(!allowUnowned)throw Error('This track has no supported chord recipes. Read it for adoption in the workshop.');
  if(bytes[2]!==1||bytes[3]!==0)throw Error('Choose a Melody or Chord track without a drum layer.');
  const recipeEnd=pos;
  if(bytes[pos]===86){byte();if(byte()!==1)throw Error('Unsupported variation extension.');take(11);}
  const moduleStart=pos,module=decodeModule(bytes.slice(pos));
  const stored=module.version===2&&module.components?.some(c=>c.id==='rhythm.stored')&&module.components.some(c=>c.id==='pitch.track');
  if(hasRecipes&&!stored)throw Error('Select Chord mode on the keyboard before editing this track.');
  const replacement=defaultModule('euclid');replacement.config.enabled=false;replacement.config.pool=[];
  const replacementModule=stored?null:encodeModule(composeModule(replacement,{id:'rhythm.stored',version:1,params:{}}));
  const owned=recipeEvents(recipes,length),remaining=events.slice();
  for(const e of owned){const i=remaining.findIndex(n=>identity(n)===identity(e));if(i<0)throw Error('Chord recipe disagrees with its recorded notes.');remaining.splice(i,1)}
  if(remaining.some(e=>recipes.some(c=>e.start<c.start+c.length&&e.end>=c.start)))throw Error('Unowned notes overlap a chord recipe.');
  return {bytes,length,channel:bytes[4],enabled:!!enabled,draft,recipes,events,foreign:remaining,notesStart,notesEnd,recipeStart,recipeEnd,moduleStart,replacementModule,hasRecipes};
}
export function encodeChordProject(source,recipes,channel=source.channel){
  if(!integer(channel,0,15))throw Error('Invalid MIDI channel.');
  const owned=recipeEvents(recipes,source.length);
  if(source.foreign.some(e=>recipes.some(c=>e.start<c.start+c.length&&e.end>=c.start)))throw Error('A chord overlaps other recorded notes. Move or shorten it.');
  const events=[...source.foreign,...owned].sort((a,b)=>a.start-b.start||a.pitch-b.pitch);
  if(events.length>128)throw Error('Chords and other notes exceed the 128-event track limit.');
  const out=[...source.bytes.slice(0,source.notesStart),events.length];out[4]=channel;
  if(source.replacementModule){out.fill(0,9,33);out[14]=16;out[32]=255;} // Canonical stored rhythm and empty pitch palette.
  for(const e of events)out.push(e.start&255,e.start>>8,e.end&255,e.end>>8,e.pitch,e.velocity);
  out.push(...source.bytes.slice(source.notesEnd,source.recipeStart),...encodeRecipes(recipes,source.draft),...source.bytes.slice(source.recipeEnd,source.moduleStart??source.recipeEnd),...(source.replacementModule??source.bytes.slice(source.moduleStart??source.recipeEnd)));
  return Uint8Array.from(out);
}
export function recipeProgression(recipes,length){
  return {length,recipes,pattern:'chords',chords:recipes.map(c=>({start:c.start,end:c.start+c.length-1,notes:recipeNotes(c),label:chordLabel(c),velocity:c.velocity,recipe:{...c}})),events:recipeEvents(recipes,length)};
}

export function conformRecipesToScale(recipes,length,scale){
  if(!scale?.enabled||!integer(scale.root,0,11)||!integer(scale.kind,0,scaleNames.length-1))throw Error('Enable a global key and scale first with Ctrl + Space.');
  recipeEvents(recipes,length);
  const allowed=scaleNotes(scale.root,scale.kind);
  const next=recipes.map(c=>{
    let root=c.root;
    for(let distance=0;distance<128;distance++){
      const lower=c.root-distance,upper=c.root+distance;
      if(lower>=0&&allowed.includes(lower%12)){root=lower;break}
      if(upper<=127&&allowed.includes(upper%12)){root=upper;break}
    }
    if(c.scaleFree)return setChordScaleFree(c,false,scale);
    if(c.type===12)return {...c,quantize:true,quantizeRoot:scale.root,quantizeKind:scale.kind};
    return {...c,root,scaleFree:false,type:scaleChordTypes.includes(c.type)?c.type:(c.type>=6&&c.type!==10)?11:10,quantize:false,scaleRoot:scale.root,scaleKind:scale.kind};
  });
  recipeEvents(next,length);return next;
}
export function decodeProjectScale(bytes){
  // Global v1: version, tempo, five settings, twelve drum pitches, optional G/1.
  if(bytes[0]!==1||![20,25].includes(bytes.length))throw Error('Unsupported global project settings.');
  if(bytes.length===20)return {enabled:false,root:0,kind:0};
  if(bytes[20]!==71||bytes[21]!==1||bytes[22]>1||bytes[23]>11||bytes[24]>=scaleNames.length)throw Error('Invalid global scale.');
  return {enabled:!!bytes[22],root:bytes[23],kind:bytes[24]};
}
export function sameScale(a,b){return !!a&&!!b&&!!a.enabled===!!b.enabled&&a.root===b.root&&a.kind===b.kind}

export function adoptionCandidates(source){
  const groups=new Map();for(const e of source.foreign){const id=`${e.start}/${e.end}`;if(!groups.has(id))groups.set(id,[]);groups.get(id).push(e)}
  return [...groups.values()].filter(group=>group.length>=2&&group.length<=8&&group.every(e=>e.velocity===group[0].velocity)&&new Set(group.map(e=>e.pitch)).size===group.length&&group[0].end<source.length&&!source.events.some(e=>!group.includes(e)&&!(e.start===group[0].start&&e.end===group[0].end&&group.some(g=>identity(g)===identity(e)))&&e.start<=group[0].end&&e.end>=group[0].start)).map(group=>({start:group[0].start,length:group[0].end-group[0].start+1,velocity:group[0].velocity,notes:group.map(e=>e.pitch).sort((a,b)=>a-b)}));
}
export function adoptChord(source,recipes,candidate,root,name){
  if(!adoptionCandidates(source).some(c=>JSON.stringify(c)===JSON.stringify(candidate)))throw Error('These notes cannot be grouped without changing other events.');
  const recipe=customRecipe(candidate.notes,root,name,{start:candidate.start,length:candidate.length,velocity:candidate.velocity});
  const nextSource={...source,foreign:source.foreign.filter(e=>e.start!==candidate.start||e.end!==candidate.start+candidate.length-1)},next=[...recipes,recipe].sort((a,b)=>a.start-b.start);encodeChordProject(nextSource,next);return {source:nextSource,recipes:next};
}
