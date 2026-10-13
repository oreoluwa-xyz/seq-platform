import {rootNames,scaleNames,scaleNotes} from './scale.mjs?v=bcc8228f15';
import {noteName} from './module.mjs?v=9e9d1bb338';
export const chordTypes=['Major','Minor','Diminished','Augmented','Sus2','Sus4','Dominant 7','Major 7','Minor 7','Half-diminished 7','In-scale triad','In-scale seventh','Custom','In-scale ninth','In-scale sixth','In-scale add9','Diminished 7'];
const suffix=['','m','dim','aug','sus2','sus4','7','maj7','m7','m7♭5',' · scale triad',' · scale 7'];
export const scaleChordTypes=[10,11,13,14,15];
export const scaleChordDegrees=type=>type===14?[0,2,4,5]:type===15?[0,2,4,8]:type===10?[0,2,4]:type===13?[0,2,4,6,8]:[0,2,4,6];
export function scaleChordNotes(c){
  const allowed=scaleNotes(c.scaleRoot,c.scaleKind),degrees=Array.from({length:128-c.root},(_,i)=>c.root+i).filter(n=>allowed.includes(n%12));
  return scaleChordDegrees(c.type).map(i=>degrees[i]);
}
export function chordLabel(c){
  let name=c.type===12?` · ${c.name||'Custom'}`:c.type===16?'dim7':suffix[c.type]??' ?';
  if(scaleChordTypes.includes(c.type)&&Number.isInteger(c.scaleRoot)&&Number.isInteger(c.scaleKind)){
    const notes=scaleChordNotes(c),intervals=notes.map(n=>n-c.root),triad=intervals.slice(0,3).join(',');
    const quality={'0,4,7':'','0,3,7':'m','0,3,6':'dim','0,4,8':'aug'}[triad];
    if(quality!==undefined){
      name=quality;
      if(c.type===11||c.type===13){const seventh=intervals[3];name=quality==='dim'&&seventh===10?'m7♭5':quality==='dim'&&seventh===9?'dim7':quality+(seventh===11?'maj7':seventh===10?'7':' · scale 7');if(c.type===13)name+=`(${intervals[4]===13?'♭9':intervals[4]===14?'9':intervals[4]===15?'♯9':'scale 9'})`;}
      if(c.type===14)name+=intervals[3]===8?'(♭6)':intervals[3]===9?'6':' · scale sixth';if(c.type===15)name+=`(add${intervals[3]===13?'♭9':intervals[3]===14?'9':intervals[3]===15?'♯9':' scale 9'})`;
    }else name=` · ${chordTypes[c.type].toLowerCase()}`;
  }
  return noteName(c.root).replace(/-?\d+$/,'')+name+(c.scaleFree?' · Free':c.quantize?' · scale':'')+(c.inversion?` · inv ${c.inversion}`:'');
}
export const inversionNames=['Root position','1st inversion','2nd inversion','3rd inversion','4th inversion','5th inversion','6th inversion','7th inversion'];
export const chordVoices=(type,intervals)=>type===12?(intervals?.length??8):type===13?5:type<6||type===10?3:4;
export function decodeChordMode(bytes){
  const word=i=>bytes[i]|bytes[i+1]<<8;
  if(bytes.length!==(bytes[0]>=3?448:192)||![1,2,3,4].includes(bytes[0])||bytes[1]>11||bytes[2]>1||bytes[3]>127||bytes[4]>=chordTypes.length||!word(5)||word(5)>2048||bytes[7]>(bytes[0]>=3?4:bytes[0]>=2?3:2)||bytes[10]>3||bytes[11]>64||bytes[12]>8||bytes[13]>1||bytes[14]>11||bytes[15]>10||!word(26)||word(26)>2048)throw Error('Invalid chord-mode snapshot.');
  const state={track:bytes[1],enabled:!!bytes[2],root:bytes[3],type:bytes[4],length:word(5),parameter:bytes[7],selected:word(8),error:bytes[10],count:bytes[11],notes:[...bytes.slice(16,16+bytes[12])],held:!!bytes[13],scaleRoot:bytes[14],scaleKind:bytes[15],bar:word(24),loopLength:word(26),mask:word(28),inversion:bytes[0]>=2?bytes[30]:0,inversionSupported:bytes[0]>=2};
  if(bytes[0]>=4){state.scaleBoundSupported=true;state.scaleFree=!!bytes[56];state.scaleEnabled=!!bytes[57];if(bytes[56]>1||bytes[57]>1)throw Error('Invalid chord scale policy.');}
  else if(state.type>12)throw Error('Unsupported chord type.');
  const label=(start)=>{const data=[...bytes.slice(start,start+12)],end=data.indexOf(0),used=end<0?data:data.slice(0,end);if(used.some(n=>n<32||n>126)||end>=0&&data.slice(end).some(n=>n))throw Error('Invalid chord name.');return String.fromCharCode(...used)};
  if(bytes[0]>=3){state.customSupported=true;state.quantize=!!bytes[31];state.voiceCount=bytes[32];state.intervals=[...bytes.slice(33,33+state.voiceCount)].map(n=>n>127?n-256:n);state.name=label(41);state.velocity=bytes[53];state.quantizeRoot=bytes[54];state.quantizeKind=bytes[55];if(bytes[54]>11||bytes[55]>10||bytes[31]>1||state.voiceCount<2||state.voiceCount>8||state.velocity<1||state.velocity>127||state.type===12&&!state.name)throw Error('Invalid custom chord snapshot.');}
  else if(state.type===12)throw Error('Unsupported custom chord snapshot.');
  if(state.inversion>=(state.voiceCount??chordVoices(state.type))||state.notes.some(n=>n>127)||state.selected!==65535&&state.selected>=state.loopLength||state.bar>127)throw Error('Invalid chord selection.');
  state.steps=Array.from({length:16},(_,i)=>{const b=64+i*(bytes[0]>=3?24:8);if(!bytes[b])return null;const c={root:bytes[b+1],type:bytes[b+2],start:word(b+3),length:word(b+5),onset:!!(bytes[b]&2),inversion:bytes[0]>=2?bytes[b+7]:0};if(bytes[0]>=3){c.quantize=!!bytes[b+8];c.voiceCount=bytes[b+9];c.name=label(b+10);if(bytes[b+8]>1||c.voiceCount<2||c.voiceCount>8||c.type===12&&!c.name)throw Error('Invalid custom chord step.');}else if(c.type===12)throw Error('Unsupported custom chord step.');if(bytes[0]>=4){c.scaleFree=!!(bytes[b]&4);c.scaleRoot=bytes[b+22];c.scaleKind=bytes[b+23];if(c.scaleRoot>11||c.scaleKind>10)throw Error('Invalid chord scale context.');}else if(c.type>12)throw Error('Unsupported chord type.');if(c.inversion>=(c.voiceCount??chordVoices(c.type))||bytes[b]>(bytes[0]>=4?7:3)||c.root>127||c.type>=chordTypes.length||!c.length||c.start+c.length>state.loopLength||state.bar*16+i<c.start||state.bar*16+i>=c.start+c.length)throw Error('Invalid chord step.');return c});return state;
}
export function renderChordMode(root,state,ready){
  const c=state?.chordMode;root.hidden=!c?.enabled;if(root.hidden)return;
  root.replaceChildren();
  const scope=document.createElement('div');scope.className='section-label';scope.textContent=c.selected===65535?'CHORD / CURRENT CHORD':`CHORD / EDITING STEP ${c.selected+1}${c.held?' · HOLD':''}`;root.append(scope);
  if(c.scaleBoundSupported){const policy=document.createElement('p');policy.className='hint';policy.textContent=c.scaleFree?'Free chord · chromatic roots and unrestricted types · Shift + ] returns this chord to the scale':c.scaleEnabled?(scaleChordTypes.includes(c.type)?`Scale-bound · ${rootNames[c.scaleRoot]} ${scaleNames[c.scaleKind]} · Shift + ] frees only the selected chord`:'Stored voicing · choose a scale variation with ] + knob, or Shift + ] to edit freely'):'Scale off · Ctrl + Space chooses a key and scale';root.append(policy);}
  const title=document.createElement('strong');title.className='chord-mode-title';title.textContent=chordLabel(c);root.append(title);
  const pitches=document.createElement('span');pitches.className='chord-mode-notes';pitches.textContent=c.notes.map(noteName).join(' · ')+(c.quantize?` · Quantized to ${rootNames[c.quantizeRoot??c.scaleRoot]} ${scaleNames[c.quantizeKind??c.scaleKind]}`:'');root.append(pitches);
  const row=document.createElement('div');row.className='chord-mode-parameters';
  [['I','Length',`${c.length} steps · ${c.length/4} beats`,2],['O','Root',noteName(c.root),0],...(c.customSupported?[['P','Velocity',c.velocity,4]]:[]),...(c.inversionSupported?[['[','Inversion',c.type===12&&!c.inversion?'As supplied':inversionNames[c.inversion],3]]:[]),[']','Type',chordTypes[c.type],1]].forEach(([key,label,value,parameter])=>{const cell=document.createElement('div');cell.classList.toggle('selected',c.parameter===parameter);const k=document.createElement('kbd'),name=document.createElement('small'),v=document.createElement('strong');k.textContent=key;name.textContent=label;v.textContent=value;cell.append(k,name,v);row.append(cell)});root.append(row);
  const hint=document.createElement('p');hint.className='hint';hint.textContent=!ready?'Waiting for keyboard…':c.error?['','That chord or duration is outside the available range.','Chord or note-event capacity reached; the previous chord is unchanged.','Recorded notes overlap here. Edit them in Melody mode or choose an empty region.'][c.error]:'Note keys choose root · I / O / P / [ / ] + knob adjust · tap a step to place/select · hold a step to edit · Del + step deletes · Esc returns to current chord.';root.append(hint);
}
