export const scaleNames=['Ionian','Dorian','Phrygian','Lydian','Mixolydian','Aeolian','Locrian','Harmonic minor','Harmonic major','Major pentatonic','Minor pentatonic'];
export const scaleMasks=[0xAB5,0x6AD,0x5AB,0xAD5,0x6B5,0x5AD,0x56B,0x9AD,0x9B5,0x295,0x4A9];
export const rootNames=['C','C♯','D','D♯','E','F','F♯','G','G♯','A','A♯','B'];
export const rootKeys=['Q','2','W','3','E','R','5','T','6','Y','7','U'];
export const modeKeys=['A','S','D','F','G','H','J','K','L',';',"'"];
export const scaleNotes=(root,kind)=>Array.from({length:12},(_,n)=>(n+root)%12).filter(n=>scaleMasks[kind]&(1<<((n+12-root)%12)));
const editors=new WeakMap();
export function renderScale(root,state,ready,onApply,onAudition){
  root.hidden=!state?.scale;if(root.hidden)return;
  let view=editors.get(root);
  if(!view){
    root.innerHTML=`<summary></summary><div class="scale-body"><div class="scale-heading"><strong>Global key & scale</strong><label><input type="checkbox" data-enable> Enable global scale</label><span>Ctrl + Space</span></div><div class="scale-root-row" aria-label="Root note"></div><div class="scale-mode-row" aria-label="Scale mode"></div><p class="scale-notes"></p><div class="scale-actions"><button data-global>Apply global</button><button data-cancel>Cancel changes</button><button data-audition>Audition scale</button></div><div class="scale-track"><label>This track<select data-follow><option value="0">Follow global</option><option value="1">Local key / scale</option><option value="2">Unconstrained</option></select></label><label>Local root<select data-root></select></label><label>Local scale<select data-kind></select></label><label>Snap direction<select data-direction><option value="0">Nearest (ties down)</option><option value="1">Down</option><option value="2">Up</option></select></label><button data-track>Apply track</button></div><p class="scale-local-notes"></p><p class="scale-hint"></p></div>`;
    view={state,ready,g:null,t:null};editors.set(root,view);
    const begin=()=>{if(!view.g){const s=view.state.scale;view.g={enabled:s.enabled,root:s.root,kind:s.kind,revision:s.revision};}return view.g;};
    const draw=()=>renderScale(root,view.state,view.ready,onApply,onAudition);
    for(let i=0;i<12;i++){const button=document.createElement('button');button.innerHTML=`<kbd>${rootKeys[i]}</kbd><strong>${rootNames[i]}</strong>`;button.setAttribute('aria-label',`Root ${rootNames[i]}`);button.onclick=()=>{Object.assign(begin(),{root:i,enabled:true});draw()};root.querySelector('.scale-root-row').append(button);}
    for(let i=0;i<scaleNames.length;i++){const button=document.createElement('button');button.innerHTML=`<kbd>${modeKeys[i]}</kbd><span>${scaleNames[i]}</span>`;button.onclick=()=>{Object.assign(begin(),{kind:i,enabled:true});draw()};root.querySelector('.scale-mode-row').append(button);}
    const populate=(selector,names)=>names.forEach((name,i)=>{const option=document.createElement('option');option.value=i;option.textContent=name;root.querySelector(selector).append(option)});
    populate('[data-root]',rootNames);populate('[data-kind]',scaleNames);
    root.querySelector('[data-enable]').onchange=e=>{begin().enabled=e.target.checked;draw()};
    root.querySelector('[data-cancel]').onclick=()=>{view.g=null;view.t=null;draw()};
    root.querySelector('[data-global]').onclick=async()=>{const g=begin();if(await onApply(0,0,Uint8Array.of(0,Number(g.enabled),g.root,g.kind,g.revision&255,g.revision>>8))){view.g=null;draw()}};
    for(const selector of ['follow','root','kind','direction'])root.querySelector(`[data-${selector}]`).onchange=()=>{const s=view.state.scale;view.t={mode:Number(root.querySelector('[data-follow]').value),root:Number(root.querySelector('[data-root]').value),kind:Number(root.querySelector('[data-kind]').value),direction:Number(root.querySelector('[data-direction]').value),revision:view.t?.revision??s.revision};draw()};
    root.querySelector('[data-track]').onclick=async()=>{const t=view.t;if(!t)return;if(await onApply(view.state.track,Number(view.state.child),Uint8Array.of(1,t.mode,t.root,t.kind,t.direction,t.revision&255,t.revision>>8))){view.t=null;draw()}};
    root.querySelector('[data-audition]').onclick=()=>{const g=view.state.scale.editing?view.state.scale.pending:view.g??view.state.scale;onAudition?.(scaleNotes(g.root,g.kind).map(n=>60+n+(n<g.root?12:0)))};
  }
  if(view.state.track!==state.track){view.t=null;}
  view.state=state;view.ready=ready;
  const s=state.scale,g=s.editing?s.pending:view.g??s,t=view.t??{mode:s.mode,root:s.localRoot,kind:s.localKind,direction:s.direction};
  const blocked=!ready||!state.active||state.armed||s.editing;
  root.dataset.editing=String(s.editing);
  if(s.editing)root.open=true;
  root.querySelector('summary').textContent=`Scale · ${s.enabled?`${rootNames[s.root]} ${scaleNames[s.kind]}`:'Global off'} · ${['Follow global','Local','Unconstrained'][s.mode]}${s.editing?' · Keyboard selecting…':''}`;
  root.querySelectorAll('input,select,button').forEach(n=>n.disabled=blocked);
  root.querySelector('[data-enable]').checked=g.enabled;
  root.querySelectorAll('.scale-root-row button').forEach((b,i)=>{b.classList.toggle('in-scale',scaleNotes(g.root,g.kind).includes(i));b.classList.toggle('root-note',i===g.root);b.setAttribute('aria-pressed',String(i===g.root))});
  root.querySelectorAll('.scale-mode-row button').forEach((b,i)=>{b.setAttribute('aria-pressed',String(i===g.kind));b.disabled=blocked||i>=(s.count??7);b.title=i>=(s.count??7)?'Update firmware to use this scale':''});
  root.querySelectorAll('[data-kind] option').forEach((o,i)=>o.disabled=i>=(s.count??7));
  for(const [key,value] of [['follow',t.mode],['root',t.root],['kind',t.kind],['direction',t.direction]])root.querySelector(`[data-${key}]`).value=value;
  root.querySelector('[data-root]').disabled=blocked||t.mode!==1;root.querySelector('[data-kind]').disabled=blocked||t.mode!==1;
  root.querySelector('[data-global]').disabled=blocked||g.kind>=(s.count??7);
  root.querySelector('[data-track]').disabled=blocked||t.kind>=(s.count??7)||!view.t||state.type===0||state.child||state.engine===1;
  root.querySelector('[data-audition]').disabled=!ready;
  root.querySelector('.scale-notes').textContent=`${rootNames[g.root]} ${scaleNames[g.kind]} · ${scaleNotes(g.root,g.kind).map(n=>rootNames[n]).join(' · ')}${s.editing?' · Enter applies / Esc cancels':view.g?' · Not applied yet':''}`;
  root.querySelector('.scale-local-notes').textContent=t.mode===1?`Local · ${rootNames[t.root]} ${scaleNames[t.kind]} · ${scaleNotes(t.root,t.kind).map(n=>rootNames[n]).join(' · ')}${view.t?' · Not applied yet':''}`:'';
  root.querySelector('.scale-hint').textContent=state.type===0||state.child||state.engine===1?'Drums and chord-bank voicings bypass quantization.':'Stored notes stay unchanged. Quantization follows the accumulator. Save the project to keep these settings. Changing scale releases affected sounding notes. Audition uses the browser sound setting.';
}

