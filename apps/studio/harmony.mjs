import {encodeRecipes,recipeEvents,recipeNotes,customRecipe,quantizeNote} from './chord-project.mjs?v=6c6f346c5c';
import {scaleNames,scaleNotes} from './scale.mjs?v=bcc8228f15';
import {defaultModule,encodeModule,composeModule,chordNotes,noteName} from './module.mjs?v=08f7f2e951';
export function decodeMelody(bytes){
  const length=bytes[2]|bytes[3]<<8;
  if(bytes[0]!==1||bytes[1]>=12||!length||length>2048||bytes.length!==8+length*2||bytes[4]>1||bytes[5]>11||bytes[6]>=scaleNames.length||bytes[7]>15)throw Error('Invalid melody capture.');
  const steps=Array.from({length},(_,i)=>{const note=bytes[8+i*2],onset=bytes[9+i*2];if((note>127&&note!==255)||onset>1||(note===255&&onset))throw Error('Invalid melody step.');return {note:note===255?null:note,onset:!!onset}});
  return {track:bytes[1],length,global:{enabled:!!bytes[4],root:bytes[5],kind:bytes[6]},channel:bytes[7],steps};
}
// Providers supply named candidate chords. Timing and voice leading are shared.
export const harmonyProviders=[
  {id:'harmony.scale',version:1,label:'Scale chords',candidates:({root,kind})=>{
    const scale=scaleNotes(root,kind).map((n,i,a)=>n+(i&&n<root?12:0));
    return scale.map((n,i)=>({label:`${noteName(60+n).replace(/-?\d+$/,'')} · degree ${i+1}`,recipe:{root:60+n,type:10,scaleRoot:root,scaleKind:kind,velocity:88,length:1},notes:[0,2,4].map(d=>60+scale[(i+d)%scale.length]+12*Math.floor((i+d)/scale.length))}));
  }},
  {id:'harmony.bank',version:1,label:'Closest bank chord',candidates:({bank})=>{
    if(!bank||bank.engine!=='chord-bank')throw Error('Choose a chord bank.');
    return bank.config.slots.map((s,i)=>{
      const notes=chordNotes(bank,i);let recipe;
      if(Number.isInteger(s.root)&&notes.length>=2){try{recipe=customRecipe(notes,s.root+bank.config.transpose,shortChordName(s.name),{velocity:s.velocity,length:s.length})}catch{}}
      return {label:s.name,notes,...(recipe?{recipe}:{})};
    }).filter(s=>s.notes.length);
  }}
];
export const shortChordName=name=>name.replaceAll('♭','b').replaceAll('♯','#').replace(/[^ -~]/g,'').trim().slice(0,12)||'Custom';
const pcDistance=(a,b)=>Math.min((a%12-b%12+12)%12,(b%12-a%12+12)%12);
function chooseChord(note,candidates,previous){
  let best=null;
  for(const candidate of candidates)for(let inversion=0;inversion<candidate.notes.length;inversion++){
    const rotated=candidate.notes.slice().sort((a,b)=>a-b);for(let i=0;i<inversion;i++){let raised=rotated.shift()+12;while(rotated.includes(raised))raised+=12;rotated.push(raised);rotated.sort((a,b)=>a-b)}
    for(let octave=-5;octave<=5;octave++){
      const notes=[...new Set(rotated.map(n=>n+octave*12))].sort((a,b)=>a-b);
      if(notes[0]<0||notes.at(-1)>127)continue;
      const fit=Math.min(...notes.map(n=>pcDistance(n,note)));
      const movement=previous?previous.reduce((sum,n)=>sum+Math.min(...notes.map(p=>Math.abs(p-n))),0)/previous.length:0;
      const distance=Math.min(...notes.map(n=>Math.abs(n-note)));
      const score=fit*1000+movement*3+distance*2+Math.abs(notes.at(-1)-note)*.2;
      if(!best||score<best.score)best={label:candidate.label,notes,score,matched:fit===0,...(candidate.recipe&&candidate.recipe.root+octave*12>=0&&candidate.recipe.root+octave*12<=127?{recipe:{...candidate.recipe,root:candidate.recipe.root+octave*12,inversion}}:{})};
    }
  }
  if(!best)throw Error('This provider has no playable chords.');return best;
}
export function harmonize(source,{provider='harmony.scale',root=source.global.root,kind=source.global.kind,rate='bar',bank}={}){
  if(!['hit','beat','bar'].includes(rate)||!Number.isInteger(root)||root<0||root>11||!Number.isInteger(kind)||kind<0||kind>=scaleNames.length)throw Error('Invalid harmony settings.');
  const component=harmonyProviders.find(p=>p.id===provider);if(!component)throw Error('Unknown harmony provider.');
  const candidates=component.candidates({root,kind,bank});if(!candidates.length)throw Error('The chord bank is empty.');
  const anchors=[];
  if(rate==='hit')source.steps.forEach((s,i)=>{if(s.onset&&s.note!==null)anchors.push({start:i,note:s.note})});
  else {const width=rate==='beat'?4:16;for(let start=0;start<source.length;start+=width){const notes=source.steps.slice(start,start+width).filter(s=>s.note!==null);if(notes.length)anchors.push({start,note:notes[0].note,end:Math.min(source.length,start+width)-1})}}
  const minimumEvents=anchors.length*Math.min(...candidates.map(c=>c.notes.length));
  if(minimumEvents>128)throw Error(`At least ${minimumEvents} note events would exceed the track limit of 128. Choose a slower chord-change rate.`);
  let previous=null;const chords=anchors.map((a,i)=>{const chord=chooseChord(a.note,candidates,previous);previous=chord.notes;return {start:a.start,end:a.end??((anchors[i+1]?.start??source.length)-1),anchor:a.note,...chord}});
  const events=chords.flatMap(c=>c.notes.map(pitch=>({start:c.start,end:c.end,pitch,velocity:88})));
  return {provider:{id:component.id,version:component.version},root,kind,rate,length:source.length,chords,events};
}
export function encodeChordTrack(result,channel,{editable=false,inversions=true,custom=true}={}){
  if(!result.events.length)throw Error('The captured melody has no notes.');
  if(result.events.length>128)throw Error(`${result.events.length} note events exceed the track limit of 128. Choose a slower chord-change rate.`);
  if(!Number.isInteger(channel)||channel<0||channel>15)throw Error('Invalid MIDI channel.');
  if(!Number.isInteger(result.length)||result.length<1||result.length>2048)throw Error('Invalid loop length.');
  for(const event of result.events)if(![event.start,event.end,event.pitch,event.velocity].every(Number.isInteger)||event.start<0||event.end<event.start||event.end>=result.length||event.pitch<0||event.pitch>127||event.velocity<1||event.velocity>127)throw Error('Invalid chord event.');
  const out=[],u8=n=>out.push(n&255),u16=n=>{u8(n);u8(n>>8)};
  u16(result.length);[1,0,channel,100,100,0,0].forEach(u8); // melodic track, no drum layer
  [0,0].forEach(u8);u16(0);u8(0);u16(16);u16(0);u16(0);u8(0);Array(8).fill(0).forEach(u8);
  [0,0,0,255,0,0,0,0].forEach(u8);Array(16).fill(64).forEach(u8);
  for(let lane=0;lane<12;lane++){u8(0);u16(0);u8(0)}
  u8(result.events.length);for(const event of result.events){u16(event.start);u16(event.end);u8(event.pitch);u8(event.velocity)}u16(0);
  [83,1,2,0,0,0].forEach(u8); // Keep the chosen voicings unconstrained on this derived track.
  if(editable){const recipes=editableRecipes(result,{inversions,custom});if(recipes.length)out.push(...encodeRecipes(recipes,recipes[0]));}
  const module=defaultModule('euclid');module.config.enabled=false;module.config.pool=[];module.config.pin=null;
  out.push(...encodeModule(composeModule(module,{id:'rhythm.stored',version:1,params:{}})));
  return Uint8Array.from(out);
}

