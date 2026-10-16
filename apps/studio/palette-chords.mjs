import {scaleMasks} from './scale.mjs';
export const paletteTypes=[0,1,2,3,4,5,6,7,8,9,16,10,11];
export const paletteTypeKeys=['A','S','D','F','G','H','J','K','L',';',"'",'Z','X'];
export const paletteTypeNames=['Major','Minor','Diminished','Augmented','Sus2','Sus4','Dominant 7','Major 7','Minor 7','Half-diminished 7','Auto triad','Auto seventh'];
paletteTypeNames[16]='Diminished 7';
const intervals=[[0,4,7],[0,3,7],[0,3,6],[0,4,8],[0,2,7],[0,5,7],[0,4,7,10],[0,4,7,11],[0,3,7,10],[0,3,6,10]];
intervals[16]=[0,3,6,9];
export const paletteScaleEnabled=scale=>!!scale&&(scale.mode===1||scale.mode===0&&scale.enabled);
export function paletteChord(entry,type,scale){
  let root=entry.note,notes=[];
  const auto=type===10||type===11,enabled=paletteScaleEnabled(scale);
  if(auto&&enabled){
    const tonic=scale.mode===1?scale.localRoot:scale.root,kind=scale.mode===1?scale.localKind:scale.kind;
    const contains=n=>!!(scaleMasks[kind]&(1<<((n+12-tonic)%12)));
    const snap=direction=>{for(let d=0;d<128;d++){if(direction!==2&&root>=d&&contains(root-d))return root-d;if(direction!==1&&root+d<=127&&contains(root+d))return root+d;}return null;};
    root=snap(scale.direction)??snap(0)??root;
    let degree=0;const wanted=type===10?3:4;
    for(let n=root;n<128&&notes.length<wanted;n++)if(contains(n)){if(degree%2===0)notes.push(n);degree++;}
    if(notes.length!==wanted)notes=[];
  }else if(auto)notes=[root];
  else notes=(intervals[type]??[]).map(n=>root+n);
  if(notes.some(n=>n<0||n>127))notes=[];
  const resolved=paletteTypes.find(t=>intervals[t]?.length===notes.length&&intervals[t].every((n,i)=>n+root===notes[i]));
  return {root,notes,resolved:resolved??null,label:auto?enabled?`${paletteTypeNames[type]} → ${paletteTypeNames[resolved]??'Scale chord'}`:`${paletteTypeNames[type]} · scale off, root only`:paletteTypeNames[type]};
}
export function paletteQualityPayload(palette,index,type){
  if(!palette?.chordSupported||palette.mode!=='chords'||!palette.qualities||!Number.isInteger(index)||index<0||index>8||(index!==8&&index>=palette.saved.length)||!paletteTypes.includes(type))throw Error('Invalid chord palette edit.');
  const bytes=new Uint8Array(21);bytes.set([4,index,palette.saved.length,palette.root]);
  bytes.set(palette.saved.map(e=>e.offset&255),4);bytes.set(palette.qualities,12);bytes[20]=type;return bytes;
}
