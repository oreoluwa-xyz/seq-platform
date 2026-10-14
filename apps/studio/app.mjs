import {renderTemp} from './temp.mjs';
import {setupFirmware,verifyFirmware} from './firmware.mjs?v=cdb10208e0';
import {decodeProjectScale,sameScale} from './chord-project.mjs?v=6c6f346c5c';
import {renderChordMode} from './chord-mode.mjs?v=886590c057';
import {harmonyRequestChanged} from './harmony.mjs?v=a92972a827';
import {setupHarmony} from './harmony-editor.mjs?v=f9d12cc709';
import {renderCC} from './cc.mjs?v=d5be1c11b7';
import {renderScale,renderScaleModal} from './scale.mjs?v=bcc8228f15';
import {renderRhythmControls,rhythmEditPayload} from './rhythm-controls.mjs';
import {renderPalette,paletteRemovePayload} from './palette.mjs?v=d934a0c35a';
import {renderHardwareOrbit} from './orbit.mjs?v=b14d15f367';
import {setupManual} from './manual.mjs?v=17e7ffad90';
import {defaultModule,validateModule,encodeModule,decodeModule,composeModule,splitModule,rhythmComponent,assertSupported,chordNotes,noteName,parseNotes,euclidHit} from './module.mjs?v=08f7f2e951';
import {SequencerConnection,sameBytes,projectFile} from './midi.mjs?v=6bb2561018';
import {LiveMonitor} from './live.mjs?v=79937b2913';
import {renderLiveSteps,liveBarLabel} from './steps.mjs?v=0df1ccd105';
const $=id=>document.getElementById(id), number=i=>String(i+1).padStart(2,'0');
if(document.querySelector('meta[name="seq-hosting"][content="static"]')) {
  const status=document.querySelector('.workspace-status');
  if(status) {
    status.textContent='BROWSER WORKSPACE';
    status.title='Drafts are saved in this browser, separately from the desktop app.';
  }
}
let manual=null;
let liveMonitor=null, liveState=null, liveReady=false, queuedHardwareState=null;
let lastHarmonyRequest=null;
let selected=0, connection=null, access=null, capabilities=null, busy=false, toastTimer, audio;
let previewMuted=false,previewGeneration=0;
const previewVoices=new Set();
try{previewMuted=localStorage.getItem('seq-preview-muted')==='true';}catch{}
let tracks=Array.from({length:12},(_,i)=>({engine:'euclid',drafts:{euclid:defaultModule('euclid'),'chord-bank':defaultModule('chord-bank')}}));
try {
  const saved=JSON.parse(localStorage.getItem('seq-module-studio-v1')||'null');
  if(Array.isArray(saved)&&saved.length===12) tracks=saved.map(t=>{const parsed={engine:['euclid','chord-bank'].includes(t.engine)?t.engine:'euclid',rhythm:t.rhythm||null,drafts:{euclid:validateModule(t.drafts.euclid),'chord-bank':validateModule(t.drafts['chord-bank'])}};if(parsed.rhythm)composeModule(parsed.drafts[parsed.engine],parsed.rhythm);return parsed;});
} catch { /* Invalid local drafts never reach the keyboard. */ }
const draft=()=>tracks[selected].drafts[tracks[selected].engine];
const assembledDraft=()=>tracks[selected].rhythm?composeModule(draft(),tracks[selected].rhythm):validateModule(draft());
function adoptModule(input){const {module,rhythm}=splitModule(input);tracks[selected].engine=module.engine;tracks[selected].drafts[module.engine]=module;tracks[selected].rhythm=rhythm;}
function renderComposer(){
  const r=tracks[selected].rhythm;
  document.querySelector('.module-card[data-engine="euclid"]').innerHTML=`<span class="engine-icon" aria-hidden="true">◌</span> ${r?'Track notes + palette':'Euclid'} <span class="engine-number">02</span>`;
  if(r){$('export').textContent='Export module ↗';$('editor-kicker').textContent=(r.id==='rhythm.euclid'?'EUCLIDEAN PULSES':'RECORDED TIMING')+' / '+(draft().engine==='chord-bank'?'CHORD BANK':'TRACK NOTES');$('module-description').textContent='Choose when notes play and which notes you hear.';$('engine-category').textContent='COMPOSABLE MODULE';$('engine-title').textContent=draft().name;$('engine-subtitle').textContent='Give your notes a rhythm. Give your rhythm a voice.';}
  $('component-rhythm').value=r?.id||'legacy';
  $('component-source').textContent=draft().engine==='chord-bank'?'Chord bank':'Track notes + palette';
  $('component-palette').hidden=!r||draft().engine!=='euclid';
  if(draft().engine==='euclid')$('pitch-mode').value=draft().config.pitchMode??'transpose';
  if(r&&draft().engine==='euclid'){$('component-pool').value=draft().config.pool.join(', ');$('component-root').value=draft().config.root;$('component-pin').value=draft().config.pin??'';}
  const chords=draft().engine==='chord-bank';
  $('component-rhythm').querySelector('[value="legacy"]').textContent=chords?'Use chord-bank performance':'Use Euclid instrument';
  $('component-description').textContent=!r
    ?chords?'Play chords with Alt/Option + number, or stamp them onto steps with Alt/Option + step. Playback follows your recorded notes. Choose Euclidean pulses to repeat the selected chord automatically.'
      :'The Euclid instrument combines its rhythm controls with your track notes and palette. Turn off Generate Euclidean hits to play recorded notes with their full lengths.'
    :r.id==='rhythm.stored'
      ?chords?'Each recorded note start triggers the selected bank chord for one step. The recorded notes supply the timing; the bank supplies the chord you hear.'
        :'Play your recorded notes at their recorded positions and lengths. An empty track stays silent.'
      :chords?'Each Euclidean pulse plays the selected bank chord for one step. Change the chord with Alt/Option + number. Turn off Generate Euclidean hits to follow recorded note starts instead.'
        :'Each Euclidean pulse plays the recorded notes spanning that step. Palette notes supplies pitches for empty steps; recorded notes act as overrides. Transpose phrase adds palette offsets to recorded or picked notes. Turn off Generate Euclidean hits to play the recorded phrase with its full note lengths.';

}
$('component-rhythm').onchange=e=>{const id=e.target.value;tracks[selected].rhythm=id==='legacy'?null:id==='rhythm.stored'?{id,version:1,params:{}}:rhythmComponent(draft().engine==='euclid'?draft():defaultModule('euclid'));persist();render();};
function editRhythm(change){
  if(busy)return;
  if(!tracks[selected].rhythm){edit(m=>change(m.config));return;}
  const previous=structuredClone(tracks[selected].rhythm);
  try{change(tracks[selected].rhythm.params);assembledDraft();persist();}catch(error){tracks[selected].rhythm=previous;notice(error.message,true);}
  render();
}
$('pitch-mode').onchange=e=>edit(m=>{m.version=3;m.config.pitchMode=e.target.value;});
$('component-pool').onchange=e=>edit(m=>{m.config.pool=e.target.value.trim()?e.target.value.split(',').map(n=>Number(n.trim())):[];if(m.config.pin>=m.config.pool.length)m.config.pin=null;});
$('component-root').onchange=e=>edit(m=>m.config.root=Number(e.target.value));
$('component-pin').onchange=e=>edit(m=>m.config.pin=e.target.value===''?null:Number(e.target.value));
function notice(text,error=false,persistent=false){clearTimeout(toastTimer);$('message').textContent=text;$('message').className=`visible${error?' error':''}`;if(!persistent)toastTimer=setTimeout(()=>$('message').className='',6500);}
function persist(){try{localStorage.setItem('seq-module-studio-v1',JSON.stringify(tracks));}catch{notice('Browser storage is unavailable. Export your module to keep these changes.',true);}}
function edit(change){if(busy)return;try{const m=structuredClone(draft());change(m);tracks[selected].drafts[m.engine]=validateModule(m);persist();render();}catch(error){notice(error.message,true);render();}}
function download(name,data){const blob=new Blob([JSON.stringify(data,null,2)+'\n'],{type:'application/json'}),url=URL.createObjectURL(blob),a=document.createElement('a');a.href=url;a.download=name;a.click();setTimeout(()=>URL.revokeObjectURL(url),1000);}
function renderTracks(){
  $('tracks').replaceChildren(...tracks.map((t,i)=>{
    const button=document.createElement('button');button.className=`track${i===selected?' active':''}`;button.setAttribute('aria-current',i===selected?'true':'false');
    const index=document.createElement('span');index.className='track-index';index.textContent=number(i);
    const text=document.createElement('span'),title=document.createElement('strong'),small=document.createElement('small'),dot=document.createElement('span');
    title.textContent=`Track ${number(i)}`;small.textContent=capabilities?`${capabilities[7+i]?'CHORD BANK':'EUCLID'} · DEVICE`:`${t.engine==='chord-bank'?'CHORD BANK':'EUCLID'} · DRAFT`;
    button.setAttribute('aria-label',`${title.textContent}, ${small.textContent.toLowerCase()}`);
    dot.className='track-light';text.append(title,small);button.append(index,text,dot);button.onclick=()=>{if(busy)return;if(liveReady && $('live-follow').checked){liveMonitor.select(i);return;}selected=i;render();};return button;
  }));
}
function renderPiano(notes){
  const active=new Set(notes),start=notes.length?Math.floor(Math.min(...notes)/12)*12:48;
  const end=Math.min(127,Math.max(start+24,...notes)),keys=[];
  let white=0;
  for(let n=start;n<=end;n++){
    const black=[1,3,6,8,10].includes(n%12),key=document.createElement('span');
    key.className=`piano-key ${black?'black':'white'}${active.has(n)?' sounding':''}`;
    key.style.setProperty('--key',black?white-.32:white++);key.title=noteName(n);
    if(active.has(n)){const label=document.createElement('i');label.textContent=noteName(n);key.append(label);}
    keys.push(key);
  }
  $('piano').style.setProperty('--white-keys',white);$('piano').replaceChildren(...keys);
  $('piano').setAttribute('aria-label',`Piano voicing: ${notes.map(noteName).join(', ')||'empty'}`);
}
function renderOrbit(config){
  const ns='http://www.w3.org/2000/svg',svg=document.createElementNS(ns,'svg');svg.setAttribute('viewBox','0 0 300 300');
  const circle=(r,cls)=>{const node=document.createElementNS(ns,'circle');node.setAttribute('cx',150);node.setAttribute('cy',150);node.setAttribute('r',r);node.setAttribute('class',cls);svg.append(node);};
  circle(117,'orbit-track');circle(91,'orbit-inner');
  const count=Math.min(config.steps,64);
  for(let i=0;i<count;i++){
    const a=2*Math.PI*i/count-Math.PI/2,hit=euclidHit(config,i),node=document.createElementNS(ns,'circle');
    node.setAttribute('cx',150+117*Math.cos(a));node.setAttribute('cy',150+117*Math.sin(a));node.setAttribute('r',count>32?3:5);node.setAttribute('class',hit?'orbit-hit':'orbit-rest');svg.append(node);
  }
  for(const [value,y,cls] of [[`${config.fills}`,158,'orbit-count'],['DRAFT / '+config.steps+' STEPS',184,'orbit-label']]){
    const text=document.createElementNS(ns,'text');text.setAttribute('x',150);text.setAttribute('y',y);text.setAttribute('class',cls);text.setAttribute('text-anchor','middle');text.textContent=value;svg.append(text);
  }
  $('rhythm-orbit').replaceChildren(svg);$('rhythm-orbit').setAttribute('aria-label',`${config.fills} hits in ${config.steps} steps${config.steps>64?', showing first 64 steps':''}`);
}
function renderRhythm(){
  const r=tracks[selected].rhythm,composed=!!r;
  const visible=r?r.id==='rhythm.euclid':draft().engine==='euclid';
  $('euclid-editor').hidden=!visible;
  if(!visible)return;
  const c=r?r.params:draft().config;
  $('pitch-mode').closest('label').hidden=draft().engine!=='euclid';
  $('euclid-root').closest('label').hidden=composed;
  $('euclid-pool').closest('label').hidden=composed;
  $('euclid-ratchet').closest('label').hidden=composed&&draft().engine==='chord-bank';
  for(const field of ['steps','fills','rotation','probability','accent','ratchet']) $(`euclid-${field}`).value=c[field];
  if(!composed){$('euclid-root').value=c.root;$('euclid-pool').value=c.pool.join(', ');}$('euclid-enabled').checked=c.enabled;
  $('rhythm').replaceChildren(...Array.from({length:Math.min(c.steps,64)},(_,i)=>{const step=document.createElement('span');step.className=euclidHit(c,i)?'hit':'';step.textContent=i+1;return step;}));
  renderOrbit(c);
  $('rhythm-description').textContent=`${c.fills} hits across ${c.steps} steps · ${c.rotation} step rotation${c.steps>64?' · Showing the first 64 steps':''}${c.template!==null?' · Captured rhythm; changing steps or hits returns to Euclidean spacing':''}. Probability and accents apply during playback.`;
}
function render(){
  const m=draft(),c=m.config,chords=m.engine==='chord-bank';renderTracks();
  for(const card of document.querySelectorAll('.module-card[data-engine="chord-bank"]')) {
    card.disabled=!!capabilities && !(capabilities[6]&2);
    card.title=card.disabled?'Chord banks were removed from this firmware. Recorded chords remain available.':'';
  }
  $('bank-compatibility').hidden=!capabilities || !!(capabilities[6]&2);
  renderLive();
  $('track-crumb').textContent=`TRACK ${number(selected)}`;
  $('track-display').textContent=number(selected);
  $('engine-title').innerHTML=chords?'Chord bank<span>.</span>':'Euclid<span>.</span>';
  $('engine-category').textContent=chords?'HARMONIC INSTRUMENT':'RHYTHMIC INSTRUMENT';
  $('engine-subtitle').textContent=chords?'Offline voicings for the Harmony workshop.':'Find the pulse. Shift the possibilities.';
  $('export').textContent=chords?'Export bank ↗':'Export pattern ↗';
  document.body.dataset.engine=m.engine;
  document.querySelectorAll('.module-card[data-engine]').forEach(button=>{const active=button.dataset.engine===m.engine;button.classList.toggle('active',active);button.setAttribute('aria-pressed',String(active));});
  $('module-name').value=m.name;$('editor-kicker').textContent=chords?'BANK / 12 VOICINGS':'PATTERN / EUCLIDEAN GENERATOR';
  $('module-description').textContent=chords?'Select a voicing to shape its notes and expression.':'Set the density, then rotate the rhythm.';
  $('chord-editor').hidden=!chords;$('euclid-editor').hidden=chords;
  if(chords){
    $('transpose').value=c.transpose;$('inversion').value=c.inversion;
    $('chord-grid').replaceChildren(...c.slots.map((slot,i)=>{
      const button=document.createElement('button');button.className=`chord-slot${i===c.selected?' active':''}`;button.setAttribute('aria-pressed',String(i===c.selected));button.setAttribute('aria-label',`Slot ${i+1}: ${slot.name}`);
      const key=document.createElement('span'),title=document.createElement('strong'),notes=document.createElement('small');key.className='slot-key';key.textContent=`DRAFT ${number(i)}`;title.textContent=slot.name;notes.textContent=chordNotes(m,i).map(noteName).join(' · ')||'Empty slot';const bars=document.createElement('span');bars.className='pad-spectrum';bars.setAttribute('aria-hidden','true');
      const pitches=new Set(chordNotes(m,i).map(n=>n%12));
      for(let pitch=0;pitch<12;pitch++){const bar=document.createElement('i');bar.className=pitches.has(pitch)?'sounding':'';bar.style.setProperty('--bar-height',`${[35,52,72,44,88,57,38,77,48,95,65,42][pitch]}%`);bars.append(bar);}
      button.append(key,bars,title,notes);button.onclick=event=>{edit(next=>next.config.selected=i);if(event.detail===0)$('chord-grid').children[i]?.focus({preventScroll:true});};return button;
    }));
    const slot=c.slots[c.selected];$('selected-chord').textContent=slot.name;$('slot-number').textContent=number(c.selected);$('slot-name').value=slot.name;$('slot-root').replaceChildren(...[['','Unknown · choose root'],...Array.from({length:128},(_,n)=>[n,noteName(n)])].map(([value,label])=>{const o=document.createElement('option');o.value=value;o.textContent=label;return o}));$('slot-root').value=slot.root??'';$('slot-notes').value=slot.notes.map(noteName).join(' ');$('slot-velocity').value=slot.velocity;$('slot-length').value=slot.length;
    $('voicing').replaceChildren(...chordNotes(m).map(note=>{const tag=document.createElement('span');tag.textContent=noteName(note);return tag;}));
    renderPiano(chordNotes(m));
    $('preview').disabled=previewMuted||!slot.notes.length;
    $('mapping').textContent='Offline chord drafts · use the Harmony workshop to create recorded, editable notes. Bank performance requires older firmware.';
  }else{
    $('mapping').innerHTML='<span><kbd>Alt</kbd> Module controls</span><span><kbd>Q / W / E</kbd> Steps / fills / rotation</span><span><kbd>Encoder</kbd> Adjust</span><span><kbd>Alt + Tab</kbd> Capture rhythm</span>';
  }
  updateTransferControls();
  renderComposer();
  renderRhythm();
  manual?.refresh();
  $('draft-status').textContent='LOCAL DRAFT';
  $('install-description').textContent=connection?`Ready for Track ${number(selected)}. Keyboard changes only when you install.`:`Connect a keyboard to install this module on Track ${number(selected)}.`;
}
for(const button of document.querySelectorAll('.module-card[data-engine]'))button.onclick=()=>{tracks[selected].engine=button.dataset.engine;if(button.dataset.engine==='chord-bank'&&tracks[selected].rhythm?.id==='rhythm.euclid')tracks[selected].rhythm.params.ratchet=0;persist();render();};
$('module-name').onchange=e=>edit(m=>m.name=e.target.value.trim());
for(const field of ['transpose','inversion']) $(field).onchange=e=>edit(m=>m.config[field]=Number(e.target.value));
for(const field of ['name','velocity','length']) $(`slot-${field}`).onchange=e=>edit(m=>m.config.slots[m.config.selected][field]=field==='name'?e.target.value.trim():Number(e.target.value));
$('slot-root').onchange=e=>edit(m=>m.config.slots[m.config.selected].root=e.target.value===''?null:Number(e.target.value));
$('slot-notes').onchange=e=>edit(m=>m.config.slots[m.config.selected].notes=parseNotes(e.target.value));
for(const field of ['steps','fills','rotation','probability','accent','ratchet']) $(`euclid-${field}`).onchange=e=>editRhythm(c=>{
  c[field]=Number(e.target.value);
  if(field==='steps'){c.fills=Math.min(c.fills,c.steps);c.rotation=Math.min(c.rotation,c.steps-1);c.ratchet=Math.min(c.ratchet,c.steps);}
  if(field==='steps'||field==='fills')c.template=null;
});
$('euclid-pool').onchange=e=>edit(m=>{m.config.pool=(e.target.value.trim()?e.target.value.trim().split(/[\s,]+/).map(Number):[]);m.config.pin=null;});
$('euclid-root').onchange=e=>edit(m=>m.config.root=Number(e.target.value));
$('euclid-enabled').onchange=e=>editRhythm(c=>c.enabled=e.target.checked);
function renderPreviewSound(){
  $('sound-mute').setAttribute('aria-pressed',String(previewMuted));
  $('sound-state').textContent=previewMuted?'Sound off':'Sound on';
  $('sound-mute').title=previewMuted?'Unmute the UI test sound':'Mute the UI test sound';
  $('preview').disabled=previewMuted||draft().engine!=='chord-bank'||!draft().config.slots[draft().config.selected].notes.length;
  $('preview-hint').textContent=previewMuted?'Test sound muted · enable Sound in the header to audition.':'Browser synth preview · no MIDI sent';
}
function stopPreviewSound(){
  previewGeneration++;
  for(const voice of previewVoices){try{voice.oscillator.stop();}catch{}voice.oscillator.disconnect();voice.gain.disconnect();}
  previewVoices.clear();
}
function setPreviewMuted(muted){
  previewMuted=muted;
  if(muted)stopPreviewSound();
  renderPreviewSound();
}
$('sound-mute').onclick=()=>{
  setPreviewMuted(!previewMuted);
  try{localStorage.setItem('seq-preview-muted',String(previewMuted));}catch{}
};
window.addEventListener('storage',event=>{if(event.key==='seq-preview-muted'||event.key===null)setPreviewMuted(event.newValue==='true');});
renderPreviewSound();
$('preview').onclick=async()=>{
  if(previewMuted)return;
  const generation=previewGeneration;
  try{audio??=new AudioContext();await audio.resume();
    if(previewMuted||generation!==previewGeneration)return;
    const notes=chordNotes(draft()),now=audio.currentTime;
    for(const note of notes){
      const oscillator=audio.createOscillator(),gain=audio.createGain(),voice={oscillator,gain};
      previewVoices.add(voice);
      oscillator.type='triangle';oscillator.frequency.value=440*2**((note-69)/12);
      gain.gain.setValueAtTime(0,now);gain.gain.linearRampToValueAtTime(.1/Math.max(1,notes.length),now+.025);gain.gain.exponentialRampToValueAtTime(.0001,now+1.6);
      oscillator.connect(gain).connect(audio.destination);
      oscillator.onended=()=>{previewVoices.delete(voice);oscillator.disconnect();gain.disconnect();};
      oscillator.start(now);oscillator.stop(now+1.7);
    }
  }catch(error){stopPreviewSound();notice(`Preview unavailable: ${error.message}`,true);}
};
$('export').onclick=()=>{try{download(`${draft().id}.seq-module.json`,assembledDraft());notice('Module exported. Names and labels are saved in this file.');}catch(error){notice(error.message,true);}};
$('import').onclick=()=>$('module-file').click();
$('module-file').onchange=async e=>{try{const file=e.target.files[0];if(!file)return;if(file.size>65536)throw Error('Module files must be smaller than 64 KiB.');const m=validateModule(JSON.parse(await file.text()));adoptModule(m);persist();render();notice(`Imported “${m.name}” into the local draft. Nothing has been sent to the keyboard.`);}catch(error){notice(`Import rejected: ${error.message}`,true);}finally{e.target.value='';}};
const option=(value,text)=>{const o=document.createElement('option');o.value=value;o.textContent=text;return o;};
$('connect').onclick=async()=>{
  if(busy)return;
  if(connection){stopLive();await connection.close();connection=null;capabilities=null;$('connection').className='connection';$('connection').innerHTML='<i></i> Offline · local drafts';$('connect').textContent='Connect keyboard ↗';render();return;}
  try{
    if(!navigator.requestMIDIAccess)throw Error('This browser does not expose Web MIDI. Open the editor in a browser with Web MIDI support. Offline editing and export still work.');
    access=await navigator.requestMIDIAccess({sysex:true});
    for(const [id,ports] of [['midi-input',access.inputs],['midi-output',access.outputs]]){
      const list=[...ports.values()].filter(p=>p.state==='connected');$(id).replaceChildren(option('','Choose keyboard port'),...list.map(p=>option(p.id,p.name||p.id)));
      const matches=list.filter(p=>/keychron/i.test(p.name||''));if(matches.length===1)$(id).value=matches[0].id;
    }
    access.onstatechange=()=>{if(connection&&(connection.input.state==='disconnected'||connection.output.state==='disconnected')){stopLive();connection.close();connection=null;capabilities=null;$('connection').textContent='Disconnected';$('connect').textContent='Connect keyboard ↗';render();notice('Keyboard disconnected. Local drafts are safe. Reconnect before continuing.',true);}};
    $('connection-error').textContent='';
    $('connection-dialog').showModal();
  }catch(error){notice(error.message,true);}
};
$('attach').onclick=async()=>{
  const input=access?.inputs.get($('midi-input').value),output=access?.outputs.get($('midi-output').value);
  if(!input||!output){notice('Select both keyboard MIDI ports.',true);return;}
  $('connection-error').textContent='Connecting…';
  $('attach').disabled=true;let candidate;
  try{await input.open();await output.open();candidate=new SequencerConnection(input,output);capabilities=await candidate.capabilities();connection=candidate;startLive(candidate);$('connection-dialog').close();$('connection').className='connection live';$('connection').innerHTML='<i></i> Connected · module v1';$('connect').textContent='Disconnect';render();notice('Connected. Local drafts are unchanged. Read a track to load its device settings.');}
  catch(error){await candidate?.close();$('connection-error').textContent=error.message;notice(error.message,true);}finally{$('attach').disabled=false;}
};
function stopLive(){lastHarmonyRequest=null;liveMonitor?.stop();liveMonitor=null;liveState=null;liveReady=false;queuedHardwareState=null;renderLive();$('live-status').textContent='Connect to follow your keyboard';}
function startLive(device){
  stopLive();$('live-status').textContent='Reading hardware state…';
  liveMonitor=new LiveMonitor(device,{canPoll:()=>!busy&&!document.hidden,wantVisual:()=>!!window.seqDesktop||manual?.wantsVisual(),onState:state=>{
    liveState=state;liveReady=true;
    const openRequested=harmonyRequestChanged(lastHarmonyRequest,state.harmonyRequest);lastHarmonyRequest=state.harmonyRequest;
    if(openRequested)openHarmony();
    for(let i=0;i<12;i++)capabilities[7+i]=(state.moduleMask>>i)&1;
    capabilities[19]=state.melodicMask&255;capabilities[20]=state.melodicMask>>8;
    const editing=document.activeElement?.matches('input,textarea,select') && document.activeElement.id!=='live-follow';
    const modal=!!document.querySelector('dialog[open]');
    if($('live-follow').checked && selected!==state.track && !busy){
      if(editing||modal)queuedHardwareState=state;
      else {selected=state.track;queuedHardwareState=null;render();}
    }
    renderLive();
  },onStatus:status=>{
    if(status==='selection-blocked'){notice('Finish the held-key gesture or step edit on the keyboard, then select the track again.',true);return;}
    liveReady=status==='live';
    $('live-status').textContent=({live:'Live · USB MIDI',unsupported:'Live view needs updated firmware',stale:'Signal lost · retrying…'})[status]||status;
    renderLive();
  }});liveMonitor.start();
}
function updateTransferControls(){
  const blocked=!!liveState?.temp?.active;
  for(const id of ['read','install','backup','save']) {
    $(id).disabled=!connection||busy||blocked||(id==='install'&&draft().engine==='chord-bank'&&capabilities&&!(capabilities[6]&2));
    $(id).title=blocked?'Keep or restore TEMP on the keyboard before saving or transferring.':'';
  }
}
function renderLive(){
  renderTemp($('live-temp'),liveState,liveReady);
  document.body.dataset.liveMode=liveReady?(liveState?.visual?.mode||'off'):'unavailable';
  document.querySelectorAll('#tracks .track').forEach((node,i)=>{
    const rgb=liveReady&&liveState?.visual?.leds?.slice((i+1)*3,(i+2)*3);
    node.style.setProperty('--hardware-led',rgb?.length===3?`rgb(${[...rgb].join(',')})`:'transparent');
    node.classList.toggle('hardware-current',!!liveReady&&liveState?.track===i);
    node.title=liveReady?'Track '+(i+1)+' · indicator mirrors the keyboard LED':'Track '+(i+1)+' · hardware unavailable';
  });
  updateTransferControls();
  $('harmony-open').title='Harmony workshop · Ctrl + Shift + Space';
  $('harmony-open').disabled=busy;
  $('live-track-mode').disabled=busy||!liveReady||!liveState?.active||!liveState?.chordSupported;
  $('live-track-mode').value=liveState?.chordMode?.enabled?'1':liveState?.type===0?'2':'0';
  renderChordMode($('live-chord-mode'),liveState,liveReady&&!busy);
  renderCC($('cc-dock'),liveState,liveReady&&!busy);
  renderScaleModal($('scale-dialog'),liveState,liveReady&&!busy,editScale,auditionScale);
  manual?.updateLive(liveState,liveReady&&!busy);
  $('live-content').hidden=!liveState;
  document.querySelector('.live-panel').classList.toggle('stale',!!liveState&&!liveReady);
  if(!liveState)return;
  const s=liveState;
  $('live-transport').textContent=!liveReady?'Unavailable':!s.active?'Sequencer off':s.running?'Running':'Stopped';
  $('live-led').classList.toggle('running',liveReady&&s.running);
  $('live-record').textContent=s.armed?'REC ARMED':'';
  $('live-track').textContent=number(s.track);
  $('live-type').textContent=s.chordMode?.enabled?'Chord':['Drum','Melodic','Drum + melodic'][s.type]+(s.child?' / drum view':'');
  $('live-engine').textContent=s.chordMode?.enabled?'Recorded chords':s.composed?`${s.rhythm==='euclid'?'Euclid':'Steps'} → ${s.engine?'Chords':'Notes'}`:s.engine?'Chord bank':'Euclid';
  $('live-tempo-label').textContent=s.external?'CLOCK SOURCE':'TEMPO';
  $('live-tempo').textContent=s.external?'External MIDI':`${s.bpm} BPM`;
  $('live-position').textContent=`${s.step+1} / ${s.length}`;
  $('live-bar').textContent=liveBarLabel(s)+(s.editStep!==65535?` · EDIT STEP ${s.editStep+1}`:'');
  renderScale($('live-scale'),s,liveReady&&!busy,editScale,auditionScale);
  renderRhythmControls($('live-euclid-settings'),s,liveReady&&!busy,editLiveRhythm);
  $('live-euclid-settings').hidden=!!s.chordMode?.enabled;
  $('hardware-orbit-caption').textContent=`LIVE KEYBOARD · VIEWED 16 STEPS · ${s.projectedNotes?'Projected notes before probability, mutes and accumulator':'Stored note labels'} · Center: sampled sound`;
  renderPalette($('live-palette'),s,liveReady&&!busy,mode=>editLivePitch(s.track,0,Uint8Array.of(0,mode==='transpose'?1:0)),replace=>editLivePitch(s.track,0,Uint8Array.of(2,Number(replace))),index=>editLivePitch(s.track,0,paletteRemovePayload(s.palette,index)));
  renderHardwareOrbit($('hardware-orbit'),s,liveReady&&!busy);
  $('live-step-caption').textContent=renderLiveSteps($('live-steps'),s,liveReady&&!busy,step=>editLivePitch(s.track,step,Uint8Array.of(1)));
  for(const [id,notes] of [['live-selected',s.selected],['live-sounding',liveReady?s.sounding:[]]]){
    $(id).replaceChildren(...(notes.length?notes.map(note=>{const el=document.createElement('span');el.textContent=noteName(note);el.title=`MIDI ${note}`;return el;}):[document.createTextNode(liveReady?'—':'Unavailable')]));
  }
  document.querySelectorAll('.track').forEach((node,i)=>{
    node.classList.toggle('hardware-current',liveReady&&s.track===i);
    if(liveReady){const type=s.chordMode?.mask&(1<<i)?'CHORD':s.wrapperMask&(1<<i)?'HYBRID':s.melodicMask&(1<<i)?'MELODIC':'DRUM';const engine=type==='CHORD'?'STEPS':s.moduleMask&(1<<i)?'CHORD BANK':'EUCLID';node.querySelector('small').textContent=`${type} / ${engine}`;node.setAttribute('aria-label',`Track ${number(i)}, ${type.toLowerCase()}, ${engine.toLowerCase()}`);}
  });
}
async function editScale(track,context,payload){
  if(!connection||busy||!liveReady)return false;
  busy=true;renderLive();
  try{await liveMonitor.pause();await connection.request(20,track,context,payload);notice('Scale settings applied. Save the project to keep them.');return true;}
  catch(error){notice(error.status===2?'Settings changed or a keyboard gesture is active. Cancel changes, finish the gesture, and try again.':error.message,true);return false;}
  finally{busy=false;liveMonitor?.resume();renderLive();}
}
async function auditionScale(notes){
  if(previewMuted){notice('Enable Sound in the header to audition.');return;}
  const generation=previewGeneration;
  try{audio??=new AudioContext();await audio.resume();if(previewMuted||generation!==previewGeneration)return;
    notes.forEach((note,i)=>{const oscillator=audio.createOscillator(),gain=audio.createGain(),voice={oscillator,gain},time=audio.currentTime+i*.16;previewVoices.add(voice);oscillator.type='triangle';oscillator.frequency.value=440*2**((note-69)/12);gain.gain.setValueAtTime(.08,time);gain.gain.exponentialRampToValueAtTime(.0001,time+.22);oscillator.connect(gain).connect(audio.destination);oscillator.onended=()=>{previewVoices.delete(voice);oscillator.disconnect();gain.disconnect()};oscillator.start(time);oscillator.stop(time+.24)});
  }catch(error){stopPreviewSound();notice(error.message,true);}
}
async function editLiveRhythm(track,context,field,value,expected){
  if(!connection||busy||!liveReady||!liveState?.euclid?.editable)return;
  busy=true;renderLive();
  try{
    await liveMonitor.pause();
    await connection.request(19,track,context,rhythmEditPayload(field,value,expected));

    notice('Hardware rhythm updated. Save on the keyboard to keep it.');
  }catch(error){notice(error.status===2?'The keyboard changed or a gesture is active. Refresh the value and try again.':error.status===3?'That value is outside the keyboard’s current range.':error.message,true);}
  finally{busy=false;liveMonitor?.resume();renderLive();}
}
async function editLivePitch(track,step,payload){
  if(!connection||busy||!liveReady||!liveState?.palette?.editable)return;
  busy=true;renderLive();await liveMonitor.pause();
  try{await connection.request(18,track,step,payload);notice(payload[0]===3?'Palette note removed. Save on the keyboard to keep it.':payload[0]===2?(payload[1]?'Next capture will replace the palette.':'Next capture will add notes to free slots.'):payload[0]?`Step ${step+1} now uses the palette.`:payload[1]?'Transpose phrase selected.':'Palette notes selected.');}
  catch(error){notice(error.status===2?'Keyboard changed track or is editing. Finish the edit and try again.':error.status===4?'No room to split this sustained note. Free an event and try again.':error.message,true);}
  finally{busy=false;liveMonitor.resume();renderLive();}
}
$('live-follow').onchange=()=>{if($('live-follow').checked&&liveState&&liveReady&&!busy){selected=liveState.track;render();}};
document.addEventListener('focusout',()=>setTimeout(()=>{
  if(queuedHardwareState&&liveReady&&$('live-follow').checked&&!busy&&!document.querySelector('dialog[open]')&&!document.activeElement?.matches('input,textarea,select')){selected=liveState.track;queuedHardwareState=null;render();}
},0));
async function operation(fn){
  if(liveState?.temp?.active){notice('Keep or restore TEMP on the keyboard before saving or transferring.',true);return;}
  if(!connection||busy)return;busy=true;document.body.classList.add('busy');document.querySelector('.app-shell').inert=true;$('connect').disabled=true;const device=connection;
  renderLive();
  await liveMonitor?.pause();
  try{await fn(device);}catch(error){notice(error.message,true,true);}finally{try{await device.finish();}catch{notice('Transfer ended without confirmation. The keyboard unlocks after 10 seconds. Keep the recovery backup.',true,true);}busy=false;document.body.classList.remove('busy');document.querySelector('.app-shell').inert=false;$('connect').disabled=false;liveMonitor?.resume();render();}
}
$('read').onclick=()=>operation(async device=>{
  const bytes=await device.read(13+selected),m=decodeModule(bytes);
  const local=assembledDraft();
  if(sameBytes(encodeModule(local),bytes)){adoptModule(local);}else adoptModule(m);
  persist();render();notice(`Read Track ${number(selected)} into the draft.`);
});
async function backup(device,prefix='seq-project'){
  const objects=[];
  for(let i=0;i<13;i++){notice(`Backing up keyboard project · ${i===0?'settings':`track ${i} of 12`}…`,false,true);objects.push(await device.read(i));}
  const file=await projectFile(objects);
  if(prefix.startsWith('seq-before-')){try{localStorage.setItem('seq-module-recovery-v1',JSON.stringify(file));$('recovery').hidden=false;}catch{throw Error('Cannot keep a recovery copy in this browser. Export a project backup and free browser storage before installing.');}}
  download(`${prefix}-${new Date().toISOString().replace(/[:.]/g,'-')}.json`,file);return objects;
}
$('backup').onclick=()=>operation(async device=>{await backup(device);notice('Full project backup downloaded. Restore it with tools/seq_backup.py.');});
$('save').onclick=()=>operation(async device=>{const result=await device.request(8);if(result[0]!==0)throw Error('The complete project does not fit on board. The previous save remains. Use Back up keyboard project.');notice('Complete project saved on board.');});
$('install').onclick=()=>{
  if(!connection)return;
  try{const m=assembledDraft();assertSupported(m,capabilities.components);$('install-title').textContent=`Install on Track ${number(selected)}?`;
    const melodic=((capabilities[19]|capabilities[20]<<8)&(1<<selected))!==0;
    if(m.version===2){$('review-summary').textContent=`“${m.name}”: ${m.components.map(c=>c.id).join(' → ')}. Existing notes are preserved. Playback uses the selected components.`;}else $('review-summary').textContent=`“${m.name}” will use the ${m.engine==='euclid'?'Euclid':'Chord Bank'} engine on this track.${m.engine==='chord-bank'&&!melodic?' A melodic layer will be added alongside its existing drums.':''} ${m.engine==='euclid'?'Its rhythm follows the track’s existing loop length.':''}`;
    $('install-dialog').showModal();
  }catch(error){notice(error.message,true);}
};
$('cancel-install').onclick=()=>$('install-dialog').close();
$('confirm-install').onclick=()=>{
  $('install-dialog').close();const target=selected,m=assembledDraft(),bytes=encodeModule(m);
  operation(async device=>{
    assertSupported(m,capabilities.components);
    await device.upload(13+target,bytes,true);
    const snapshot=await backup(device,'seq-before-module-install');
    const previous=snapshot[target+1];
    notice('Installing module and verifying keyboard settings…',false,true);
    try{await device.upload(13+target,bytes);if(!sameBytes(await device.read(13+target),bytes))throw Error('Module verification failed.');}
    catch(error){try{await device.upload(target+1,previous);if(!sameBytes(await device.read(target+1),previous))throw Error('Rollback verification failed.');}catch{throw Error('Install was interrupted and rollback could not be confirmed. Restore the downloaded project backup.');}throw error;}
    capabilities=await device.capabilities();notice(`Installed and verified on Track ${number(target)}. Use Save on board to keep it after power-off.`,false,true);
  });
};
$('ai-prompt').onclick=()=>{
  $('prompt-text').value=`Create a module JSON file for my SEQ keyboard. My musical idea: [describe it here].\n\nUse format "seq-module", the version shown in this example, a lowercase id, a name (max 64 characters), and engine "euclid" or "chord-bank". Modules contain data only, no JavaScript or C. Match this complete example exactly, changing its musical values:\n\n${JSON.stringify(assembledDraft(),null,2)}\n\nChord banks require exactly 12 slots, each with a name, 0–8 unique ascending MIDI notes (0–127), velocity 1–127, and length 1–2048 steps. selected is 0–11, transpose is -24..24, inversion is -3..3. Euclid version 3 adds pitchMode: palette (named pitches with recorded step overrides) or transpose (offsets applied to recorded or picked notes). For Euclid, steps is 1–2048, fills 0..steps, rotation 0..steps-1, probability/accent 0–100, ratchet 0..min(255,steps), root 0–11, pool 0–8 integer offsets (-128..127), pin null or a pool index, and template null unless preserving an imported capture. Return only the JSON file. Do not add fields or executable code.`;
  if(assembledDraft().version===2)$('prompt-text').value=`Create a SEQ module JSON composition, version 2, matching this example exactly. Use exactly two components in order: rhythm.stored or rhythm.euclid, then pitch.track or pitch.chord-bank. Every component has version 1 and params. No executable code, extra fields, or unsupported components. Preserve the parameter structure and bounds in the example; chord notes must be sorted, unique MIDI pitches 0–127, at most eight voices. Chord banks contain twelve slots. Euclidean fills cannot exceed steps, and rotation must be below steps. Chord-bank compositions require ratchet 0. My musical idea: [describe it here]. Return JSON only.\n\n${JSON.stringify(assembledDraft(),null,2)}`;
  $('ai-dialog').showModal();
};
$('close-ai').onclick=()=>$('ai-dialog').close();
$('copy-prompt').onclick=async()=>{try{await navigator.clipboard.writeText($('prompt-text').value);notice('Authoring prompt copied.');}catch{$('prompt-text').select();notice('Select and copy the prompt from the text box.');}};
render();

