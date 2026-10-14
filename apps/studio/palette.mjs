import {noteName} from './module.mjs?v=08f7f2e951';
export const offsetLabel=offset=>`${offset>=0?'+':''}${offset}`;
export function paletteShortcut(os){
  return os==='win'?'Windows layout: hold Shift + Alt (the Command-labelled key next to Space if you have Mac keycaps).':os==='mac'?'Mac layout: hold Shift + Option.':'Hold Shift + the keyboard’s Alt/Option key; its keycap depends on the selected hardware layout.';
}
export function paletteVisible(state){
  return !!state?.active && state.type!==0 && !state.child && state.engine===0 && state.visual?.mode==='euclid';
}
export function renderPalette(root,state,ready,onMode,onCaptureMode,onRemove){
  root.hidden=!paletteVisible(state);
  root.closest('.hardware-rhythm')?.classList.toggle('without-palette',root.hidden);
  if(root.hidden)return;
  const helpOpen=root.querySelector('details')?.open||false;
  root.replaceChildren();
  const add=(tag,text,cls)=>{const node=document.createElement(tag);node.textContent=text;if(cls)node.className=cls;root.append(node);return node;};
  add('h3','Live note palette');
  const p=state.palette;
  if(!p){add('p',state.paletteSupported?'Choose a melodic Euclid track to use the note palette.':'Update firmware to see saved palette notes and capture in progress.');return;}

  const current=ready&&state.running&&state.active&&Math.floor(state.step/16)===state.bar?p.slots[state.step%16]:255;
  add('p',!ready?'Live signal unavailable':p.capturing?(p.full?'Palette full · extra notes ignored':p.replace?'Replacing · release to apply':'Adding · release to apply'):p.held&&p.shift?'Ready to capture · tap melodic note keys':!p.saved.length?'Empty palette · add notes to play generated hits':p.pin===null?'Saved palette · cycling across hits':`Saved palette · pinned to slot ${p.pin+1}`,'palette-status');
  function slots(entries,pending){
    const row=add('div','',`palette-slots${pending?' pending':''}`);
    for(let i=0;i<8;i++){
      const item=document.createElement('div'),entry=entries[i];item.className=`palette-slot${!pending&&p.pin===i?' pinned':''}${!pending&&current===i?' current':''}${pending&&i<p.retained?' retained':''}`;
      const index=document.createElement('small');index.textContent=`${i+1}${!pending&&p.pin===i?' / PIN':''}`;
      const note=document.createElement('strong');note.textContent=entry?noteName(entry.note):'—';
      const offset=document.createElement('span');offset.textContent=entry?(p.mode==='transpose'?`${offsetLabel(entry.offset)} st`:''):'';
      item.append(index,note,offset);
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
    for(const [mode,title] of [['palette','Palette notes'],['transpose','Transpose phrase']]){
      const button=document.createElement('button');button.textContent=title;button.setAttribute('aria-pressed',String(p.mode===mode));button.disabled=!ready||p.capturing;button.onclick=()=>onMode?.(mode);modes.append(button);
    }
  }
  if(!p.editable)add('p','Update firmware to choose Palette notes and clear step overrides.','hint');
  if(p.capturing){
    add('p',`Playing: ${p.saved.map(e=>noteName(e.note)).join(' · ')}`,'palette-playing');
    add('h4',`${p.replace?'Replacement':'After adding'} · ${p.capture.length}/8`);slots(p.capture,true);
  }else{add('h4',p.mode==='palette'?'Notes for generated hits':'Transposition palette');slots(p.saved,false);}
  const help=add('details','','palette-help');help.open=helpOpen;
  const summary=document.createElement('summary');summary.textContent='Capture shortcut & how notes are applied';help.append(summary);
  for(const text of [paletteShortcut(state.visual?.os)+(p.removeSupported?' To remove one slot, hold the Euclid button + Delete (without Shift), then tap 1–8. Slots close up after removal.':'')+(p.clearShortcut?' While holding those modifiers, tap Delete to clear the palette and discard pending notes. Recorded step notes remain.':'')+(p.replaceShortcut?' Add Ctrl before the first note to replace for this capture only. The untouched starter note is replaced automatically on your first capture.':'')+(p.captureEditable?' Add notes keeps your palette, appends unique pitches into free slots and preserves a pinned slot. Replace starts a fresh palette and resumes cycling. Release the modifier to apply.':' Tap up to eight melodic notes, then release the modifier to replace the saved palette. Update firmware for Add notes.'),p.mode==='palette'?'Generated hits cycle through these notes. Recorded notes override the palette on their steps. Use palette on a step removes that override; sustained notes are preserved on surrounding steps. Octave controls move the palette register.':'Each slot adds its offset to the recorded or last-picked notes. The first captured note defines zero transposition. Choose Palette notes to play the named pitches instead.']){const paragraph=document.createElement('p');paragraph.textContent=text;help.append(paragraph);}

}

export function paletteRemovePayload(palette,index){
  if(!palette?.removeSupported||!Number.isInteger(index)||index<0||index>=palette.saved.length)throw Error('Invalid palette slot.');
  const payload=new Uint8Array(12);payload.set([3,index,palette.saved.length,palette.root]);
  payload.set(palette.saved.map(entry=>entry.offset&255),4);return payload;
}