// Chord vocabulary, progression timing and note patterns remain separate stages.
export function progressionCandidates({provider='harmony.scale',root=0,kind=0,bank}={}) {
  if(!Number.isInteger(root)||root<0||root>11||!Number.isInteger(kind)||kind<0||kind>=scaleNames.length)throw Error('Invalid key or scale.');
  const component=harmonyProviders.find(p=>p.id===provider);if(!component)throw Error('Unknown harmony provider.');
  const candidates=component.candidates({root,kind,bank});
  if(!candidates.length)throw Error('This provider has no playable chords.');
  return candidates;
}
export function buildProgression(entries,settings={}) {
  if(!Array.isArray(entries)||!entries.length||entries.length>32)throw Error('Use between 1 and 32 chords.');
  const candidates=progressionCandidates(settings);let length=0;
  const chords=entries.map(({candidate,duration=16,inversion=0,octave=0})=>{
    if(!Number.isInteger(candidate)||!candidates[candidate])throw Error('Choose a chord for every position.');
    const chord=candidates[candidate];
    if(!Number.isInteger(duration)||duration<1||duration>2048||!Number.isInteger(inversion)||inversion<0||inversion>=chord.notes.length||!Number.isInteger(octave)||octave<-5||octave>5)throw Error('Invalid chord duration or voicing.');
    const notes=chord.notes.map(n=>n+octave*12).sort((a,b)=>a-b);
    for(let i=0;i<inversion;i++){let raised=notes.shift()+12;while(notes.includes(raised))raised+=12;notes.push(raised);notes.sort((a,b)=>a-b)}
    if(notes.some(n=>!Number.isInteger(n)||n<0||n>127))throw Error('This voicing is outside the MIDI note range.');
    const start=length;length+=duration;
    return {start,end:length-1,label:chord.label,notes,...(chord.recipe&&chord.recipe.root+octave*12>=0&&chord.recipe.root+octave*12<=127?{recipe:{...chord.recipe,root:chord.recipe.root+octave*12,inversion}}:{})};
  });
  if(length>2048)throw Error('The progression exceeds the 2048-step loop limit.');
  return {provider:{id:settings.provider??'harmony.scale',version:1},length,chords,events:chords.flatMap(c=>c.notes.map(pitch=>({start:c.start,end:c.end,pitch,velocity:88})))};
}
export const notePatterns=[{id:'chords',label:'Chords'},{id:'arp-up',label:'Arpeggio · up'},{id:'arp-down',label:'Arpeggio · down'},{id:'arp-bounce',label:'Arpeggio · up / down'},{id:'bass',label:'Bass · lowest chord tone'}];
export function deriveNotes(progression,{pattern='chords',stride=2,octave=0}={}) {
  if(!notePatterns.some(p=>p.id===pattern)||![1,2,4,8,16].includes(stride)||!Number.isInteger(octave)||octave<-3||octave>3)throw Error('Invalid note pattern settings.');
  const events=[];
  const emit=(start,end,pitch,velocity=88)=>{
    pitch+=octave*12;if(pitch<0||pitch>127)throw Error('The output is outside the MIDI note range.');
    if(events.length===128)throw Error('More than 128 note events. Use a slower note rate or a shorter progression.');
    events.push({start,end,pitch,velocity});
  };
  for(const chord of progression.chords){
    const notes=[...new Set(chord.notes)].sort((a,b)=>a-b);
    if(!notes.length)continue;
    if(pattern==='chords'){for(const pitch of notes)emit(chord.start,chord.end,pitch,chord.velocity??88);continue}
    const sequence=pattern==='bass'?[notes[0]]:pattern==='arp-down'?notes.slice().reverse():pattern==='arp-bounce'?[...notes,...notes.slice(1,-1).reverse()]:notes;
    let i=0;for(let start=chord.start;start<=chord.end;start+=stride)emit(start,Math.min(start+stride-1,chord.end),sequence[i++%sequence.length],chord.velocity??88);
  }
  return {...progression,pattern,outputOctave:octave,events};
}
// Baseline on connection; a held shortcut must not reopen the workshop each poll.
export function harmonyRequestChanged(previous,current) {
  return Number.isInteger(previous)&&Number.isInteger(current)&&previous!==current;
}

