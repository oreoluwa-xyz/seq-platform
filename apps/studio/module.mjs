import {componentHelp} from './component-help.mjs?v=1b4a360ca4';
export const ENGINES = ['euclid', 'chord-bank'];
const own = (v, keys, where) => {
  if (!v || typeof v !== 'object' || Array.isArray(v)) throw Error(`${where} must be an object.`);
  for (const key of Object.keys(v)) if (!keys.includes(key)) throw Error(`Unsupported field ${where}.${key}.`);
};
const integer = (v, min, max, name) => { if (!Number.isInteger(v) || v < min || v > max) throw Error(`${name} must be an integer from ${min} to ${max}.`); return v; };
const label = (v, name) => { if (typeof v !== 'string' || !v.trim() || v.length > 64) throw Error(`${name} must contain 1–64 characters.`); return v; };
export function validateModule(input) {
  if(input?.version===2)return validateComposition(input);
  own(input, ['format','version','id','name','engine','config'], 'module');
  if (input.format !== 'seq-module' || ![1,3,4].includes(input.version)) throw Error('Expected a seq-module file, version 1, 3 or 4.');
  label(input.id, 'ID'); if (!/^[a-z0-9][a-z0-9.-]*$/.test(input.id)) throw Error('ID must use lowercase letters, numbers, dots, or hyphens.');
  label(input.name, 'Name');
  if (!ENGINES.includes(input.engine)) throw Error('This firmware supports Euclid and Chord Bank engines.');
  const c=input.config;
  if (input.version>=3 && input.engine!=='euclid') throw Error('Module version 3 requires Euclid.');
  if (input.engine === 'chord-bank') {
    own(c,['selected','transpose','inversion','slots'],'config');
    integer(c.selected,0,11,'Selected slot'); integer(c.transpose,-24,24,'Transpose'); integer(c.inversion,-3,3,'Inversion');
    if (!Array.isArray(c.slots) || c.slots.length !== 12) throw Error('A chord bank must contain exactly 12 slots. Use an empty notes array for an unused slot.');
    c.slots.forEach((slot,i) => {
      own(slot,['name','notes','velocity','length','root'],`slot ${i+1}`); label(slot.name,`Slot ${i+1} name`);
      if(slot.root!==undefined&&slot.root!==null)integer(slot.root,0,127,`Slot ${i+1} root`);
      if (!Array.isArray(slot.notes) || slot.notes.length>8) throw Error(`Slot ${i+1} can hold at most 8 notes.`);
      slot.notes.forEach((note,j) => { integer(note,0,127,`Slot ${i+1} note`); if (j && note<=slot.notes[j-1]) throw Error(`Slot ${i+1} notes must be unique and ascending.`); });
      integer(slot.velocity,1,127,`Slot ${i+1} velocity`); integer(slot.length,1,2048,`Slot ${i+1} length`);
    });
  } else {
    own(c,['enabled','steps','fills','rotation','probability','accent','ratchet','root','pool','pin','template',...(input.version>=3?['pitchMode']:[]),...(input.version===4?['qualities']:[])], 'config');
    if(input.version>=3&&!(input.version===4?['palette','transpose','chords']:['palette','transpose']).includes(c.pitchMode))throw Error('Choose palette or transpose pitch mode.');
    if(input.version===4){if(!Array.isArray(c.qualities)||c.qualities.length!==8||c.qualities.some(v=>!Number.isInteger(v)||(v>11&&v!==16)||v<0))throw Error('Invalid palette chord qualities.');}
    if (typeof c.enabled !== 'boolean') throw Error('Enabled must be true or false.');
    integer(c.steps,1,2048,'Steps'); integer(c.fills,0,c.steps,'Fills'); integer(c.rotation,0,c.steps-1,'Rotation');
    integer(c.probability,0,100,'Probability'); integer(c.accent,0,100,'Accent'); integer(c.ratchet,0,Math.min(255,c.steps),'Ratchet threshold'); integer(c.root,0,11,'Palette root');
    if (!Array.isArray(c.pool) || c.pool.length>8) throw Error('Euclid supports 0–8 palette offsets.');
    c.pool.forEach(n=>integer(n,-128,127,'Palette offset'));
    if (c.pin !== null) integer(c.pin,0,c.pool.length-1,'Palette pin');
    if (c.template !== null) { integer(c.template,0,65535,'Template mask'); if(c.steps>16) throw Error('Captured templates support at most 16 steps.'); }
  }
  return structuredClone(input);
}
export function defaultModule(engine='chord-bank') {
  const base={format:'seq-module',version:engine==='euclid'?3:1,id:engine==='chord-bank'?'after-hours':'four-on-sixteen',name:engine==='chord-bank'?'After hours':'Four on sixteen',engine};
  if(engine==='euclid') base.config={pitchMode:'palette',enabled:true,steps:16,fills:4,rotation:0,probability:100,accent:0,ratchet:0,root:0,pool:[0],pin:null,template:null};
  else {
    const names=['Cmaj7','Dm7','Em7','Fmaj7','G7','Am7','Bm7♭5','Cadd9','Fsus2','Gsus4','Am9','G13'];
    const notes=[[48,52,55,59],[50,53,57,60],[52,55,59,62],[53,57,60,64],[43,47,50,53],[45,48,52,55],[47,50,53,57],[48,52,55,62],[53,55,60],[43,48,50],[45,48,52,55,59],[43,47,50,53,64]];
    base.config={selected:0,transpose:0,inversion:0,slots:names.map((name,i)=>({name,root:[48,50,52,53,43,45,47,48,53,43,45,43][i],notes:notes[i],velocity:96,length:4}))};
  }
  return base;
}
export function encodeModule(input) {
  if(input?.version===2)return encodeComposition(validateComposition(input));
  const m=validateModule(input), c=m.config, out=[77,m.version===3&&c.pitchMode==='transpose'?1:m.version,m.engine==='euclid'?0:1];
  const b=v=>out.push(v&255), w=v=>{b(v);b(v>>8);};
  if(m.engine==='euclid') {
    b(Number(c.enabled)); w(c.steps); w(c.fills); w(c.rotation); b(c.probability); b(c.accent); b(c.ratchet); b(c.root); b(c.pool.length); b(c.pin??255); b(Number(c.template!==null)); w(c.template??0);
    for(let i=0;i<8;i++) b(c.pool[i]??0);
    if(m.version===3&&c.pitchMode==='palette')b(0);
    if(m.version===4){b(['palette','transpose','chords'].indexOf(c.pitchMode));c.qualities.forEach(b);}
  } else {
    b(c.selected); b(c.transpose); b(c.inversion);
    for(const slot of c.slots) { b(slot.notes.length); b(slot.velocity); w(slot.length); slot.notes.forEach(b); }
  }
  return Uint8Array.from(out);
}
export function decodeModule(data) {
  let pos=0;
  const b=()=>{if(pos>=data.length) throw Error('Truncated module data.'); return data[pos++];};
  const w=()=>b()|(b()<<8), signed=v=>v>127?v-256:v;
  if(b()!==77)throw Error('Unsupported module wire format.');
  const version=b();if(version===2)return decodeComposition(data);
  if(version!==1&&version!==3&&version!==4) throw Error('Unsupported module wire version.');
  const kind=b(); if(kind>1) throw Error('Unsupported engine.');
  const m=defaultModule(ENGINES[kind]); m.version=version; m.name='Device module'; m.id='device-module';
  if(!kind) {
    const enabled=b(); if(enabled>1) throw Error('Invalid enabled flag.');
    const c={enabled:!!enabled,steps:w(),fills:w(),rotation:w(),probability:b(),accent:b(),ratchet:b(),root:b()};
    const count=b(),pin=b(),active=b(),mask=w();
    if(count>8||active>1) throw Error('Invalid Euclid data.');
    const pool=Array.from({length:8},()=>signed(b())); c.pool=pool.slice(0,count); c.pin=pin===255?null:pin; c.template=active?mask:null; if(version>=3){const mode=b();if(mode>(version===4?2:1))throw Error('Invalid pitch mode.');c.pitchMode=['palette','transpose','chords'][mode];if(version===4)c.qualities=Array.from({length:8},b);} m.config=c;
  } else {
    const c={selected:b(),transpose:signed(b()),inversion:signed(b()),slots:[]};
    for(let i=0;i<12;i++) { const count=b(),velocity=b(),length=w(); if(count>8) throw Error('Invalid voice count.'); c.slots.push({name:`Chord ${i+1}`,notes:Array.from({length:count},b),velocity,length}); }
    m.config=c;
  }
  if(pos!==data.length) throw Error('Unexpected trailing module data.');
  return validateModule(m);
}
export function chordNotes(m, slot=m.config.selected) {
  const c=m.config; let notes=c.slots[slot].notes.map(n=>n+c.transpose);
  if(!notes.length) return [];
  for(let i=0;i<c.inversion;i++) { notes.push(notes.shift()+12); notes.sort((a,b)=>a-b); }
  for(let i=0;i>c.inversion;i--) { notes.unshift(notes.pop()-12); notes.sort((a,b)=>a-b); }
  return [...new Set(notes.map(n=>Math.max(0,Math.min(127,n))))];
}
export const noteName=n=>['C','C♯','D','D♯','E','F','F♯','G','G♯','A','B♭','B'][n%12]+(Math.floor(n/12)-1);
export function parseNotes(text) {
  if(!text.trim()) return [];
  return [...new Set(text.trim().split(/[\s,]+/).map(token=>{
    if(/^\d+$/.test(token)) return integer(Number(token),0,127,'MIDI note');
    const match=/^([A-Ga-g])([#b♯♭]?)(-?\d)$/.exec(token);
    if(!match) throw Error(`Cannot read note “${token}”. Use C4, F#4, Bb3, or MIDI numbers.`);
    const semitone={C:0,D:2,E:4,F:5,G:7,A:9,B:11}[match[1].toUpperCase()]+(['#','♯'].includes(match[2])?1:['b','♭'].includes(match[2])?-1:0);
    return integer((Number(match[3])+1)*12+semitone,0,127,'MIDI note');
  }))].sort((a,b)=>a-b);
}
export function euclidHit(c,step) {
  if(!c.enabled||!c.fills) return false;
  const rotated=(step+c.steps-c.rotation)%c.steps;
  return c.template!==null ? !!(c.template&(1<<rotated)) : (rotated*c.fills)%c.steps<c.fills;
}
export function checksum(data) { let hash=2166136261; for(const byte of data) hash=Math.imul(hash^byte,16777619)>>>0; return Uint8Array.of(hash&255,(hash>>>8)&255,(hash>>>16)&255,hash>>>24); }


export const COMPONENTS = [
  {id:'rhythm.stored',wire:1,version:1,stage:'rhythm'},
  {id:'rhythm.euclid',wire:2,version:1,stage:'rhythm'},
  {id:'pitch.track',wire:3,version:1,stage:'pitch'},
  {id:'pitch.track',wire:3,version:2,stage:'pitch'},
  {id:'pitch.track',wire:3,version:3,stage:'pitch'},
  {id:'pitch.chord-bank',wire:4,version:1,stage:'pitch'}
].map(component=>({...component,help:componentHelp[`${component.id}@${component.version}`]}));
const rhythmFields=['enabled','steps','fills','rotation','probability','accent','ratchet','template'];
export function rhythmComponent(module=defaultModule('euclid')) {
  const c=module.config;
  return {id:'rhythm.euclid',version:1,params:Object.fromEntries(rhythmFields.map(k=>[k,c[k]]))};
}
export function composeModule(module,rhythm) {
  const m=validateModule(module);
  if(![1,3,4].includes(m.version))throw Error('Expected an editable source module.');
  const source=m.engine==='chord-bank'?{id:'pitch.chord-bank',version:1,params:m.config}:{id:'pitch.track',version:m.version===4?3:m.version===3?2:1,params:{root:m.config.root,pool:m.config.pool,pin:m.config.pin,...(m.version>=3?{pitchMode:m.config.pitchMode}:{}),...(m.version===4?{qualities:m.config.qualities}:{})}};
  return validateComposition({format:m.format,version:2,id:m.id,name:m.name,components:[rhythm,source]});
}
export function splitModule(input) {
  const m=validateModule(input);
  if(m.version!==2)return {module:m,rhythm:null};
  const [rhythm,source]=m.components;
  const module=defaultModule(source.id==='pitch.chord-bank'?'chord-bank':'euclid');
  module.id=m.id;module.name=m.name;
  if(module.engine==='chord-bank')module.config=structuredClone(source.params);
  else {if(source.version===3)module.version=4;if(source.version===1){module.version=1;delete module.config.pitchMode;}Object.assign(module.config,source.params,rhythm.id==='rhythm.euclid'?rhythm.params:{enabled:false});}
  return {module,rhythm:structuredClone(rhythm)};
}
export function validateComposition(input){
  own(input,['format','version','id','name','components'],'module');
  if(input.format!=='seq-module'||input.version!==2)throw Error('Expected module format version 2.');
  label(input.id,'ID');if(!/^[a-z0-9][a-z0-9.-]*$/.test(input.id))throw Error('Invalid module ID.');label(input.name,'Name');
  if(!Array.isArray(input.components)||input.components.length!==2)throw Error('A composition requires one rhythm followed by one pitch component.');
  input.components.forEach((c,i)=>{
    own(c,['id','version','params'],`component ${i+1}`);
    if(!COMPONENTS.some(d=>d.id===c.id&&d.version===c.version&&d.stage===(i?'pitch':'rhythm')))throw Error(`Unsupported component, version or stage: ${c.id} v${c.version}.`);
  });
  const [r,p]=input.components,legacy=defaultModule(p.id==='pitch.chord-bank'?'chord-bank':'euclid');
  if(r.id==='rhythm.stored'){own(r.params,[],'stored rhythm');}
  else {
    own(r.params,rhythmFields,'Euclidean rhythm');
    validateModule({...defaultModule('euclid'),config:{...defaultModule('euclid').config,...r.params}});
    for(const key of rhythmFields)if(!(key in r.params))throw Error(`Missing rhythm parameter ${key}.`);
  }
  if(p.id==='pitch.chord-bank'){
    legacy.config=p.params;validateModule(legacy);
    if(r.id==='rhythm.euclid'&&r.params.ratchet)throw Error('Chord-bank pitches do not support drum ratchets.');
  }else{
    own(p.params,['root','pool','pin',...(p.version>=2?['pitchMode']:[]),...(p.version===3?['qualities']:[])],'track pitch');
    if(p.version>=2&&!(p.version===3?['palette','transpose','chords']:['palette','transpose']).includes(p.params.pitchMode))throw Error('Invalid pitch mode.');
    for(const key of ['root','pool','pin'])if(!(key in p.params))throw Error(`Missing pitch parameter ${key}.`);
    validateModule({...legacy,version:p.version===3?4:legacy.version,config:{...legacy.config,...p.params}});
  }
  return structuredClone(input);
}
function encodeComposition(m){
  const [r,p]=m.components,out=[77,2,COMPONENTS.find(c=>c.id===r.id).wire,1];
  const b=v=>out.push(v&255),w=v=>{b(v);b(v>>8);};
  if(r.id==='rhythm.euclid'){
    const c=r.params;b(Number(c.enabled));w(c.steps);w(c.fills);w(c.rotation);b(c.probability);b(c.accent);b(c.ratchet);b(Number(c.template!==null));w(c.template??0);
  }
  b(COMPONENTS.find(c=>c.id===p.id).wire);b(p.id==='pitch.track'&&p.version<3&&p.params.pitchMode==='transpose'?1:p.version);
  if(p.id==='pitch.chord-bank')out.push(...encodeModule({...defaultModule('chord-bank'),config:p.params}).slice(3));
  else{const c=p.params;b(c.root);b(c.pool.length);b(c.pin??255);for(let i=0;i<8;i++)b(c.pool[i]??0);if(p.version===2&&c.pitchMode==='palette')b(0);if(p.version===3){b(['palette','transpose','chords'].indexOf(c.pitchMode));c.qualities.forEach(b);}}
  return Uint8Array.from(out);
}
function decodeComposition(data){
  let pos=2;const b=()=>{if(pos>=data.length)throw Error('Truncated composition.');return data[pos++];},w=()=>b()|b()<<8;
  const component=()=>{const id=b(),version=b(),entry=COMPONENTS.find(c=>c.wire===id&&c.version===version);if(!entry)throw Error('Unsupported component contract.');return {id:entry.id,version,params:{}};};
  const r=component();
  if(r.id==='rhythm.euclid'){
    const enabled=b();if(enabled>1)throw Error('Invalid rhythm enabled flag.');
    const c={enabled:!!enabled,steps:w(),fills:w(),rotation:w(),probability:b(),accent:b(),ratchet:b()},active=b(),mask=w();
    if(active>1)throw Error('Invalid template flag.');r.params={...c,template:active?mask:null};
  }else if(r.id!=='rhythm.stored')throw Error('Expected rhythm component.');
  const p=component();
  if(p.id==='pitch.chord-bank'){
    p.params=decodeModule(Uint8Array.from([77,1,1,...data.slice(pos)])).config;pos=data.length;
  }else if(p.id==='pitch.track'){
    const root=b(),count=b(),pin=b(),pool=Array.from({length:8},()=>{const n=b();return n>127?n-256:n;});
    if(count>8)throw Error('Invalid pool size.');p.params={root,pool:pool.slice(0,count),pin:pin===255?null:pin};if(p.version>=2){const mode=b();if(mode>(p.version===3?2:1))throw Error('Invalid pitch mode.');p.params.pitchMode=['palette','transpose','chords'][mode];if(p.version===3)p.params.qualities=Array.from({length:8},b);}
  }else throw Error('Expected pitch component.');
  if(pos!==data.length)throw Error('Unexpected composition data.');
  return validateComposition({format:'seq-module',version:2,id:'device-module',name:'Device module',components:[r,p]});
}
export function assertSupported(module,catalog){
  if(catalog && module.engine==='chord-bank' && !Array.from({length:(catalog.length-6)/2},(_,i)=>catalog[6+i*2]).includes(4))throw Error('Chord banks are not supported by this firmware. Use recorded chords or polyphonic notes.');
  if(module.version===1)return;
  if(module.version===4){if(!catalog||catalog[1]<4)throw Error('Update firmware to use chord palettes.');return;}
  if(module.version===3){if(!catalog||catalog[1]<3)throw Error('Update firmware to use palette pitches.');return;}
  if(!catalog||catalog.length<6||(catalog.length-6)%2||catalog[0]!==1||catalog[1]<2||catalog[2]<2||catalog[3]<8)throw Error('Update keyboard firmware to install composable modules.');
  if(encodeModule(module).length>(catalog[4]|catalog[5]<<8))throw Error('Composition exceeds the keyboard module size limit.');
  for(const c of module.components){const id=COMPONENTS.find(d=>d.id===c.id).wire;let found=false;for(let i=6;i<catalog.length;i+=2)if(catalog[i]===id&&catalog[i+1]===c.version)found=true;if(!found)throw Error(`Keyboard does not support ${c.id} v${c.version}.`);}
}
