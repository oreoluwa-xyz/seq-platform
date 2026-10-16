/* Use the firmware's source-slot mapping: equal pitches can belong to different
 * entries, and recorded overrides do not consume the palette in note/chord mode. */
export function paletteSlotForStep(state,index){
  const p=state?.palette,slot=p?.slots?.[index],step=(state?.bar??0)*16+index;
  const length=state?.loopSteps?Math.min(state.loopSteps,state.length):state?.length;
  if(!state?.active||state.type===0||state.child||state.engine!==0||!state.euclid?.euclidean||!state.euclid.enabled||index<0||index>=16||step>=length)return null;
  if(!((state.euclid.generated??state.pattern)&(1<<index))||!Number.isInteger(slot)||!p.saved[slot])return null;
  if(p.mode!=='transpose'&&(p.overrides&(1<<index)))return null;
  return slot;
}

export function syncPaletteSelection(root,state){
  const p=state.palette,identity=JSON.stringify([p?.root,p?.saved?.map(e=>e.offset)]);
  let selection=root.paletteSelection;
  if(!selection||selection.track!==state.track||selection.identity!==identity){
    selection=root.paletteSelection={track:state.track,identity,index:null,pin:null,editDigit:null};
    delete root.dataset.chordSlot;delete root.dataset.chordTrack;
  }
  if(p?.editDigit!=null&&p.editDigit!==selection.editDigit)selection.index=p.editDigit===0?8:p.editDigit-1;
  else if(p?.pin!==selection.pin)selection.index=p?.pin??null;
  selection.pin=p?.pin??null;selection.editDigit=p?.editDigit??null;
  if(selection.index!=null){root.dataset.chordSlot=String(selection.index);root.dataset.chordTrack=String(state.track);}
  selection.state=state;
  return selection;
}

export function paintPaletteSelection(root,steps=root?.ownerDocument.querySelector('#live-steps')){
  const selection=root?.paletteSelection,state=selection?.state;
  const index=root&&!root.hidden&&!state?.palette?.capturing?selection?.index:null;
  let count=0;
  for(const node of steps?.children??[]){
    const slot=node.dataset.paletteSlot;
    const selected=index!=null&&slot!==undefined&&(index===8||Number(slot)===index)&&steps.paletteTrack===selection.track;
    node.classList.toggle('palette-match',selected);if(selected)count++;
  }
  for(const button of root?.querySelectorAll('[data-palette-select]')??[]){
    const selected=index!=null&&(index===8||Number(button.dataset.paletteSelect)===index);
    button.setAttribute('aria-pressed',String(selected));button.closest('.palette-slot').classList.toggle('selected',selected);
  }
  const status=root?.ownerDocument.querySelector('#palette-selection-status');
  if(status){
    const text=state?.palette?.capturing?'Capturing · release to apply':index==null?'Select a palette entry to locate its hits':`${index===8?'All slots':`P${index+1}`} · ${count} ${count===1?'hit':'hits'} in this bar${state?.variation?.current?.enabled?' · before variation':''}`;
    if(status.textContent!==text)status.textContent=text;
  }
}

/* Compare frozen values, not the mutable live snapshot. Release and playhead
 * updates must not reopen a pane the user deliberately switched away from. */
export function paletteNavigationSnapshot(state){
  const p=state?.palette;
  return {track:state?.track,gesture:p?.editDigit!=null?`chord-${p.editDigit}`:p?.capturing?'capture':p?.held&&p?.shift?'capture-ready':'',
    content:p?JSON.stringify([p.mode,p.replace,p.pin,p.root,p.saved?.map(e=>e.offset),p.qualities]):null};
}
export function paletteWantsView(previous,next){
  const sameTrack=!!previous&&previous.track===next.track;
  return !!next.gesture&&(!sameTrack||previous.gesture!==next.gesture)||!!(sameTrack&&previous.content!=null&&next.content!=null&&previous.content!==next.content);
}