const scaleModals=new WeakMap();
export function renderScaleModal(dialog,state,ready,onApply,onAudition){
  let view=scaleModals.get(dialog);
  if(!view){
    view={editing:false,dismissed:false};scaleModals.set(dialog,view);
    dialog.innerHTML=`<div class="scale-modal-header"><div><div class="eyebrow">GLOBAL KEY & SCALE · LIVE KEYBOARD</div><h2 id="scale-dialog-title">Choose your scale.</h2></div><button data-hide autofocus>Hide</button></div><p class="scale-modal-instructions">Choose a root with the note keys and a scale with A–J, K, L, ; or '. Press Enter on the keyboard to apply, or Esc to cancel.</p><details class="scale-modal-editor" open></details><p class="scale-modal-footer">Hide dismisses this window; your keyboard stays in Scale mode.</p>`;
    dialog.querySelector('[data-hide]').onclick=()=>{view.dismissed=true;dialog.close()};
    dialog.addEventListener('cancel',()=>{view.dismissed=true});
  }
  const editing=!!(state?.active&&state.scale?.editing);
  if(!editing){view.dismissed=false;view.editing=false;if(dialog.open)dialog.close();return;}
  if(!view.editing)view.dismissed=false;
  view.editing=true;
  renderScale(dialog.querySelector('details'),state,ready,onApply,onAudition);
  dialog.querySelector('.scale-modal-instructions').textContent=ready?'Choose a root with the note keys and a scale with A–J, K, L, ; or \'. Press Enter on the keyboard to apply, or Esc to cancel.':'Keyboard connection interrupted. Waiting for live scale settings…';
  // Keep polling while open, without stealing focus again on each snapshot.
  if(ready&&!view.dismissed&&!dialog.open&&!dialog.ownerDocument.querySelector('dialog[open]'))dialog.showModal();
}
