const countHits=mask=>Array.from({length:16},(_,i)=>(mask>>i)&1).reduce((a,b)=>a+b,0);
export function rhythmFields(state){
  const e=state.euclid,ordered=e.orderedKeys;
  const keys=ordered?['Q','W','E','R','T']:['E','Q','W','P','Shift + Q'];
  return [
    {id:0,key:'steps',label:'Steps',max:state.length,min:1,shortcut:keys[0],help:'Generator length. Reducing it also limits fills and wraps rotation.'},
    {id:1,key:'fills',label:'Fills',max:e.steps,min:0,shortcut:keys[1],help:'Number of evenly spaced hits. Zero makes the generator silent.'},
    {id:2,key:'rotation',label:'Rotation',max:e.steps-1,min:0,shortcut:keys[2],help:'Move the rhythm around its cycle.'},
    {id:3,key:'probability',label:'Probability',max:100,min:0,shortcut:keys[3],unit:'%',help:'Chance each eligible hit plays, from 0 to 100 percent.'},
    {id:4,key:'accent',label:'Accent',max:48,min:0,shortcut:keys[4],help:'Alternating velocity boost and reduction, from 0 to 48 MIDI velocity points.'},
    {id:5,key:'enabled',label:'Rhythm',shortcut:null,help:'Enable the generator, or return to recorded timing. Turning it on keeps your chosen fills.'}
  ];
}
export function rhythmEditPayload(field,value,expected){
  if(!Number.isInteger(value)||!Number.isInteger(expected)||value<0||value>65535||expected<0||expected>65535)throw Error('Enter a whole number within the displayed range.');
  return Uint8Array.of(field,value&255,value>>8,expected&255,expected>>8);
}
export function renderRhythmControls(root,state,ready,onEdit){
  const e=state?.euclid;
  if(!e?.euclidean){root.replaceChildren();delete root.dataset.context;root.textContent=e?'Recorded timing · no Euclidean generator selected.':'Update firmware for live rhythm controls.';return;}
  const context=`${state.track}:${e.lane??"track"}:${Number(state.child)}:${Number(e.editable)}:${Number(e.orderedKeys)}:${state.engine}`;
  const fields=rhythmFields(state);
  if(root.dataset.context!==context){
    root.replaceChildren();root.dataset.context=context;
    const controls=document.createElement('div');controls.className='rhythm-edit-row';root.append(controls);
    for(const field of fields){
      const wrap=document.createElement('label');wrap.className='rhythm-field';wrap.dataset.field=field.key;wrap.title=field.help;
      const title=document.createElement('span');title.className='rhythm-field-title';title.append(field.label);
      if(field.shortcut&&state.engine===0){const key=document.createElement('kbd');key.textContent=field.shortcut;title.append(key);}
      const value=document.createElement('div');value.className='rhythm-field-value';
      const input=document.createElement(field.key==='enabled'?'select':'input');input.setAttribute('aria-label',`Live ${field.label.toLowerCase()}`);
      if(field.key==='enabled')for(const [v,text] of [[1,'Enabled'],[0,'Disabled']]){const option=document.createElement('option');option.value=v;option.textContent=text;input.append(option);}
      else{input.type='number';input.step='1';input.inputMode='numeric';}
      input.onfocus=()=>{input.dataset.expected=input.dataset.latest;};
      input.onkeydown=event=>{if(event.key==='Enter'){event.preventDefault();input.onchange();input.blur();}if(event.key==='Escape'){event.preventDefault();input.value=input.dataset.latest;input.dataset.expected=input.dataset.latest;input.blur();}};
      input.onchange=()=>{
        if(!input.isConnected||input.disabled)return;
        if(input.value.trim()===''||!input.checkValidity()){input.reportValidity();input.value=input.dataset.latest;return;}
        const next=Number(input.value),expected=Number(input.dataset.expected??input.dataset.latest);
        if(next!==expected){input.dataset.expected=String(next);onEdit(state.track,e.lane!=null?2+2*e.lane+Number(state.child):Number(state.child),field.id,next,expected);}
      };
      value.append(input);if(field.unit){const unit=document.createElement('span');unit.textContent=field.unit;value.append(unit);}
      wrap.append(title,value);controls.append(wrap);
    }
    const results=document.createElement('div');results.className='rhythm-results';
    for(const [key,label] of [['pulses','Pulses / view'],['eligible','Eligible hits / view']]){const metric=document.createElement('span');metric.append(label+' ');const number=document.createElement('strong');number.dataset.result=key;metric.append(number);results.append(metric);}
    root.append(results);const help=document.createElement('p');help.className='rhythm-live-help';root.append(help);
  }
  const blocked=!ready||!state.active||state.armed||state.transferring||!!state.palette?.capturing||['drum-edit','melodic-edit','confirmation','navigation','cc','chord'].includes(state.visual?.mode);
  for(const field of fields){
    const input=root.querySelector(`[data-field="${field.key}"] input,[data-field="${field.key}"] select`);
    input.disabled=blocked||!e.editable;
    input.dataset.latest=Number(e[field.key]);
    if(field.key!=='enabled'){input.min=field.min;input.max=field.max;}
    if(document.activeElement!==input){input.value=Number(e[field.key]);input.dataset.expected=Number(e[field.key]);}
  }
  root.querySelector('[data-result="pulses"]').textContent=ready?countHits(e.generated):'—';
  root.querySelector('[data-result="eligible"]').textContent=ready?countHits(state.pattern):'—';
  root.querySelector('.rhythm-live-help').textContent=!e.editable?'Update firmware to edit here. Shortcuts shown match the connected firmware.':`${e.lane!=null?`Drum lane ${e.lane+1} · independent rhythm. `:''}${state.engine===0?'Euclid controls: select a key, then turn the knob. ':''}Edit a value and press Enter or leave the field to apply. ${e.template?'Captured template · changing steps or fills restores even spacing.':'Euclidean spacing.'} Counts are calculated before probability and mutes.`;
}
