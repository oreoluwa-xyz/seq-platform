import {setupChordWorkshop} from './chord-workshop.mjs?v=27945b640f';
import {decodeMelody,harmonize,encodeChordTrack,harmonyProviders,progressionCandidates,buildProgression,deriveNotes,notePatterns,editableRecipes,quantizeProgression} from './harmony.mjs?v=54cddb1a55';
import {scaleNames,rootNames} from './scale.mjs?v=bcc8228f15';
import {noteName} from './module.mjs?v=9e9d1bb338';
export function setupHarmony(dialog,{capture,readTrack,install,bank,audition,getGlobalScale,setBankIdentity}){
  let source=null,result=null,sourceTrack=0,working=false,canCapture=false,canChord=false,canInvert=false,canCustom=false;
  const bankDrafts=new Map();
  const bankDraft=t=>{if(!bankDrafts.has(t))bankDrafts.set(t,structuredClone(bank(t)));return bankDrafts.get(t)};
  let entries=[0,3,4,0].map(candidate=>({candidate,duration:16,inversion:0,octave:0}));
  dialog.innerHTML=`<div class="harmony-header"><div><div class="eyebrow">HARMONY WORKSHOP · CTRL ⇧ SPACE</div><h2>Find the next voice.</h2></div><button data-close>Close</button></div>
    <div class="harmony-options"><label>Start from<select data-workflow><option value="melody">A captured melody</option><option value="progression">A new chord progression</option><option value="track">An existing chord track</option></select></label></div>
    <section data-chord-workshop hidden></section><div data-generator><section data-capture-panel><p data-source></p><button data-capture>Capture melody</button><p class="hint">Capture stops playback. Uses the complete loop’s highest projected voice, including Euclid, recorded overrides, palette and scale. Probability, mutes and accumulator movement are excluded.</p></section>
    <div class="harmony-options"><label>Harmony provider<select data-provider></select></label><label data-rate-label>Chord changes<select data-rate><option value="bar">Each bar</option><option value="beat">Each beat</option><option value="hit">Each melody hit</option></select></label><label>Key<select data-root></select></label><label>Scale<select data-kind></select></label><label data-bank-label hidden>Chord bank<select data-bank></select></label></div>
    <details data-bank-identities hidden><summary>Bank chord roots & names</summary><p class="hint">Declare each root to keep the chord editable. Roots and names are stored in your local bank and exported JSON; the bank playback protocol only carries notes. Imported chord steps preserve their identities on hardware. Hardware chord names use 12 plain characters.</p><div data-bank-entries></div></details><label class="harmony-confirm"><input type="checkbox" data-quantize> Quantize voices to the selected key and scale</label><p class="hint" data-method></p><section data-compose hidden><div class="harmony-entries"></div><button data-add>Add chord +</button><p class="hint">Durations are in sixteenth-note steps: 4 = one beat, 16 = one bar. Inversions raise the lowest notes by an octave.</p></section>
    <div class="harmony-options harmony-output"><label>Generate<select data-pattern></select></label><label>Note every<select data-stride><option value="1">1 step · 1/16</option><option value="2" selected>2 steps · 1/8</option><option value="4">4 steps · 1/4</option><option value="8">8 steps · 1/2</option><option value="16">16 steps · 1 bar</option></select></label><label>Output octave<select data-octave></select></label></div>
    <label class="harmony-confirm"><input type="checkbox" data-editable> Keep supported chords editable on the keyboard</label><p class="hint" data-recipe-hint></p><p data-summary role="status"></p><div class="harmony-progression"></div>
    <div class="harmony-destination"><label>Destination<select data-target></select></label><label>MIDI channel<select data-channel></select></label><button data-audition>Audition output</button></div>
    <label class="harmony-confirm"><input type="checkbox" data-confirm> <span data-confirm-label></span></label><p class="hint">Each output is an independent track of editable notes. Later source or scale changes do not rewrite it. A full recovery backup downloads before installation. Save on board afterward to persist it.</p><div class="dialog-actions"><button data-install class="primary">Back up & create track</button></div><p data-error role="alert"></p></div>`;
  const q=s=>s==='[data-chord-workshop]'||s==='[data-workflow]'||s==='[data-generator]'||s==='[data-close]'?dialog.querySelector(s):dialog.querySelector('[data-generator]').querySelector(s);
  const chordWorkshop=setupChordWorkshop(q('[data-chord-workshop]'),{readTrack,install,audition,getGlobalScale});
  function options(el,items){el.replaceChildren();for(const [value,text] of items){const o=document.createElement('option');o.value=value;o.textContent=text;el.append(o)}}
  options(q('[data-provider]'),harmonyProviders.map(p=>[p.id,p.label]));options(q('[data-root]'),rootNames.map((n,i)=>[i,n]));options(q('[data-kind]'),scaleNames.map((n,i)=>[i,n]));options(q('[data-bank]'),Array.from({length:12},(_,i)=>[i,`Track ${i+1} · local bank draft`]));options(q('[data-channel]'),Array.from({length:16},(_,i)=>[i,`${i+1}`]));options(q('[data-pattern]'),notePatterns.map(p=>[p.id,p.label]));options(q('[data-octave]'),Array.from({length:7},(_,i)=>[i-3,`${i-3>0?'+':''}${i-3}`]));q('[data-octave]').value=0;
  const isNew=()=>q('[data-workflow]').value==='progression';
  const settings=()=>({provider:q('[data-provider]').value,rate:q('[data-rate]').value,root:Number(q('[data-root]').value),kind:Number(q('[data-kind]').value),bank:bankDraft(Number(q('[data-bank]').value))});
  const error=e=>q('[data-error]').textContent=e?.message??'';
  const changed=()=>{q('[data-confirm]').checked=false;render()};
  const busy=value=>{working=value;q('[data-generator]').querySelectorAll('button,input,select').forEach(el=>el.disabled=value);if(!value)render()};
  function targets(){const previous=q('[data-target]').value;options(q('[data-target]'),Array.from({length:12},(_,i)=>i).filter(i=>isNew()||i!==sourceTrack).map(i=>[i,`Track ${i+1}`]));q('[data-target]').value=previous!==''&&(isNew()||Number(previous)!==sourceTrack)?previous:(sourceTrack+1)%12}
  function renderBankIdentities(visible){
    q('[data-bank-identities]').hidden=!visible;const container=q('[data-bank-entries]');container.replaceChildren();if(!visible)return;
    const t=Number(q('[data-bank]').value),draft=bankDraft(t);
    draft.config.slots.forEach((slot,i)=>{if(!slot.notes.length)return;const row=document.createElement('div');row.className='bank-identity-row';const title=document.createElement('span');title.textContent=`${i+1} · ${slot.notes.map(noteName).join(' / ')}`;
      const root=document.createElement('select');root.setAttribute('aria-label',`Bank slot ${i+1} root`);options(root,[['','Unknown root'],...Array.from({length:128},(_,n)=>[n,noteName(n)])]);root.value=slot.root??'';root.onchange=()=>{slot.root=root.value===''?null:Number(root.value);setBankIdentity?.(t,i,{root:slot.root});changed()};
      const name=document.createElement('input');name.value=slot.name;name.maxLength=64;name.setAttribute('aria-label',`Bank slot ${i+1} name`);name.onchange=()=>{slot.name=name.value;setBankIdentity?.(t,i,{name:slot.name});changed()};row.append(title,name,root);container.append(row);
    });
  }
  function renderEntries(candidates){
    const container=q('.harmony-entries');container.replaceChildren();
    entries.forEach((entry,index)=>{
      const row=document.createElement('div');row.className='harmony-entry';const number=document.createElement('span');number.textContent=String(index+1).padStart(2,'0');row.append(number);
      function select(label,field,items){const wrapper=document.createElement('label'),el=document.createElement('select');wrapper.textContent=label;el.setAttribute('aria-label',`Chord ${index+1} ${label.toLowerCase()}`);options(el,items);el.value=entry[field];el.onchange=()=>{entry[field]=Number(el.value);if(field==='candidate')entry.inversion=0;changed()};wrapper.append(el);row.append(wrapper)}
      select('Chord','candidate',candidates.map((c,i)=>[i,c.label]));
      const durationLabel=document.createElement('label'),duration=document.createElement('input');durationLabel.textContent='Steps';duration.type='number';duration.min=1;duration.max=2048;duration.value=entry.duration;duration.setAttribute('aria-label',`Chord ${index+1} steps`);duration.onchange=()=>{entry.duration=Number(duration.value);changed()};durationLabel.append(duration);row.append(durationLabel);
      select('Inversion','inversion',Array.from({length:candidates[entry.candidate]?.notes.length??1},(_,i)=>[i,i===0?'As supplied':`+${i}`]));select('Octave','octave',[-2,-1,0,1,2].map(i=>[i,i>0?`+${i}`:`${i}`]));
      const remove=document.createElement('button');remove.textContent='Remove';remove.setAttribute('aria-label',`Remove chord ${index+1}`);remove.disabled=entries.length===1;remove.onclick=()=>{entries.splice(index,1);changed()};row.append(remove);container.append(row);
    });q('[data-add]').disabled=entries.length>=32;
  }
  function render(){
    const existing=q('[data-workflow]').value==='track';q('[data-chord-workshop]').hidden=!existing;q('[data-generator]').hidden=existing;if(existing)return;
    result=null;error(null);const fresh=isNew(),useBank=q('[data-provider]').value==='harmony.bank';
    q('[data-capture-panel]').hidden=fresh;q('[data-compose]').hidden=!fresh;q('[data-rate-label]').hidden=fresh;
    q('[data-capture]').disabled=!canCapture||working;
    q('[data-source]').textContent=source?`Captured Track ${sourceTrack+1} · ${source.length} steps · snapshot`:canCapture?`Source: Track ${sourceTrack+1}. Capture its melody to begin.`:'Select a melodic hardware track and reconnect with harmony-capture firmware to capture. You can build a new progression now.';
    q('[data-bank-label]').hidden=!useBank;q('[data-root]').disabled=useBank&&!q('[data-quantize]').checked;q('[data-kind]').disabled=useBank&&!q('[data-quantize]').checked;renderBankIdentities(useBank);
    q('[data-stride]').disabled=q('[data-pattern]').value==='chords';q('[data-editable]').disabled=!canChord;
    q('[data-recipe-hint]').textContent=canChord?(canCustom?'Scale chords and bank chords with declared roots retain editable recipes. Quantization preserves source voicings; coincident voices merge. Chords without a valid root remain ordinary notes.':canInvert?'Scale chords retain editable roots, types and inversions. Update firmware for custom or quantized recipes.':'Root-position scale chords retain recipes. Update firmware to keep inverted chords editable. Bank voicings stay ordinary notes.'):'Chord-mode firmware is required to keep recipes; output remains ordinary notes.';
    q('[data-method]').textContent=fresh?(useBank?'Choose chords from the local bank draft. Bank pitch classes are preserved.':'Choose chords built from alternating scale degrees. Pentatonic scales use the same degree-stacking rule.'):(useBank?'Chooses bank chords closest to each melody anchor, preferring shared pitch classes and smooth movement.':'Builds three-note chords from alternating scale degrees, then chooses matching voicings with smooth movement.');
    q('.harmony-progression').replaceChildren();q('[data-summary]').textContent='No melody captured yet.';
    try{
      if(fresh)renderEntries(progressionCandidates(settings()));
      if(fresh||source){
        let progression=fresh?buildProgression(entries,settings()):harmonize(source,settings());
        if(q('[data-quantize]').checked)progression=quantizeProgression(progression,Number(q('[data-root]').value),Number(q('[data-kind]').value));
        result=deriveNotes(progression,{pattern:q('[data-pattern]').value,stride:Number(q('[data-stride]').value),octave:Number(q('[data-octave]').value)});
        encodeChordTrack(result,Number(q('[data-channel]').value),{editable:canChord&&q('[data-editable]').checked,inversions:canInvert,custom:canCustom});
        q('[data-summary]').textContent=`${result.length} steps · ${result.chords.length} chords · ${result.events.length} / 128 note events · ${canChord&&q('[data-editable]').checked?editableRecipes(result,{inversions:canInvert,custom:canCustom}).length:0} hardware-editable chords${result.chords.length>64?' · showing first 64 chords':''}`;
        for(const chord of result.chords.slice(0,64)){
          const el=document.createElement('div');el.className='harmony-chord';const step=document.createElement('small'),label=document.createElement('strong'),notes=document.createElement('span'),output=document.createElement('small');step.textContent=`STEP ${chord.start+1}–${chord.end+1}`;label.textContent=chord.label;notes.textContent=chord.notes.map(noteName).join(' · ');
          const pitches=result.events.filter(e=>e.start>=chord.start&&e.start<=chord.end).map(e=>noteName(e.pitch));output.textContent=`OUT: ${pitches.slice(0,16).join(' → ')}${pitches.length>16?' …':''}`;el.append(step,label,notes,output);if(chord.anchor!==undefined){const anchor=document.createElement('small');anchor.textContent=`Melody ${noteName(chord.anchor)} · ${chord.matched?'chord tone':'nearest match'}`;el.append(anchor)}q('.harmony-progression').append(el);
        }
      }
    }catch(e){error(e);q('[data-summary]').textContent='Adjust the settings to generate a playable track.';result=null}
    q('[data-confirm-label]').textContent=`Replace Track ${Number(q('[data-target]').value)+1}’s notes, drum lanes, module and CC automation with this output.`;
    q('[data-install]').disabled=!result||!q('[data-confirm]').checked||working;q('[data-audition]').disabled=!result||working;
  }
  q('[data-close]').onclick=()=>{if(!working&&!chordWorkshop.working)dialog.close()};dialog.addEventListener('cancel',e=>{if(working||chordWorkshop.working)e.preventDefault()});
  for(const el of q('[data-generator]').querySelectorAll('select,input'))el.onchange=()=>{
    if(el===q('[data-confirm]')){render();return}
    if(el===q('[data-workflow]'))targets();
    if(['[data-provider]','[data-root]','[data-kind]','[data-bank]'].some(s=>el===q(s)))try{const candidates=progressionCandidates(settings());entries=entries.map(e=>({...e,candidate:Math.min(e.candidate,candidates.length-1),inversion:0}))}catch{}
    changed();
  };
  q('[data-workflow]').onchange=()=>{if(working||chordWorkshop.working){q('[data-workflow]').value=q('[data-chord-workshop]').hidden?'progression':'track';return}targets();changed()};
  q('[data-add]').onclick=()=>{if(entries.length<32){entries.push({...entries.at(-1)});changed()}};
  q('[data-capture]').onclick=async()=>{busy(true);try{const bytes=await capture(sourceTrack);if(!bytes)throw Error('Capture failed. Reconnect and try again.');source=decodeMelody(bytes);q('[data-root]').value=source.global.root;q('[data-kind]').value=source.global.kind;q('[data-channel]').value=(source.channel+1)%16;q('[data-confirm]').checked=false;}catch(e){source=null;busy(false);error(e);return}busy(false)};
  q('[data-audition]').onclick=()=>{if(result)audition(result)};
  q('[data-install]').onclick=async()=>{if(!result||working||!q('[data-confirm]').checked)return;const target=Number(q('[data-target]').value),bytes=encodeChordTrack(result,Number(q('[data-channel]').value),{editable:canChord&&q('[data-editable]').checked,inversions:canInvert,custom:canCustom});if(!isNew()&&target===sourceTrack)return;busy(true);try{if(await install(target,bytes)){q('[data-confirm]').checked=false;busy(false);q('[data-summary]').textContent=`Created Track ${target+1}. Choose another output and destination to make a companion track.`;return}}catch(e){busy(false);error(e);return}busy(false)};
  return {open(track,{captureAvailable=true,chordAvailable=false,inversionAvailable=false,customAvailable=false,scaleBoundAvailable=false,chordActive=false,global}={}){if(working||chordWorkshop.working||dialog.open)return;sourceTrack=track;source=null;result=null;canCapture=captureAvailable;canChord=chordAvailable;canInvert=inversionAvailable;canCustom=customAvailable;bankDrafts.clear();chordWorkshop.open(track,canChord,canInvert,canCustom,scaleBoundAvailable);q('[data-quantize]').checked=false;q('[data-editable]').checked=canChord;q('[data-workflow]').value=chordActive?'track':canCapture?'melody':'progression';if(global){q('[data-root]').value=global.root;q('[data-kind]').value=global.kind}targets();q('[data-target]').value=(track+1)%12;q('[data-confirm]').checked=false;render();dialog.showModal()}};
}
