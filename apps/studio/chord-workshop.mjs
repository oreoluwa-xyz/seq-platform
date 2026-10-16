import {decodeChordProject,encodeChordProject,recipeProgression,recipeNotes,conformRecipesToScale,quantizeRecipesToScale,customRecipe,adoptionCandidates,adoptChord,sameScale,setChordScaleFree,quantizeNote} from './chord-project.mjs?v=3aa3001b5b';
import {deriveNotes,encodeChordTrack,notePatterns} from './harmony.mjs?v=5bc5306d12';
import {chordTypes,chordLabel,chordVoices,inversionNames,scaleChordTypes} from './chord-mode.mjs?v=e16e3e7465';
import {noteName} from './module.mjs?v=8c4bcd2da4';
import {rootNames,scaleNames,scaleNotes} from './scale.mjs?v=bcc8228f15';
export function setupChordWorkshop(root,{readTrack,install,audition,getGlobalScale=()=>null}){
  let source=null,recipes=[],track=0,available=false,canInvert=false,canCustom=false,canBound=false,working=false,result=null,bytes=null,scalePreview=null,appliedScale=null;
  root.innerHTML=`<p class="hint">Read the selected track’s saved chord recipes. Roots, types, inversions, timing, velocity and scale context stay explicit. Other recorded notes and CC automation are preserved when writing chords back.</p><button data-read>Read chord track</button><p data-source></p><section data-adoption class="chord-scale-action"></section><section class="chord-scale-action"><button data-conform>Apply global scale to all chords…</button><button data-quantize>Quantize all voices to global scale…</button><p class="hint" data-global-context></p><div data-scale-preview hidden><h3 data-scale-title></h3><p class="hint" data-scale-description></p><div data-scale-changes></div><button data-scale-audition>Audition scale preview</button><button data-scale-apply>Use these chords</button><button data-scale-cancel>Cancel preview</button></div></section><div data-entries></div><button data-add>Add chord +</button><div class="harmony-options"><label>Output<select data-pattern></select></label><label>Note every<select data-stride><option value="1">1 step</option><option value="2" selected>2 steps</option><option value="4">4 steps</option><option value="8">8 steps</option><option value="16">16 steps</option></select></label><label>Destination<select data-target></select></label><label>MIDI channel<select data-channel></select></label></div><p data-summary role="status"></p><button data-preview>Audition chord progression</button><label class="harmony-confirm"><input type="checkbox" data-confirm><span data-confirm-label></span></label><div class="dialog-actions"><button data-write class="primary">Back up & write track</button></div><p data-error role="alert"></p>`;
  const q=s=>root.querySelector(s),options=(el,items)=>{el.replaceChildren(...items.map(([v,n])=>{const o=document.createElement('option');o.value=v;o.textContent=n;return o}))};
  options(q('[data-pattern]'),notePatterns.map(p=>[p.id,p.label]));options(q('[data-target]'),Array.from({length:12},(_,i)=>[i,`Track ${i+1}`]));options(q('[data-channel]'),Array.from({length:16},(_,i)=>[i,i+1]));
  const fail=e=>q('[data-error]').textContent=e?.message??'';
  function renderAdoption(){
    const container=q('[data-adoption]');container.replaceChildren();container.hidden=!canCustom||!source?.foreign.length;if(container.hidden)return;
    const intro=document.createElement('p');intro.className='hint';intro.textContent='Adopt recorded note groups as editable chords. Choose a root explicitly; the preview preserves every pitch. Only 2–8 distinct notes with matching start, end and velocity and no overlaps are eligible. This changes the draft; write when ready.';container.append(intro);
    const candidates=adoptionCandidates(source);
    if(!candidates.length){const p=document.createElement('p');p.textContent='No unambiguous note groups available. Other recorded notes remain unchanged.';container.append(p)}
    candidates.forEach((candidate,i)=>{
      const row=document.createElement('div');row.className='chord-adoption-row';const heading=document.createElement('strong');heading.textContent=`Step ${candidate.start+1} · ${candidate.length} steps · ${candidate.notes.map(noteName).join(' / ')}`;row.append(heading);
      const rootLabel=document.createElement('label'),choice=document.createElement('select');rootLabel.textContent='Declared root';choice.setAttribute('aria-label',`Group ${i+1} root`);options(choice,[['','Choose root…'],...Array.from({length:128},(_,n)=>[n,noteName(n)])]);rootLabel.append(choice);
      const nameLabel=document.createElement('label'),name=document.createElement('input');nameLabel.textContent='Name';name.value='Custom';name.maxLength=12;name.setAttribute('aria-label',`Group ${i+1} name`);nameLabel.append(name);
      const preview=document.createElement('small'),button=document.createElement('button');button.textContent=`Adopt group ${i+1} into draft`;button.disabled=true;button.dataset.needsRoot='true';
      const update=()=>{button.dataset.needsRoot=String(choice.value==='');button.disabled=working||choice.value==='';preview.textContent=choice.value===''?'':`Root ${noteName(Number(choice.value))} · intervals ${candidate.notes.map(n=>n-Number(choice.value)).join(', ')}`;};choice.onchange=update;
      button.onclick=()=>{if(choice.value==='')return;try{const adopted=adoptChord(source,recipes,candidate,Number(choice.value),name.value);source=adopted.source;recipes=adopted.recipes;changed()}catch(e){fail(e)}};
      row.append(rootLabel,nameLabel,preview,button);container.append(row);
    });
  }
  function renderRows(){
    renderAdoption();q('[data-entries]').replaceChildren();
    recipes.forEach((c,index)=>{
      const row=document.createElement('div');row.className='chord-recipe-row';
      const heading=document.createElement('strong');try{heading.textContent=`${String(index+1).padStart(2,'0')} · ${chordLabel(c)} · ${recipeNotes(c).map(noteName).join(' / ')}`}catch{heading.textContent=`Chord ${index+1} · adjust root or scale`}row.append(heading);
      const fields=document.createElement('div');fields.className='harmony-options';row.append(fields);
      function field(label,key,values,min,max,offset=0){const wrapper=document.createElement('label');wrapper.textContent=label;const input=document.createElement(values?'select':'input');input.setAttribute('aria-label',`Chord ${index+1} ${label.toLowerCase()}`);if(values)options(input,values);else{input.type='number';input.min=min;input.max=max}input.value=Number(c[key])+offset;const update=()=>{const previous={...c};c[key]=input.value===''?NaN:Number(input.value)-offset;
        if(key==='scaleFree'){try{Object.assign(c,setChordScaleFree(previous,!!c.scaleFree,getGlobalScale()));}catch(e){Object.assign(c,previous);renderRows();fail(e);return}}
        const activeScale=getGlobalScale();
        if(canBound&&!c.scaleFree&&activeScale?.enabled&&(key==='root'||key==='type')){c.root=quantizeNote(c.root,activeScale.root,activeScale.kind);c.scaleRoot=activeScale.root;c.scaleKind=activeScale.kind;c.quantize=false;if(!scaleChordTypes.includes(c.type))c.type=chordVoices(c.type,c.intervals)>=4?11:10;appliedScale={...activeScale};}
        if(key==='type'&&c.type===12&&previous.type!==12){try{Object.assign(c,customRecipe(recipeNotes(previous),previous.root,'Custom',{...previous,inversion:0}))}catch(e){Object.assign(c,previous);renderRows();fail(e);return}}
        if(key==='quantize'&&c.quantize){const scale=getGlobalScale();if(scale?.enabled){c.quantizeRoot=scale.root;c.quantizeKind=scale.kind;appliedScale={...scale};}}
scalePreview=null;q('[data-confirm]').checked=false;if(key==='type'||key==='quantize'||key==='scaleFree'||key==='root'){c.inversion=Math.min(c.inversion??0,chordVoices(c.type,c.intervals)-1);renderRows();}else{try{heading.textContent=`${String(index+1).padStart(2,'0')} · ${chordLabel(c)} · ${recipeNotes(c).map(noteName).join(' / ')}`}catch{heading.textContent=`Chord ${index+1} · adjust root or scale`}}render()};if(values)input.onchange=update;else input.oninput=update;wrapper.append(input);fields.append(wrapper)}
      const bound=canBound&&!c.scaleFree&&(getGlobalScale()?.enabled||scaleChordTypes.includes(c.type));
      if(canBound){c.scaleFree??=false;field('Scale policy','scaleFree',[[0,'Scale-bound'],[1,'Free · this chord only']]);}
      field('Root','root',Array.from({length:128},(_,i)=>[i,noteName(i)]));
      field('Type','type',chordTypes.map((n,i)=>[i,n]).filter(([i])=>(canBound||i<13)&&(canCustom||i!==12)&&(!bound||scaleChordTypes.includes(i)||i===c.type)));field('Start step','start',null,1,source.length,1);field('Length','length',null,1,source.length);field('Velocity','velocity',null,1,127);
      if(canInvert)field('Inversion','inversion',inversionNames.slice(0,chordVoices(c.type,c.intervals)).map((name,i)=>[i,c.type===12&&!i?'As supplied':name]));
      if(canCustom){c.quantize??=false;field('Quantize voices','quantize',[[0,'Off · preserve voicing'],[1,'On · saved scale']]);}
      if(c.type===12){
        function textField(label,value,apply){const wrapper=document.createElement('label'),input=document.createElement('input');wrapper.textContent=label;input.value=value;input.setAttribute('aria-label',`Chord ${index+1} ${label.toLowerCase()}`);input.oninput=()=>{apply(input.value);scalePreview=null;q('[data-confirm]').checked=false;try{heading.textContent=`${String(index+1).padStart(2,'0')} · ${chordLabel(c)} · ${recipeNotes(c).map(noteName).join(' / ')}`}catch{heading.textContent=`Chord ${index+1} · check custom intervals`}render()};wrapper.append(input);fields.append(wrapper);return input}
        textField('Name',c.name,v=>c.name=v).maxLength=12;
        textField('Intervals',c.intervals.join(', '),v=>{c.intervals=v.split(/[ ,]+/).filter(Boolean).map(Number);c.inversion=Math.min(c.inversion??0,Math.max(0,c.intervals.length-1))});
        const hint=document.createElement('p');hint.className='hint';hint.textContent='Intervals are semitones from the declared root. Negative values keep voices below the root. Source intervals stay intact when quantization is on.';row.append(hint);
      }
      if(scaleChordTypes.includes(c.type)){field('Scale key','scaleRoot',rootNames.map((n,i)=>[i,n]));field('Scale','scaleKind',scaleNames.map((n,i)=>[i,n]));}
      if(c.quantize){c.quantizeRoot??=c.scaleRoot;c.quantizeKind??=c.scaleKind;field('Quantize key','quantizeRoot',rootNames.map((n,i)=>[i,n]));field('Quantize scale','quantizeKind',scaleNames.map((n,i)=>[i,n]));}
      const remove=document.createElement('button');remove.textContent='Remove';remove.setAttribute('aria-label',`Remove chord ${index+1}`);remove.onclick=()=>{recipes.splice(index,1);changed()};fields.append(remove);q('[data-entries]').append(row);
    });
  }
  function render(){
    bytes=null;result=null;fail(null);const pattern=q('[data-pattern]').value,target=Number(q('[data-target]').value),chords=pattern==='chords';
    root.querySelectorAll('button,input,select').forEach(el=>el.disabled=working||el.dataset.needsRoot==='true');
    q('[data-read]').disabled=working||!available;q('[data-add]').disabled=working||!source||recipes.length>=64;q('[data-stride]').disabled=working||chords;
    q('[data-conform]').disabled=working||!source||!recipes.length;q('[data-quantize]').hidden=!canCustom;q('[data-quantize]').disabled=working||!source||!recipes.length;
    const global=getGlobalScale();q('[data-global-context]').textContent=global?`Global scale: ${rootNames[global.root]} ${scaleNames[global.kind]}${global.enabled?'':' · Off'}`:'Connect to read the current global scale.';q('[data-global-context]').textContent+=' · Applies to chord recipes on this track.';
    q('[data-scale-preview]').hidden=!scalePreview;
    q('[data-source]').textContent=source?`Track ${track+1} · ${source.length} steps · ${source.foreign.length} other note events retained${source.replacementModule?' · Writing selects stored Chord mode and resets the previous rhythm/palette':''}`:`Source: Track ${track+1}${available?' · read to begin':' · connect with chord-mode firmware to read'}`;
    q('[data-confirm-label]').textContent=chords?`Write chords to Track ${target+1}, preserving the captured track’s other notes, CC automation and settings. ${target!==track?'This replaces the destination track.':''}`:`Replace Track ${target+1} with an independent ${pattern==='bass'?'bass line':'arpeggio'} from these chord recipes. Other source notes and CC automation are not copied.`;
    q('[data-summary]').textContent='Read a chord track to begin.';
    if(source)try{
      if(!canBound&&[source.draft,...recipes].some(c=>c.scaleFree||c.type>12))throw Error('Update firmware for scale-bound chord variations.');
      if(!canCustom&&[source.draft,...recipes].some(c=>c.type===12||c.quantize))throw Error('Update firmware to write custom or quantized chords.');
      if(!canInvert&&[source.draft,...recipes].some(c=>c.inversion))throw Error('Update firmware to write inverted chord recipes.');
      const progression=recipeProgression(recipes,source.length);result=deriveNotes(progression,{pattern,stride:Number(q('[data-stride]').value)});
      if(chords)bytes=encodeChordProject(source,recipes,Number(q('[data-channel]').value));
      else {if(target===track)throw Error('Choose a different destination to keep the source chord track.');bytes=encodeChordTrack(result,Number(q('[data-channel]').value));}
      q('[data-summary]').textContent=`${recipes.length} editable chords · ${result.events.length} chord-output note events${chords?` + ${source.foreign.length} other notes`:''} · ${source.length} steps. Rests and sustained durations are retained.`;
    }catch(e){fail(e);bytes=null}
    q('[data-write]').disabled=working||(!source?.hasRecipes&&!recipes.length)||!!scalePreview||!bytes||!q('[data-confirm]').checked;q('[data-preview]').disabled=working||!result?.events.length;
  }
  function changed(){scalePreview=null;q('[data-confirm]').checked=false;renderRows();render()}
  async function read(){if(working||!available)return;working=true;render();try{const captured=await readTrack(track);if(!captured)throw Error('Read failed. Reconnect and try again.');source=decodeChordProject(captured,{allowUnowned:canCustom});appliedScale=null;recipes=source.recipes.map(c=>({...c}));q('[data-channel]').value=source.channel;working=false;changed()}catch(e){working=false;source=null;recipes=[];changed();fail(e)}}
  q('[data-read]').onclick=read;
  function scaleProposal(quantize){if(working||!source)return;try{
    const global=getGlobalScale();const next=(quantize?quantizeRecipesToScale:conformRecipesToScale)(recipes,source.length,global);encodeChordProject(source,next);
    scalePreview={recipes:next,scale:{enabled:!!global.enabled,root:global.root,kind:global.kind}};q('[data-confirm]').checked=false;
    q('[data-scale-description]').textContent=quantize?'Each sounding voice snaps to the nearest scale note (ties down). Colliding voices merge. Source intervals, root, inversion and timing stay intact; switch Quantize voices off to restore the source voicing.':'Built-in chords use scale-derived harmony; Free chords return to scale-bound triads or sevenths. Other custom chords retain their source intervals and quantize their voices. Timing and velocity are preserved.';
    q('[data-scale-title]').textContent=`Preview · ${rootNames[global.root]} ${scaleNames[global.kind]}`;
    q('[data-scale-changes]').replaceChildren(...recipes.map((c,i)=>{const row=document.createElement('p');row.textContent=`Step ${c.start+1}: ${chordLabel(c)} (${recipeNotes(c).map(noteName).join(' / ')}) → ${chordLabel(next[i])} (${recipeNotes(next[i]).map(noteName).join(' / ')})`;return row}));render();
  }catch(e){scalePreview=null;render();fail(e)}}
  root.addEventListener('keydown',event=>{
    if(event.repeat||event.isComposing||!(event.metaKey||event.ctrlKey)||event.code!=='Enter'||!source||working)return;
    if(event.shiftKey&&!event.altKey){event.preventDefault();scaleProposal(false);}
    else if(event.altKey&&!event.shiftKey&&canCustom){event.preventDefault();scaleProposal(true);}
  });
  q('[data-conform]').title='Preview scale conformity: Cmd/Ctrl + Shift + Enter';
  q('[data-quantize]').title='Preview voice quantization: Cmd/Ctrl + Alt/Option + Enter';
  q('[data-conform]').onclick=()=>scaleProposal(false);q('[data-quantize]').onclick=()=>scaleProposal(true);
  q('[data-scale-audition]').onclick=()=>{if(scalePreview)audition(recipeProgression(scalePreview.recipes,source.length))};
  q('[data-scale-cancel]').onclick=()=>{scalePreview=null;render()};
  q('[data-scale-apply]').onclick=()=>{if(!scalePreview||working)return;if(!sameScale(getGlobalScale(),scalePreview.scale)){fail(Error('Global scale changed. Preview it again before applying.'));return}recipes=scalePreview.recipes;appliedScale=scalePreview.scale;changed()};
  q('[data-add]').onclick=()=>{if(!source||recipes.length>=64)return;const start=Math.max(0,...recipes.map(c=>c.start+c.length));let chord={...source.draft,start,length:Math.min(source.draft.length,Math.max(1,source.length-start)),scaleFree:false};if(canBound&&getGlobalScale()?.enabled)chord=setChordScaleFree(chord,false,getGlobalScale());recipes.push(chord);changed()};
  for(const el of root.querySelectorAll('select,input'))el.onchange=()=>{if(el===q('[data-pattern]')&&el.value!=='chords'&&Number(q('[data-target]').value)===track)q('[data-target]').value=(track+1)%12;if(el!==q('[data-confirm]')){q('[data-confirm]').checked=false;scalePreview=null}render()};
  q('[data-preview]').onclick=()=>{if(result)audition(result)};
  q('[data-write]').onclick=async()=>{if(working||scalePreview||!bytes||!q('[data-confirm]').checked)return;const target=Number(q('[data-target]').value),output=bytes;working=true;render();try{const ok=await install(target,output,{track,bytes:source.bytes,...(appliedScale?{scale:appliedScale}:{})});working=false;render();if(ok){q('[data-confirm]').checked=false;if(target===track){source=decodeChordProject(output);recipes=source.recipes.map(c=>({...c}));renderRows();render()}q('[data-summary]').textContent=`Track ${target+1} written and verified. Save on board to persist it.`}}catch(e){working=false;render();fail(e)}};
  return {get working(){return working},open(t,enabled,inversions=false,custom=false,bound=false){if(working)return;track=t;available=enabled;canInvert=inversions;canCustom=custom;canBound=bound;source=null;recipes=[];scalePreview=null;appliedScale=null;q('[data-pattern]').value='chords';q('[data-target]').value=track;q('[data-confirm]').checked=false;renderRows();render()}};
}
