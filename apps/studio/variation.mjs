export const variationOrders=['Original order','Reverse','Rotate','Reshuffle','Random selection'];
export const defaultVariation=()=>({seed:0,order:0,rotate:1,mutation:0,range:7,voicing:0,enabled:false,locked:true});
const integer=(n,a,b)=>Number.isInteger(n)&&n>=a&&n<=b;
export function variationBytes(v){
  if(!integer(v.seed,0,4294967295)||!integer(v.order,0,4)||!integer(v.rotate,0,127)||!integer(v.mutation,0,100)||!integer(v.range,0,24)||!integer(v.voicing,0,4)||typeof v.enabled!=='boolean'||typeof v.locked!=='boolean')throw Error('Invalid variation settings.');
  return Uint8Array.of(v.seed&255,v.seed>>>8&255,v.seed>>>16&255,v.seed>>>24,v.order,v.rotate,v.mutation,v.range,v.voicing,Number(v.enabled),Number(v.locked));
}
function readVariation(b){const v={seed:(b[0]|b[1]<<8|b[2]<<16|b[3]<<24)>>>0,order:b[4],rotate:b[5],mutation:b[6],range:b[7],voicing:b[8],enabled:!!b[9],locked:!!b[10]};if(b[9]>1||b[10]>1)throw Error('Invalid variation flags.');variationBytes(v);return v;}
export function decodeVariation(bytes){
  if(bytes.length!==32||bytes[0]!==1||bytes[1]>11||bytes[4]>1||bytes[27]>1||bytes[28]>1||![0,2,4].includes(bytes[29])||bytes[30]>1||bytes[31]>1)throw Error('Incompatible variation state.');
  return {track:bytes[1],revision:bytes[2]|bytes[3]<<8,pending:!!bytes[4],current:readVariation(bytes.slice(5,16)),next:readVariation(bytes.slice(16,27)),accumulator:{enabled:!!bytes[27],random:!!bytes[28],run:bytes[29],backward:!!bytes[30]},melodic:!!bytes[31]};
}
export function variationPayload(action,scope,nextBar,revision,config){
  if(!integer(action,0,5)||!integer(revision,0,65535)||!integer(scope,0,1))throw Error('Invalid variation action.');
  let extra=[];
  if(action===0)extra=variationBytes(config);
  if(action===5){if(!config||![0,2,4].includes(config.run))throw Error('Invalid accumulator settings.');extra=[Number(config.enabled),Number(config.random),config.run,Number(config.backward)];}
  return Uint8Array.from([action,scope,Number(action===5?false:nextBar),revision&255,revision>>8,...extra]);
}
const views=new WeakMap();
export function renderVariation(root,state,ready,onApply){
  root.hidden=!state?.variation||!state.variation.melodic||state.child;
  if(root.hidden)return;
  let view=views.get(root);
  if(!view){
    view={track:state.track,state,ready,onApply,draft:null,acc:null,revision:null,scope:0,nextBar:false};views.set(root,view);
    root.innerHTML=`<summary>Variation</summary><div class="variation-body"><p class="variation-status" role="status"></p><div class="variation-grid"><label>Apply to<select data-scope><option value="0">This track</option><option value="1">All melodic tracks</option></select></label><label>Apply when<select data-when><option value="0">Immediately</option><option value="1">Next global bar</option></select></label><label>Pitch order<select data-order>${variationOrders.map((n,i)=>`<option value="${i}">${n}</option>`).join('')}</select></label><label>Rotate entries<input data-rotate type="number" min="0" max="127"></label><label>Mutation chance (%)<input data-mutation type="number" min="0" max="100"></label><label><span data-range-label>Mutation range</span><input data-range type="number" min="0" max="24"></label><label>Voicing<select data-voicing><option value="0">As written</option><option value="1">Raise lowest voice</option><option value="2">Raise two voices</option><option value="3">Raise three voices</option><option value="4">Random inversion</option></select></label><label class="variation-check"><input data-enabled type="checkbox"> Enable variation</label></div><div class="variation-actions"><button data-apply>Apply variation</button><button data-reroll>Reroll</button><button data-lock>Lock</button><button data-reset>Reset variation</button><button data-cancel>Cancel queued change</button><button data-discard>Discard draft</button></div><p class="hint" data-help></p><fieldset><legend>Accumulator · one offset per note or chord onset</legend><div class="variation-grid"><label class="variation-check"><input data-acc-enabled type="checkbox"> Enabled</label><label>Traversal<select data-acc-direction><option value="0">Forward</option><option value="1">Backward</option><option value="2">Random</option></select></label><label>Reset after<select data-acc-run><option value="0">Never</option><option value="2">2 onsets</option><option value="4">4 onsets</option></select></label><button data-acc-apply>Apply accumulator now</button></div></fieldset><p class="hint">Original notes and chord qualities stay intact. Reset removes the variation. Save on board keeps the settings; TEMP restores the earlier state.</p></div>`;
    const q=s=>root.querySelector(s),draw=()=>renderVariation(root,view.state,view.ready,view.onApply);
    const begin=()=>{if(!view.draft){view.draft={...view.state.variation.next};view.revision=view.state.variation.revision;}return view.draft;};
    for(const name of ['order','rotate','mutation','range','voicing','enabled'])q(`[data-${name}]`).oninput=e=>{begin()[name]=name==='enabled'?e.target.checked:Number(e.target.value);draw()};
    q('[data-scope]').onchange=e=>{view.scope=Number(e.target.value);draw()};q('[data-when]').onchange=e=>{view.nextBar=!!Number(e.target.value);draw()};
    const send=async(action,config)=>{const snapshot=view.state,revision=view.revision??snapshot.variation.revision;let payload;try{payload=variationPayload(action,view.scope,view.nextBar,revision,config);}catch{q('.variation-status').textContent='Use values within the displayed ranges.';return;}
      if(await view.onApply(snapshot.track,payload)){view.draft=null;view.acc=null;view.revision=null;}draw();};
    q('[data-apply]').onclick=()=>send(0,view.draft??view.state.variation.next);
    for(const [name,action] of [['reroll',1],['lock',2],['reset',3],['cancel',4]])q(`[data-${name}]`).onclick=()=>send(action);
    q('[data-discard]').onclick=()=>{view.draft=null;view.acc=null;view.revision=null;view.force=true;draw()};
    const accChange=()=>{view.revision??=view.state.variation.revision;const direction=Number(q('[data-acc-direction]').value);view.acc={enabled:q('[data-acc-enabled]').checked,backward:direction===1,random:direction===2,run:Number(q('[data-acc-run]').value)};draw();};
    for(const name of ['enabled','direction','run'])q(`[data-acc-${name}]`).onchange=accChange;
    q('[data-acc-apply]').onclick=()=>send(5,view.acc??view.state.variation.accumulator);
  }
  const changedTrack=view.track!==state.track;
  if(changedTrack){view.draft=null;view.acc=null;view.revision=null;view.track=state.track;}
  view.state=state;view.ready=ready;view.onApply=onApply;
  const q=s=>root.querySelector(s),v=state.variation,value=view.draft??v.next,acc=view.acc??v.accumulator;
  const blocked=!ready||!state.active||state.armed||state.transferring,stale=view.revision!==null&&view.revision!==v.revision;
  q('summary').textContent=`Variation · ${v.current.enabled?variationOrders[v.current.order]:'Original'}${v.pending?' · Queued for next bar':''}${v.current.enabled?(v.current.locked?' · Locked':' · Evolving'):''}`;
  q('.variation-status').textContent=stale?'Keyboard settings changed. Discard the draft and retry.':view.draft||view.acc?'Draft · not yet applied':v.pending?'Queued · lands at the next global bar (16 steps), or when transport stops.':v.current.enabled?`Seed ${v.current.seed} · ${v.current.locked?'Repeats each loop':'New variation each track loop'}`:'Original notes are playing.';
  root.querySelectorAll('input,select,button').forEach(n=>n.disabled=blocked);
  for(const name of ['order','rotate','mutation','range','voicing'])if(changedTrack||view.force||document.activeElement!==q(`[data-${name}]`))q(`[data-${name}]`).value=value[name];
  view.force=false;
  q('[data-enabled]').checked=value.enabled;q('[data-scope]').value=view.scope;q('[data-when]').value=Number(view.nextBar);
  q('[data-acc-enabled]').checked=acc.enabled;q('[data-acc-direction]').value=acc.random?2:Number(acc.backward);q('[data-acc-run]').value=acc.run;
  q('[data-lock]').textContent=value.locked?'Unlock / evolve':'Lock this variation';
  for(const name of ['apply','reroll','lock','reset','cancel','acc-apply'])q(`[data-${name}]`).disabled=blocked||stale;
  for(const name of ['reroll','lock','reset','cancel'])q(`[data-${name}]`).disabled||=!!(view.draft||view.acc);
  q('[data-cancel]').hidden=!v.pending;q('[data-discard]').hidden=!(view.draft||view.acc);q('[data-rotate]').disabled=blocked||value.order!==2;
  const scaled=state.scale&&(state.scale.mode===1||(state.scale.mode===0&&state.scale.enabled));
  q('[data-range-label]').textContent=scaled?'Mutation range (scale degrees)':'Palette range (semitones)';
  q('[data-help]').textContent=`Reshuffle uses every entry once; Random selection can repeat entries. ${scaled?'Mutation moves roots by scale degrees.':'Mutation selects existing palette roots within the range.'} Chords move together and keep their interval shape. Reordered recorded chords use the destination’s longest gate. ${view.scope?'These settings will apply to every melodic track.':''}`;
}