try{$('recovery').hidden=!localStorage.getItem('seq-module-recovery-v1');}catch{}
$('recovery').onclick=()=>{try{const file=JSON.parse(localStorage.getItem('seq-module-recovery-v1'));if(!file)throw Error('No recovery copy is available.');download('seq-before-module-install-recovery.json',file);}catch(error){notice(error.message,true);}};

manual=setupManual({getDraft:()=>({track:selected,module:assembledDraft()})});

const harmony=setupHarmony($('harmony-dialog'),{
  getGlobalScale:()=>liveReady?liveState?.scale:null,
  bank:track=>tracks[track].drafts['chord-bank'],
  setBankIdentity:(track,slot,identity)=>{Object.assign(tracks[track].drafts['chord-bank'].config.slots[slot],identity);persist();},
  capture:async track=>{let bytes;await operation(async device=>{bytes=await device.read(25+track);notice(`Captured melody from Track ${number(track)}. Playback is stopped.`)});return bytes;},
  readTrack:async track=>{let bytes;await operation(async device=>{bytes=await device.read(track+1);notice(`Read chord recipes from Track ${number(track)}. Playback is stopped.`)});return bytes;},
  install:async(target,bytes,expected)=>{if(!connection){notice('Reconnect the keyboard before creating a chord track.',true);return false;}let success=false;await operation(async device=>{
    await device.upload(target+1,bytes,true);
    const snapshot=await backup(device,'seq-before-chord-track');
    if(expected?.scale&&!sameScale(decodeProjectScale(snapshot[0]),expected.scale))throw Error('Global scale changed after preview. Read the track and preview conformity again.');
    if(expected&&!sameBytes(snapshot[expected.track+1],expected.bytes))throw Error('The source track changed after capture. Read the chord track again before writing.');
    try{await device.upload(target+1,bytes);if(!sameBytes(await device.read(target+1),bytes))throw Error('Chord-track verification failed.');}
    catch(error){try{await device.upload(target+1,snapshot[target+1]);if(!sameBytes(await device.read(target+1),snapshot[target+1]))throw Error('Rollback verification failed.');}catch{throw Error('Could not confirm recovery. Restore the downloaded project backup.');}throw error;}
    capabilities=await device.capabilities();success=true;notice(`Created and verified Track ${number(target)}. Start playback to hear the output; Save on board to keep it.`,false,true);
  });return success;},
  audition:async result=>{
    if(previewMuted){notice('Enable Sound in the header to audition.');return;}
    stopPreviewSound();const generation=previewGeneration;
    try{audio??=new AudioContext();await audio.resume();if(previewMuted||generation!==previewGeneration)return;
      const stepSeconds=60/(liveState?.bpm||120)/4,now=audio.currentTime+.03;
      for(const event of result.events){const oscillator=audio.createOscillator(),gain=audio.createGain(),voice={oscillator,gain},start=now+event.start*stepSeconds,end=now+(event.end+1)*stepSeconds;previewVoices.add(voice);oscillator.type='triangle';oscillator.frequency.value=440*2**((event.pitch-69)/12);gain.gain.setValueAtTime(0,start);gain.gain.linearRampToValueAtTime(.025,start+.015);gain.gain.setValueAtTime(.025,Math.max(start+.015,end-.04));gain.gain.linearRampToValueAtTime(.0001,end);oscillator.connect(gain).connect(audio.destination);oscillator.onended=()=>{previewVoices.delete(voice);oscillator.disconnect();gain.disconnect()};oscillator.start(start);oscillator.stop(end+.01)}
    }catch(error){stopPreviewSound();notice(error.message,true);}
  }
});
function openHarmony(){
  if(busy||document.querySelector('dialog[open]'))return;
  harmony.open(liveReady?liveState.track:selected,{captureAvailable:!!(liveReady&&liveState?.active&&liveState?.harmonySupported&&liveState?.type!==0&&!liveState?.child),chordAvailable:!!(liveReady&&liveState?.active&&liveState?.chordSupported),inversionAvailable:!!liveState?.chordInversionSupported,customAvailable:!!liveState?.chordCustomSupported,scaleBoundAvailable:!!liveState?.chordScaleBoundSupported,chordActive:!!liveState?.chordMode?.enabled,global:liveState?.scale});
}
$('harmony-open').onclick=openHarmony;
document.addEventListener('keydown',event=>{
  if(event.ctrlKey&&event.shiftKey&&event.code==='Space'&&!event.altKey&&!event.metaKey&&!event.target?.closest('input,textarea,select,[contenteditable="true"]')){
    event.preventDefault();if(!event.repeat)openHarmony();
  }
});

