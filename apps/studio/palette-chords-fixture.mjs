
import {renderPalette} from './palette.mjs?v=831d6385ec';
import {paletteQualityPayload} from './palette-chords.mjs?v=b0a37ac77a';
const root=document.querySelector('#live-palette'),result=document.querySelector('#result');
const state={active:true,type:1,child:false,engine:0,track:0,running:true,step:4,bar:0,visual:{mode:'euclid',os:'mac'},scale:{enabled:true,root:0,kind:0,mode:0,direction:0},palette:{chordSupported:true,editable:true,captureEditable:true,removeSupported:true,replace:false,root:0,mode:'chords',qualities:[0,11,8,7,0,0,0,0],editDigit:2,pin:null,saved:[{offset:0,note:60},{offset:2,note:62},{offset:7,note:67},{offset:5,note:65}],capture:[],slots:[0,255,255,255,1,255,255,255,2,255,255,255,3,255,255,255]}};
let calls=0;const check=(value,text)=>{if(!value)throw Error(text)};
const render=()=>renderPalette(root,state,true,()=>{},()=>{},()=>{},(index,type)=>{paletteQualityPayload(state.palette,index,type);calls++;for(let i=0;i<state.palette.saved.length;i++)if(index===8||i===index)state.palette.qualities[i]=type;render()});
try{
 render();check(root.textContent.includes('Palette 2 · D4 · Auto seventh → Minor 7'),'Held slot and resolved Auto label');
 check(root.querySelector('.scale-notes').textContent==='D4 – F4 – A4 – C5','Auto chord tones');
 check([...root.querySelectorAll('.palette-quality-row button')].every(b=>b.disabled),'Hardware edit owns the picker');
 state.palette.editDigit=null;root.dataset.chordSlot='1';root.dataset.chordTrack='0';render();
 [...root.querySelectorAll('.palette-quality-row button')].find(b=>b.querySelector('kbd').textContent==='S').click();
 check(calls===1&&state.palette.qualities[1]===1&&state.palette.qualities[0]===0,'Click changes only selected slot');
 [...root.querySelectorAll('button')].find(b=>b.textContent==='Edit all slots').click();
 check(root.textContent.includes('Mixed qualities'),'All slots exposes mixed qualities');
 [...root.querySelectorAll('.palette-quality-row button')].find(b=>b.querySelector('kbd').textContent==='L').click();
 check(calls===2&&state.palette.qualities.slice(0,4).every(t=>t===8),'All-slot quality edit');
 state.scale.enabled=false;render();check([...root.querySelectorAll('.palette-quality-row button')].filter(b=>['Z','X'].includes(b.querySelector('kbd').textContent)).every(b=>b.disabled),'Auto unavailable without scale');
 state.scale.enabled=true;state.palette.qualities=[0,11,8,7,0,0,0,0];state.palette.editDigit=2;render();
 result.textContent='PASS · slot selection, Auto label, chord tones, hardware ownership, per-slot/all-slot edits, scale-off guard';document.title='PASS · Chord palette';
}catch(error){result.textContent=error.stack;document.title='FAIL · Chord palette';throw error;}