// Only an explicit provider recipe can become hardware-editable. Never infer a root from the bass.
export function editableRecipes(result,{inversions=true,custom=true}={}){
  if(result.pattern&&result.pattern!=='chords')return [];
  const recipes=result.chords.filter(c=>c.recipe&&(inversions||!c.recipe.inversion)&&(custom||c.recipe.type!==12&&!c.recipe.quantize)).map(c=>({...c.recipe,start:c.start,length:c.end-c.start+1,root:c.recipe.root+(result.outputOctave??0)*12,velocity:c.velocity??88}));
  recipeEvents(recipes,result.length);
  for(const c of recipes){const expected=recipeNotes(c);const actual=result.events.filter(e=>e.start<c.start+c.length&&e.end>=c.start);
    if(actual.length!==expected.length||actual.some(e=>e.start!==c.start||e.end!==c.start+c.length-1||e.velocity!==c.velocity||!expected.includes(e.pitch))||new Set(actual.map(e=>e.pitch)).size!==actual.length)throw Error('Provider recipe does not match the output notes.');
  }return recipes;
}

export function quantizeProgression(progression,root,kind){
  if(!Number.isInteger(root)||root<0||root>11||!Number.isInteger(kind)||kind<0||kind>=scaleNames.length)throw Error('Choose a supported key and scale.');
  const chords=progression.chords.map(c=>({...c,notes:[...new Set(c.notes.map(n=>quantizeNote(n,root,kind)))],...(c.recipe?{recipe:{...c.recipe,quantize:true,quantizeRoot:root,quantizeKind:kind}}:{})}));
  return {...progression,chords,events:chords.flatMap(c=>c.notes.map(pitch=>({start:c.start,end:c.end,pitch,velocity:c.velocity??88})))};
}
