import {chordLabel} from './chord-mode.mjs?v=886590c057';
import {noteName} from './module.mjs?v=08f7f2e951';

export function liveBarLabel(state){
  const start=state.bar*16+1,end=Math.min((state.bar+1)*16,state.length),playBar=Math.floor(state.step/16);
  return `VIEW BAR ${state.bar+1} / ${Math.ceil(state.length/16)} · STEPS ${start}–${end}`+
    (state.active&&state.running&&playBar!==state.bar?` · Playing bar ${playBar+1} · Space + = to follow`:'');
}

export function renderLiveSteps(root,state,ready,onUsePalette){
  const melodic=state.type!==0&&!state.child;
  root.classList.toggle('melodic-steps',melodic);
  root.replaceChildren(...Array.from({length:16},(_,i)=>{
    const step=state.bar*16+i, outside=step>=state.length;
    const pitches=melodic&&!outside?(state.projectedNotes??state.stepNotes)?.[i]:null;
    const names=pitches?.map(noteName);
    const hit=!!(state.pattern&(1<<i)),playhead=ready&&state.active&&state.running&&state.step===step;
    const node=document.createElement('div');
    node.className=`live-step${hit?' hit':''}${playhead?' playhead':''}${outside?' outside':''}${pitches?.length?' has-notes':''}`;
    const number=document.createElement('small');number.textContent=String(step+1).padStart(2,'0');node.append(number);
    if(melodic){
      const label=document.createElement('strong');
      const chord=state.chordMode?.enabled?state.chordMode.steps[i]:null;
      label.textContent=chord?`${chord.onset?'':'↳ '}${chordLabel(chord)}`:outside?'—':names?.length?names.join(' · '):names?'—':'?';
      node.append(label);
    }
    const slot=state.palette?.slots[i],entry=slot!==undefined&&slot!==255?state.palette.saved[slot]:null;
    const override=state.euclid?.euclidean&&state.euclid.enabled&&state.palette?.editable&&state.palette.mode==='palette'&&!!(state.palette.overrides&(1<<i));
    if(melodic&&!outside&&override){const badge=document.createElement('span');badge.className='step-override';badge.textContent='Recorded override';node.append(badge);const button=document.createElement('button');button.textContent='Use palette';button.setAttribute('aria-label',`Use palette on step ${step+1}`);button.disabled=!ready||state.palette.capturing;button.onclick=()=>onUsePalette?.(step);node.append(button);}
    if(melodic&&!outside&&entry){const badge=document.createElement('span');badge.className='step-palette-slot';badge.textContent=`P${slot+1}${state.palette.mode==='palette'?'':` · ${entry.offset>=0?'+':''}${entry.offset}`}`;badge.title=`Palette slot ${slot+1}, offset ${entry.offset} semitones`;node.append(badge);}
    const description=`Step ${step+1}${outside?', outside loop':melodic?`, ${state.projectedNotes?'projected':'stored'} notes: ${names?names.join(', ')||'none':'unavailable; firmware update needed'}`:hit?', note present':''}${playhead?', playhead':''}`;
    node.setAttribute('aria-label',description);node.title=description;
    return node;
  }));
  if(state.chordMode?.enabled)return 'Chord onsets and sustain tails · hold any part of a chord to edit it as a group.';
  return melodic?(state.projectedNotes?'Projected notes before probability, mutes and accumulator. Empty steps are rests.':state.stepNotes?'Stored notes · sustained notes appear on each step they span. Euclid may alter playback.':'Update keyboard firmware to see the notes stored on each step.'):'Hardware step view';
}