$('live-track-mode').onchange=async()=>{
  if(!connection||busy||!liveReady)return;const mode=Number($('live-track-mode').value),track=liveState.track;busy=true;renderLive();
  try{await liveMonitor.pause();await connection.request(23,track,0,Uint8Array.of(mode));notice('Track mode requested. If the keyboard asks, press Y to confirm or N to cancel. Save on board to keep the change.');}
  catch(error){notice(error.status===2?'Finish the keyboard gesture before changing mode.':error.message,true);}
  finally{busy=false;liveMonitor?.resume();renderLive()}
};

// Firmware maintenance serializes backups/checks with live MIDI polling.
async function firmwareMidiTask(fn){
  if(busy)throw Error('Wait for the current keyboard operation to finish.');
  busy=true;const device=connection;render();
  try{await liveMonitor?.pause();return await fn(device);}
  finally{busy=false;liveMonitor?.resume();render();}
}
setupFirmware({button:$('firmware-open'),dialog:$('firmware-dialog'),connected:()=>!!connection,
  backup:()=>firmwareMidiTask(async device=>{
    if(!device)throw Error('Connect the keyboard normally before backing up.');
    if(liveState?.temp?.active)throw Error('Keep or restore TEMP before backing up or installing firmware.');
    try{await backup(device,'seq-before-firmware');}finally{await device.finish();}
  }),
  disconnect:()=>firmwareMidiTask(async device=>{
    stopLive();connection=null;capabilities=null;await device?.close();
    $('connection').textContent='MIDI disconnected for firmware update';$('connection').className='connection';$('connect').textContent='Connect keyboard ↗';
  }),
  verify:release=>firmwareMidiTask(async device=>{
    if(device)return verifyFirmware(device,release);
    if(!navigator.requestMIDIAccess)throw Error('This browser does not expose Web MIDI. Reconnect with a Web MIDI browser.');
    const midi=await navigator.requestMIDIAccess({sysex:true});
    const inputs=[...midi.inputs.values()].filter(p=>p.state==='connected'&&p.name?.includes('Keychron V1 Max'));
    const outputs=[...midi.outputs.values()].filter(p=>p.state==='connected'&&p.name?.includes('Keychron V1 Max'));
    if(inputs.length!==1||outputs.length!==1)throw Error('Reconnect USB normally, then use Connect keyboard to select both V1 Max ports.');
    let candidate;
    try{await inputs[0].open();await outputs[0].open();candidate=new SequencerConnection(inputs[0],outputs[0]);
      const result=await verifyFirmware(candidate,release);capabilities=await candidate.capabilities();connection=candidate;access=midi;
      startLive(candidate);$('connection').className='connection live';$('connection').innerHTML='<i></i> Connected · module v1';$('connect').textContent='Disconnect';
      midi.onstatechange=()=>{if(connection&&(connection.input.state==='disconnected'||connection.output.state==='disconnected')){stopLive();connection.close();connection=null;capabilities=null;$('connection').textContent='Disconnected';$('connect').textContent='Connect keyboard ↗';render();}};
      return result;
    }catch(error){await candidate?.close();throw error;}
  })
});
