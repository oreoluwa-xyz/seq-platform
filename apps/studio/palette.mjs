import {syncPaletteSelection,paintPaletteSelection} from './palette-links.mjs?v=b388aa571a';
import {paletteTypes,paletteTypeKeys,paletteTypeNames,paletteScaleEnabled,paletteChord} from './palette-chords.mjs?v=b0a37ac77a';
import {noteName} from './module.mjs?v=8c4bcd2da4';
export const offsetLabel=offset=>`${offset>=0?'+':''}${offset}`;
export function paletteShortcut(os){
  return os==='win'?'Windows layout: hold Shift + Alt (the Command-labelled key next to Space if you have Mac keycaps).':os==='mac'?'Mac layout: hold Shift + Option.':'Hold Shift + the keyboard’s Alt/Option key; its keycap depends on the selected hardware layout.';
}
export function paletteVisible(state){
  return !!state?.active && state.type!==0 && !state.child && state.engine===0 && state.visual?.mode==='euclid';
}
export function renderPalette(root,state,ready,onMode,onCaptureMode,onRemove,onQuality){
  root.hidden=!paletteVisible(state);
  root.closest('.hardware-rhythm')?.classList.toggle('without-palette',root.hidden);
  if(root.hidden){paintPaletteSelection(root);return;}
  const focusedSlot=root.contains(document.activeElement)?document.activeElement.dataset.paletteSelect:undefined;
  const selection=syncPaletteSelection(root,state);
  const select=index=>{selection.index=index;root.dataset.chordSlot=String(index);root.dataset.chordTrack=String(state.track);paintPaletteSelection(root);};
  const p=state.palette;
  const current=ready&&state.running&&state.active&&Math.floor(state.step/16)===state.bar?p?.slots[state.step%16]:255;
  // Keep controls mounted between snapshots so a poll cannot interrupt a click.
  const renderKey=JSON.stringify([state.track,{...p,slots:undefined},state.scale,state.visual?.os,ready,selection.index,root.dataset.chordSlot,root.dataset.chordTrack]);
  if(p&&root.paletteRenderKey===renderKey){
    root.querySelectorAll('.palette-slot').forEach((node,i)=>node.classList.toggle('current',!p.capturing&&current===i));
    paintPaletteSelection(root);return;
  }
  const helpOpen=root.querySelector('details')?.open||false;
  root.replaceChildren();
  root.classList.toggle('chord-palette',state.palette?.mode==='chords');
  const add=(tag,text,cls)=>{const node=document.createElement(tag);node.textContent=text;if(cls)node.className=cls;root.append(node);return node;};
  add('h3',state.palette?.mode==='chords'?'Live chord palette':'Live note palette');
  if(!p){root.paletteRenderKey=null;add('p',state.paletteSupported?'Choose a melodic Euclid track to use the note palette.':'Update firmware to see saved palette notes and capture in progress.');return;}

  add('p',!ready?'Live signal unavailable':p.capturing?(p.full?'Palette full · extra notes ignored':p.replace?'Replacing · release to apply':'Adding · release to apply'):p.held&&p.shift?'Ready to capture · tap melodic note keys':!p.saved.length?'Empty palette · add notes to play generated hits':p.pin===null?'Saved palette · cycling across hits':`Saved palette · pinned to slot ${p.pin+1}`,'palette-status');
  const heading=document.createElement('div');heading.className='palette-heading';
  root.prepend(heading);heading.append(root.querySelector('h3'),root.querySelector('.palette-status'));
  function slots(entries,pending){
    const row=add('div','',`palette-slots${pending?' pending':''}`);
    for(let i=0;i<8;i++){
      const item=document.createElement('div'),entry=entries[i];item.className=`palette-slot${!pending&&p.pin===i?' pinned':''}${!pending&&current===i?' current':''}${pending&&i<p.retained?' retained':''}${!pending&&p.mode==='chords'&&p.editDigit===i+1?' editing':''}`;
      const index=document.createElement('small');index.textContent=`${i+1}${!pending&&p.pin===i?' / PIN':''}`;
      const note=document.createElement('strong');note.textContent=entry?noteName(entry.note):'—';
      const offset=document.createElement('span');offset.textContent=entry?(p.mode==='transpose'?`${offsetLabel(entry.offset)} st`:''):'';
      if(entry&&!pending){
        const button=document.createElement('button');button.type='button';button.className='palette-select';button.dataset.paletteSelect=String(i);
        button.setAttribute('aria-label',`Show hits for palette ${i+1}, ${noteName(entry.note)}`);button.title='Highlight the Euclidean hits using this entry';
        button.append(index,note,offset);button.onclick=()=>{select(i);renderPalette(root,state,ready,onMode,onCaptureMode,onRemove,onQuality);};item.append(button);
      }else item.append(index,note,offset);
      if(entry&&!pending&&p.mode==='chords'&&p.qualities){
        const chord=paletteChord(entry,p.qualities[i],state.scale);offset.textContent=chord.label;
        item.querySelector('.palette-select').setAttribute('aria-label',`Show hits for palette ${i+1}, ${noteName(entry.note)}, ${chord.label}`);
        const edit=document.createElement('button');edit.textContent='Edit chord';edit.className='palette-edit-chord';edit.disabled=!ready||p.capturing||p.editDigit!==null&&p.editDigit!==undefined;
        edit.onclick=()=>{select(i);renderPalette(root,state,ready,onMode,onCaptureMode,onRemove,onQuality);
          const heading=root.querySelector('.palette-quality-title');
          if(heading&&document.body.classList.contains('desktop-performance'))root.scrollTop+=heading.getBoundingClientRect().top-root.getBoundingClientRect().top;
        };
        item.append(edit);
      }
      if(entry&&!pending&&p.removeSupported){
        const remove=document.createElement('button');remove.textContent='Remove';remove.className='palette-remove';
        remove.setAttribute('aria-label',`Remove ${noteName(entry.note)} from palette slot ${i+1}`);
        remove.disabled=!ready||p.capturing;remove.onclick=()=>onRemove?.(i);item.append(remove);
      }
      row.append(item);
    }
  }
  if(p.captureEditable){
    const capture=add('div','','palette-capture-mode');capture.setAttribute('aria-label','Palette capture behavior');
    const label=document.createElement('small');label.textContent='CAPTURE';capture.append(label);
    for(const [replace,title] of [[false,'Add notes'],[true,'Replace']]){
      const button=document.createElement('button');button.textContent=title;button.setAttribute('aria-pressed',String(p.replace===replace));button.disabled=!ready||p.capturing;button.onclick=()=>onCaptureMode?.(replace);capture.append(button);
    }
    add('p',p.starter&&!p.capturing?'Starter note · first capture replaces it.':p.replace?'Next capture replaces the saved palette.':`${8-(p.capturing?p.capture.length:p.saved.length)} free slots · duplicates ignored`,'palette-modifiers');
  }else add('p',ready?`Capture modifier: ${p.held?'held':'released'} · Shift: ${p.shift?'held':'released'}`:'Modifier state unavailable','palette-modifiers');
  if(p.editable){
    const modes=add('div','','palette-modes');modes.setAttribute('aria-label','Live pitch mode');
    for(const [mode,title] of [['palette','Palette notes'],['transpose','Transpose phrase'],...(p.chordSupported?[['chords','Chord palette']]:[])]){
      const button=document.createElement('button');button.textContent=title;button.setAttribute('aria-pressed',String(p.mode===mode));button.disabled=!ready||p.capturing;button.onclick=()=>onMode?.(mode);modes.append(button);
    }
  }
  const captureControls=root.querySelector('.palette-capture-mode'),pitchModes=root.querySelector('.palette-modes');
  if(captureControls&&pitchModes){
    const controls=document.createElement('div');controls.className='palette-controls';
    root.insertBefore(controls,captureControls);controls.append(pitchModes,captureControls);
  }
  if(!p.editable)add('p','Update firmware to choose Palette notes and clear step overrides.','hint');
  if(p.capturing){
    add('p',`Playing: ${p.saved.map(e=>noteName(e.note)).join(' · ')}`,'palette-playing');
    add('h4',`${p.replace?'Replacement':'After adding'} · ${p.capture.length}/8`,'palette-slots-title');slots(p.capture,true);
  }else{add('h4',p.mode==='chords'?'Chords for generated hits':p.mode==='palette'?'Notes for generated hits':'Transposition palette','palette-slots-title');slots(p.saved,false);}
  root.append(root.querySelector('.palette-modifiers'));
  if(p.mode==='chords'&&p.qualities&&!p.capturing){
    const index=p.editDigit!==null&&p.editDigit!==undefined?(p.editDigit===0?8:p.editDigit-1):root.dataset.chordTrack===String(state.track)?Number(root.dataset.chordSlot??0):0;
    const slot=index===8?0:index,entry=p.saved[slot];
    if(entry){
      const chord=paletteChord(entry,p.qualities[slot],state.scale),mixed=index===8&&p.qualities.slice(0,p.saved.length).some(t=>t!==p.qualities[0]),title=index===8?'All palette slots':`Palette ${slot+1}`;
      add('h4',`${title} · ${mixed?'Mixed qualities':`${noteName(chord.root)} · ${chord.label}`}`,'palette-quality-title');
      add('p',index===8?'Preview of first slot: '+chord.notes.map(noteName).join(' – '):chord.notes.length?chord.notes.map(noteName).join(' – '):'Chord exceeds the MIDI note range. Lower the palette octave.','scale-notes');
      const choices=add('div','','scale-mode-row palette-quality-row');
      paletteTypes.forEach((type,i)=>{
        const button=document.createElement('button'),key=document.createElement('kbd'),label=document.createElement('span');
        key.textContent=paletteTypeKeys[i];label.textContent=paletteTypeNames[type];button.append(key,label);
        button.setAttribute('aria-pressed',String(!mixed&&p.qualities[slot]===type));
        button.disabled=!ready||p.editDigit!==null&&p.editDigit!==undefined||(type===10||type===11)&&!paletteScaleEnabled(state.scale);
        button.onclick=()=>{select(index);onQuality?.(index,type);};choices.append(button);
      });
      const all=add('button',index===8?'Editing all slots':'Edit all slots');all.disabled=!ready||p.editDigit!==null&&p.editDigit!==undefined;
      all.onclick=()=>{select(8);renderPalette(root,state,ready,onMode,onCaptureMode,onRemove,onQuality)};
    }
    add('p','Hold 1–8 + knob to change one chord; hold 0 + knob for all. Tap 1–8 to pin, tap 0 to cycle. While holding a slot, home-row keys choose a type; Z/X choose Auto triad/seventh. Orange = root, green = chord tones, violet = quality. Changes play on the next hit.','hint');
  }
  const help=add('details','','palette-help');help.open=helpOpen;
  const summary=document.createElement('summary');summary.textContent='Capture shortcut & how notes are applied';help.append(summary);
  for(const text of [paletteShortcut(state.visual?.os)+(p.removeSupported?' To remove one slot, hold the Euclid button + Delete (without Shift), then tap 1–8. Slots close up after removal.':'')+(p.clearShortcut?' While holding those modifiers, tap Delete to clear the palette and discard pending notes. Recorded step notes remain.':'')+(p.replaceShortcut?' Add Ctrl before the first note to replace for this capture only. The untouched starter note is replaced automatically on your first capture.':'')+(p.captureEditable?' Add notes keeps your palette, appends unique pitches into free slots and preserves a pinned slot. Replace starts a fresh palette and resumes cycling. Release the modifier to apply.':' Tap up to eight melodic notes, then release the modifier to replace the saved palette. Update firmware for Add notes.'),p.mode==='chords'?'Each generated hit plays a chord from the palette. Recorded notes override it on their steps. Fixed qualities keep their intervals even with scale on. Auto follows the track’s global/local scale; with scale off, an existing Auto entry plays its root only. Hold the Euclid button and tap C to toggle chord palette.':p.mode==='palette'?'Generated hits cycle through these notes. Recorded notes override the palette on their steps. Use palette on a step removes that override; sustained notes are preserved on surrounding steps. Octave controls move the palette register.':'Each slot adds its offset to the recorded or last-picked notes. The first captured note defines zero transposition. Choose Palette notes to play the named pitches instead.']){const paragraph=document.createElement('p');paragraph.textContent=text;help.append(paragraph);}

  root.paletteRenderKey=renderKey;
  paintPaletteSelection(root);
  if(focusedSlot!==undefined)root.querySelector(`[data-palette-select="${focusedSlot}"]`)?.focus({preventScroll:true});
}

export function paletteRemovePayload(palette,index){
  if(!palette?.removeSupported||!Number.isInteger(index)||index<0||index>=palette.saved.length)throw Error('Invalid palette slot.');
  const payload=new Uint8Array(12);payload.set([3,index,palette.saved.length,palette.root]);
  payload.set(palette.saved.map(entry=>entry.offset&255),4);return payload;
}
