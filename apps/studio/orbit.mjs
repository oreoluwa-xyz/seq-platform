import {chordLabel} from './chord-mode.mjs?v=886590c057';
import {noteName} from './module.mjs?v=08f7f2e951';

// The live protocol reports a viewed bar, not the full Euclidean configuration.
export function liveOrbitSteps(state,ready){
  const melodic=state.type!==0&&!state.child;
  return Array.from({length:16},(_,i)=>{
    const step=state.bar*16+i,outside=step>=state.length;
    return {step,outside,hit:!outside&&!!(state.pattern&(1<<i)),playhead:ready&&state.active&&state.running&&state.step===step,
      notes:melodic&&!outside?(state.projectedNotes??state.stepNotes)?.[i]??null:null};
  });
}
export function renderHardwareOrbit(root,state,ready){
  const ns='http://www.w3.org/2000/svg',svg=document.createElementNS(ns,'svg');
  svg.setAttribute('viewBox','0 0 420 420');svg.setAttribute('role','img');
  const title=document.createElementNS(ns,'title');title.textContent=`Keyboard track ${state.track+1}, viewed bar ${state.bar+1}. ${state.projectedNotes?'Projected':'Stored'} note labels; highlighted ring marks the playhead.`;svg.append(title);
  const node=(tag,attrs,text)=>{const el=document.createElementNS(ns,tag);for(const [key,value] of Object.entries(attrs))el.setAttribute(key,value);if(text!==undefined)el.textContent=text;svg.append(el);return el;};
  node('circle',{cx:210,cy:210,r:135,class:'orbit-track'});
  node('circle',{cx:210,cy:210,r:105,class:'orbit-inner'});
  const steps=liveOrbitSteps(state,ready);
  for(const [i,step] of steps.entries()){
    const angle=2*Math.PI*i/16-Math.PI/2,x=210+135*Math.cos(angle),y=210+135*Math.sin(angle);
    const dot=node('circle',{cx:x,cy:y,r:step.playhead?9:6,class:`${step.hit?'orbit-hit':'orbit-rest'}${step.playhead?' hardware-orbit-current':''}${step.outside?' hardware-orbit-outside':''}`});
    const detail=document.createElementNS(ns,'title');detail.textContent=`Step ${step.step+1}${step.outside?' · outside loop':step.hit?' · eligible hit':' · rest'}${step.notes?.length?(state.projectedNotes?' · projected ':' · stored ')+step.notes.map(noteName).join(', '):''}${step.playhead?' · playhead':''}`;dot.append(detail);
    const slot=state.palette?.slots[i];
    if(slot!==undefined&&slot!==255&&!step.outside)node('text',{x:210+116*Math.cos(angle),y:213+116*Math.sin(angle),class:'orbit-label','text-anchor':'middle'},`P${slot+1}`);
    const names=step.notes?.map(noteName)||[];
    node('text',{x:210+175*Math.cos(angle),y:210+175*Math.sin(angle),class:'hardware-orbit-note','text-anchor':'middle'},state.chordMode?.enabled&&state.chordMode.steps[i]?.onset?chordLabel(state.chordMode.steps[i]):step.outside?'—':names.length?`${names[0]}${names.length>1?' +'+(names.length-1):''}`:String(step.step+1).padStart(2,'0'));
  }
  node('text',{x:210,y:176,class:'orbit-label','text-anchor':'middle'},`LIVE / BAR ${state.bar+1}`);
  node('text',{x:210,y:216,class:'hardware-orbit-position','text-anchor':'middle'},ready&&state.active&&state.running?`STEP ${state.step+1}`:ready?'STOPPED':'UNAVAILABLE');
  const sounding=ready?state.sounding.map(noteName):[];
  node('text',{x:210,y:245,class:'hardware-orbit-sounding','text-anchor':'middle'},sounding.length?sounding.slice(0,3).join(' · ')+(sounding.length>3?' +'+(sounding.length-3):''):'—');
  node('text',{x:210,y:263,class:'orbit-label','text-anchor':'middle'},'SOUNDING NOW');
  root.replaceChildren(svg);
}
