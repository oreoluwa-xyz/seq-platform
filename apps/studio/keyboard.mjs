import {chordLabel} from './chord-mode.mjs?v=baf320581a';
import {scaleNames,rootKeys,rootNames,modeKeys,scaleNotes} from './scale.mjs';
import {keyboardLayout} from './keyboard-layout.mjs?v=e35f4ea181';
import {noteName} from './module.mjs?v=9e9d1bb338';

export const modes = [
  ['chord-step','Chords','Build a progression.','Ctrl + Tab + track selects Chord mode. Note keys choose root; I / O / P / [ / ] select Length / Root / Velocity / Inversion / Type for the knob. Tap a step to place/select, hold to edit. With a scale enabled, Type cycles scale-derived variations. Shift + ] frees or rebinds only the selected chord. Del + step deletes.'],
  ['scale','Global scale','Set the musical key.','Ctrl + Space opens this editor. Note keys choose root; A–J choose modes, K/L harmonic minor/major, semicolon/apostrophe major/minor pentatonic. Enter applies, Esc cancels, 0 toggles global scale.'],
  ['off','Sequencer off','Ready when you are.','Double-tap PgUp on the keyboard to enter the sequencer.'],
  ['confirmation','Confirmation','Confirm the track change.','The keyboard is waiting for Y to confirm or N to cancel.'],
  ['drum','Drums','Build a rhythm.','Tap a step to toggle it. Hold a track key and tap another to choose a drum lane.'],
  ['melodic','Melody','Play it. Place it.','Hold note keys to form a chord, then tap a step. Double-tap a populated step to edit it.'],
  ['euclid','Euclid','Find the pulse.','Hold Alt / Option, or double-tap it to latch. Select a parameter, then turn the knob. Requires a Euclid module.'],
  ['chord','Chord bank','A handful of harmony.','With a Chord Bank module installed, hold Alt / Option to audition a slot or stamp a chord into a step.'],
  ['cc','CC','Shape the sound.','Hold Cmd / Win, or double-tap to latch. Select a controller and turn the knob. Shift + PgDn toggles Automation / Manual for the target track.'],
  ['navigation','Navigation','Move through the pattern.','Hold Space for bar navigation, mutes, sync, swing, and saving.'],
  ['drum-edit','Drum edit','Give every hit a feel.','Double-tap a filled drum step. Choose velocity, ratchets, or spacing, then turn the knob.'],
  ['melodic-edit','Melodic edit','Sculpt the phrase.','Double-tap a populated melodic step. Choose a parameter, then turn the knob.']
];
const steps='A S D F G H J K Z X C V B N M ,'.split(' ');
const grid='1 2 3 4 Q W E R A S D F Z X C V'.split(' ');
const notes='Q 2 W 3 E R 5 T 6 Y 7 U'.split(' ');
const pitches=['C','C♯','D','D♯','E','F','F♯','G','G♯','A','A♯','B'];
const slots='1 2 3 4 5 6 7 8 9 0 - ='.split(' ');
export function controlsFor(mode,os='mac',fourByFour=false,ordered=true,holdEdit=false){
  const map={};const put=(key,label,group,detail)=>map[key]={label,group,detail};
  if(mode!=='off')for(const key of ['LCtrl','RCtrl'])put(key,'Tempo','modifier','Hold Ctrl and turn the knob for global tempo; add Shift for 5 BPM. External MIDI clock keeps tempo in the DAW.');
  if(mode==='off'){put('PgUp','Enter seq','transport','Double-tap PgUp to enter the sequencer.');return map;}
  if(mode==='confirmation'){put('Y','Confirm','transport','Confirm the pending track type change.');put('N','Cancel','transport','Cancel the pending track type change.');return map;}
  if(mode==='scale'){rootKeys.forEach((key,i)=>put(key,rootNames[i],'note',`Choose ${rootNames[i]} as the global root.`));modeKeys.forEach((key,i)=>put(key,scaleNames[i],'parameter',`Choose ${scaleNames[i]}.`));put('Enter','Apply','transport','Apply the pending global key and scale.');put('Esc','Cancel','transport','Discard pending changes.');put('0','Scale on/off','modifier','Toggle global scale constraint.');return map;}
  const alt=os==='mac'?['Mod1']:['Mod2','Mod3'],gui=os==='mac'?['Mod2','Mod3']:['Mod1'];
  const modifier=(keys,label,detail)=>keys.forEach(k=>put(k,label,'modifier',detail));
  put('Esc','Back / exit','transport','Tap to leave an edit mode. Double-tap to exit the sequencer.');
  put('PgUp','Seq on/off','transport','Double-tap to enter or exit the sequencer.');
  put('PgDn','Arm record','transport','Double-tap to arm or disarm recording. Armed notes are captured while transport runs.');
  put('Home','Start / stop','transport','Toggle transport from the start in master mode. Use DAW transport when following external clock.');
  for(let n=1;n<=12;n++)put(`F${n}`,`Track ${String(n).padStart(2,'0')}`,'track',`Select track ${n}. Hold a drum track and tap another track key to select its lane. Hold Tab + track for melodic, Caps + track for drum; confirm an active-track conversion with Y or cancel with N.`);
  modifier(alt,'Euclid / bank','Hold for the installed module: Euclid controls or Chord Bank. Double-tap to latch Euclid.');
  modifier(gui,'CC mode','Hold for CC mode; double-tap to latch. Hold Shift on entry for temporary changes that revert on exit.');
  put('Space','Navigation','modifier','Hold Space for navigation, mute, layout, saving and sync controls. Ctrl + Space edits the global scale; Ctrl + Shift + Space opens the UI harmony workshop.');
  if(['drum','melodic','chord','drum-edit','melodic-edit','chord-step'].includes(mode)){
    const used=mode.startsWith('drum')&&fourByFour?grid:steps;
    used.forEach((key,i)=>put(key,String(i+1).padStart(2,'0'),'step',`Step ${i+1} of the viewed bar. `+(mode==='chord'?'Alt / Option + step stamps the selected bank chord.':mode.startsWith('drum')?'Tap to toggle. Hold a filled step + knob for velocity; release to finish. Double-tap for the full editor.':'Hold notes and tap to place or toggle those pitches. With no notes held, tap to erase on release, or hold the step + knob to change its length. While held, tap I Length, O Pitch (in the active scale), or P Velocity. Release to finish.')));
  }
  if(mode==='melodic'||mode==='melodic-edit'||mode==='chord-step'){
    notes.forEach((key,i)=>put(key,pitches[i],'note',mode==='melodic'?'Hold this pitch to build a chord. Pitch follows the current octave.':'Toggle this pitch in the edited melodic event.'));
    if(mode==='melodic'){
      put('1','Octave −','parameter','Lower the melodic octave.');put('8','Octave +','parameter','Raise the melodic octave.');
      put('4','Rest / tie','parameter','Toggle rest/tie; Shift + 4 arms a tie.');put('`','Octave nudge','parameter','Hold to nudge the octave upward; Shift + ` nudges downward. Release to return.');
    }
  }
  if(mode==='drum'||mode==='melodic'){
    put('Tab','Set melodic','modifier','Hold Tab + a track to set its type to melodic. Tab + = doubles the track and copies content; add both Shift keys for a blank extension.');
    put('Caps','Set drum','modifier','Hold Caps + a track to set its type to drum. Confirm with Y or cancel with N when prompted.');
    put('Del','Clear track','modifier','Hold Del + a track to clear its selected lane. Add both Shift keys to clear the whole drum track.');
  }
  if(mode==='euclid'){
    modifier(alt,'Hold / latch','Hold to enter Euclid controls; double-tap to latch. Drum settings belong to the selected lane. While latched, hold the current track F key and tap another F key to choose a lane.');
    for(const [key,label,detail] of (ordered? [['Q','Steps','Set Euclid step count.'],['W','Fills','Set the hit count.'],['E','Rotation','Set pattern rotation.'],['R','Probability','Set trigger probability. Shift uses ten-percent increments.'],['T','Accent','Alternate velocity boost and reduction, 0–48 MIDI velocity points.'],['F','Ratchet','Set the drum auto-ratchet threshold; zero disables it.']] : [['Q','Fills','Set the hit count. Shift + knob while fills is selected adjusts velocity swing.'],['W','Rotation','Set pattern rotation.'],['E','Steps','Set Euclid step count.'],['P','Probability','Set trigger probability. Shift uses ten-percent increments.'],['F','Ratchet','Set the auto-ratchet threshold; zero disables it.']]))put(key,label,'parameter',detail+' Select, then turn the knob.');
    put(ordered?'[':'R','Rotate −','parameter','Rotate one step backward.');put(ordered?']':'T','Rotate +','parameter','Rotate one step forward.');
    slots.slice(0,8).forEach((key,i)=>put(key,`Palette ${i+1}`,'note',`Alt / Option + ${key} pins playback to palette slot ${i+1}.`));
    put('0','Cycle notes','note','Alt / Option + 0 returns to cycling through the palette.');
    put('Tab','Capture','parameter','Alt / Option + Tab snapshots the current 16-step window into a Euclid template.');
    modifier(['LShift','RShift'],'Palette / fast','Alt / Option + Shift + melodic note keys captures up to eight palette notes. Also modifies knob changes.');
    modifier(['Ctrl','RCtrl'],'Replace palette','Hold Ctrl + Shift + Alt / Option before tapping melodic notes to replace the palette on release. This leaves the UI capture choice unchanged.');
    put('Del','Clear / reset','modifier','While Euclid is latched, hold Delete and double-tap Alt / Option to reset the selected generator, keeping recorded notes. Set Fills to zero to silence generated hits. On melody, Shift + Alt / Option + Delete clears the note palette.');
    put('Knob','Adjust','parameter','Turn to adjust the selected Euclid parameter.');
  }
  if(mode==='chord'){
    modifier(alt,'Hold bank','Hold Alt / Option to use Chord Bank controls.');
    slots.forEach((key,i)=>put(key,`Chord ${i+1}`,'note',`Alt / Option + ${key} selects and auditions chord slot ${i+1}. Release to end audition. While armed and running, records with its configured duration.`));
    put('Knob','Transpose','parameter','Alt / Option + knob transposes. Add Shift to change inversion.');
    modifier(['LShift','RShift'],'Inversion','Alt / Option + Shift + knob changes chord inversion.');
  }
  if(mode==='cc'){
    modifier(gui,'Hold / latch','Hold Cmd / Win for CC mode; double-tap to latch.');
    [...slots,'Q','W','E','R'].forEach((key,i)=>put(key,`CC ${i+16}`,'parameter',`Default controller ${i+16}. Select and turn the knob (0–127). Double-tap for a temporary tweak that reverts on release. Channel follows the target track.`));
    put('\\','Latch CC','modifier','Toggle latched CC mode.');put('Knob','Value','parameter','Turn to change the selected CC value. Automation records while running; Manual sends live tweaks without recording or playback.');
    put('PgDn','Record / CC','transport','Double-tap to arm/disarm note recording. In CC mode, hold either Shift and tap once to toggle Automation / Manual without changing note-record arming. Requires firmware 2026.10.12.1 or later.');
    put('T','Auto / Manual','parameter','Toggle CC recording and playback for this target track. Manual keeps saved automation, and T resumes it. Requires CC automation-mode firmware.');
  }
  if(mode==='navigation'){
    put('Space','Hold','modifier','Keep Space held while using the highlighted navigation controls.');
    '1 2 3 4 5 6 7 8 9 0'.split(' ').forEach((key,i)=>put(key,`Bar ${i+1}`,'parameter','Space + digit jumps to this bar. Rapid taps multiply; Shift doubles the target.'));
    put('=','Follow','parameter','Space + = toggles following the playhead.');put('Backspace','Unmute all','parameter','Space + Backspace clears all track and lane mutes.');
    put('S','Step layout','parameter','Hold Space, tap S, then 1 for the drum 4×4 grid or ` for the default two-row grid. Melody always uses two rows.');
    put('Enter','Save','transport','Space + Enter saves the project on board if it fits. Esc red/amber warns of a failed save; keep a computer backup for large projects.');
    put('Home','Clock source','transport','Space + Home toggles internal MIDI master / DAW follower.');put('Knob','Swing','parameter','Space + knob adjusts internal timing swing from 0 to 50%.');
    for(let n=1;n<=12;n++)put(`F${n}`,`Mute ${n}`,'track','While holding Space, double-tap a melodic track to mute/unmute. For drums, tap the drum track then the lane’s track key.');
  }
  if(mode.endsWith('-edit')){
    const labels=mode==='drum-edit'?['Velocity','Ratchets','Spacing']:['Length','Pitch','Velocity'];
    ['I','O','P'].forEach((key,i)=>put(key,labels[i],'parameter',`Select ${labels[i].toLowerCase()}, then turn the knob.`));
    put('Knob','Adjust','parameter','Turn to adjust the selected step parameter.');
    if(mode==='melodic-edit')for(const [key,label,detail] of [['[','Direction −','Set accumulator direction downward.'],[']','Direction +','Set accumulator direction upward.'],[';','Run length','Cycle accumulator run length.'],["'",'Random','Toggle accumulator randomness.'],['/','Accumulator','Toggle the accumulator.']])put(key,label,'parameter',detail);
  }
  if(mode==='chord'){
    for(const key of [...Array.from({length:12},(_,i)=>`F${i+1}`),'PgUp','PgDn','Home',...gui]){
      map[key].detail='Release Alt / Option first. '+map[key].detail;
    }
  }
  if(mode==='chord-step'){
    notes.forEach((key,i)=>put(key,pitches[i],'note','Choose and audition this chord root. Hold the root + tap a step to stamp/replace. While holding a step, this edits its root.'));
    steps.forEach((key,i)=>put(key,String(i+1).padStart(2,'0'),'step','Tap an empty step to place the current chord; tap a chord to select it. Hold a step or sustain tail to edit the whole chord. Del + step deletes.'));
    ['I','O','P','[',']'].forEach((key,i)=>put(key,['Length','Root','Velocity','Inversion','Type'][i],'parameter','Select this parameter, then turn the knob. Works while a step is held.'+(key===']'?' With a scale enabled, cycles Triad, Seventh, Ninth, Sixth and Add9. Shift + ] toggles Scale-bound / Free for the selected chord.':'')));
    put('Knob','Chord edit','parameter','Adjust Root, Type, Length, Inversion or Velocity. Each held step starts on Length.');
    put('Del','Delete chord','modifier','Hold Delete and tap a chord onset or tail to remove the whole chord.');
    put('Tab','Set melodic','modifier','Tab + track returns to Melody. Ctrl + Tab + track selects Chord mode.');
    modifier(alt,'','Euclid chord timing is not supported in this slice. Use recorded chord steps.');
    modifier(['LShift','RShift'],'Length ×4','With Length selected, Shift changes four steps per knob turn.');
    modifier(['LCtrl','RCtrl'],'Tempo / chord','Ctrl + knob changes global tempo; add Shift for 5 BPM. Ctrl + Tab + F1–F12 selects Chord mode. Ctrl + Space opens the global scale editor.');
    put('PgDn','Record notes','transport','Double-tap arms the ordinary melodic recorder. Use step placement to create grouped chords.');
  }
  if(holdEdit){
    for(const [key,control] of Object.entries(map))if(!['Knob','Esc',...(mode==='melodic-edit'?['I','O','P']:[])].includes(key))control.detail='Release the held step before using this control. '+control.detail;
    put('Knob',mode==='drum-edit'?'Velocity':'Step edit','parameter',mode==='drum-edit'?'Adjust Normal, Accent (+20) or Ghost (−40). Release the step to finish.':'Starts on Length. Keep the step held and tap I Length, O Pitch (in the active scale), or P Velocity, then turn. Release to finish.');
  }
  return map;
}
// Semantic marks are separate from sampled LED colors; note marks indicate pitch class.
export function keyboardMarks(state,mode,gridEnabled=false){
  const marks=Object.fromEntries(keyboardLayout.map(key=>[key.id,{}]));
  if(!state)return marks;
  if(state.active){
    marks[`F${state.track+1}`].current=true;
    const controls=controlsFor(mode,'mac',gridEnabled),keys=mode.startsWith('drum')&&gridEnabled?grid:steps;
    keys.forEach((key,i)=>{if(controls[key]?.group!=='step')return;const absolute=state.bar*16+i;marks[key].hit=!!(state.pattern&(1<<i))&&absolute<state.length;marks[key].playhead=state.running&&state.step===absolute;if(state.type!==0&&!state.child&&state.stepNotes&&absolute<state.length)marks[key].notes=state.stepNotes[i];});
    if(mode==='melodic'||mode==='melodic-edit')notes.forEach((key,i)=>{marks[key].selected=state.selected.some(n=>n%12===i);marks[key].sounding=state.sounding.some(n=>n%12===i);});
    if(mode==='chord-step'&&state.chordMode?.enabled){notes.forEach((key,i)=>{marks[key].selected=state.chordMode.root%12===i;marks[key].hit=state.chordMode.notes.some(n=>n%12===i);marks[key].notes=state.chordMode.notes.filter(n=>n%12===i);marks[key].sounding=state.sounding.some(n=>n%12===i);});steps.forEach((key,i)=>{const c=state.chordMode.steps[i];if(c)marks[key].chordLabel=(c.onset?'':'↳ ')+chordLabel(c);if(c&&c.start===state.chordMode.selected)marks[key].selected=true;});}
    if(mode==='chord'&&state.chord!==255)marks[slots[state.chord]].selected=true;
    if(mode==='scale'&&state.scale){const s=state.scale.editing?state.scale.pending:state.scale;rootKeys.forEach((key,i)=>{marks[key].selected=i===s.root;marks[key].hit=scaleNotes(s.root,s.kind).includes(i)});marks[modeKeys[s.kind]].selected=true;}
    if(mode.endsWith('-edit')&&Number.isInteger(state.editStep)&&state.editStep>=state.bar*16&&state.editStep<state.bar*16+16)marks[keys[state.editStep-state.bar*16]].selected=true;
    if(state.visual?.holdEdit&&mode==='melodic-edit')notes.forEach((key,i)=>{const selected=state.selected.filter(n=>n%12===i);if(selected.length)marks[key].notes=selected});
    marks.Home.running=state.running;marks.PgDn.armed=state.armed;
  }
  if(state.visual?.leds)for(const key of keyboardLayout){const [row,col]=key.matrix;const offset=(row*16+col)*3;marks[key.id].rgb=Array.from(state.visual.leds.slice(offset,offset+3));}
  return marks;
}
export function setupKeyboard(){
  const root=document.getElementById('keyboard-guide');
  let mode='drum',selected='F1',hardware=null,ready=false;
  const held=()=>!!(root.querySelector('#keyboard-live').checked&&ready&&hardware?.visual?.holdEdit);
  const ordered=()=>!root.querySelector('#keyboard-live').checked||!hardware?true:!!hardware.euclid?.orderedKeys;
  const board=root.querySelector('.keyboard-keys'),detail=root.querySelector('.keyboard-detail');
  const buttons=new Map();
  const labelFor=id=>({LShift:'Shift',RShift:'Shift',RCtrl:'Ctrl',Mod1:root.querySelector('#keyboard-os').value==='mac'?'Option':'Win',Mod2:root.querySelector('#keyboard-os').value==='mac'?'Cmd':'Alt',Mod3:root.querySelector('#keyboard-os').value==='mac'?'Cmd':'Alt'})[id]||id;
  function inspect(id){
    selected=id;const control=controlsFor(mode,root.querySelector('#keyboard-os').value,root.querySelector('#keyboard-grid').checked,ordered(),held())[id];
    for(const [key,button] of buttons){button.setAttribute('aria-pressed',String(key===id));button.tabIndex=key===id?0:-1;}
    detail.replaceChildren();const cap=document.createElement('span');cap.className='detail-key';cap.textContent=labelFor(id);const text=document.createElement('div'),title=document.createElement('strong'),description=document.createElement('p');
    title.textContent=control?.label||'No mode-specific control';description.textContent=control?.detail||'This key has no dedicated control in this view. Other held-key combinations may still apply.';
    const mark=keyboardMarks(root.querySelector('#keyboard-live').checked&&ready?hardware:null,mode,root.querySelector('#keyboard-grid').checked)[id];
    if(mark.notes)description.textContent+=` Stored notes: ${mark.notes.map(noteName).join(', ')||'none'}.`;
    text.append(title,description);detail.append(cap,text);
  }
  for(const key of keyboardLayout){
    const button=document.createElement('button');button.className='keyboard-key'+(key.id==='Knob'?' keyboard-knob':'');button.style.cssText=`left:${key.x/16.25*100}%;top:${key.y/6.5*100}%;width:${(key.w-.09)/16.25*100}%;height:${.88/6.5*100}%`;
    button.onclick=()=>inspect(key.id);
    button.onkeydown=event=>{if(!['ArrowLeft','ArrowRight','ArrowUp','ArrowDown','Home','End'].includes(event.key))return;event.preventDefault();let next;
      if(event.key==='Home'||event.key==='End')next=keyboardLayout[event.key==='Home'?0:keyboardLayout.length-1];
      else{const horizontal=['ArrowLeft','ArrowRight'].includes(event.key),sign=['ArrowLeft','ArrowUp'].includes(event.key)?-1:1;next=keyboardLayout.filter(k=>sign*((horizontal?k.x:k.y)-(horizontal?key.x:key.y))>0).sort((a,b)=>{const score=k=>Math.abs((horizontal?k.y:k.x)-(horizontal?key.y:key.x))*4+Math.abs((horizontal?k.x:k.y)-(horizontal?key.x:key.y));return score(a)-score(b);})[0];}
      if(next){inspect(next.id);buttons.get(next.id).focus();}
    };buttons.set(key.id,button);board.append(button);
  }
  function render(){
    const config=modes.find(m=>m[0]===mode),map=controlsFor(mode,root.querySelector('#keyboard-os').value,root.querySelector('#keyboard-grid').checked,ordered(),held());
    root.querySelector('.keyboard-mode-title').textContent=config[2];root.querySelector('.keyboard-mode-help').textContent=held()?(mode==='drum-edit'?'Hold the step and turn the knob for Velocity. Release to finish.':'Hold the step and turn the knob for Length. Tap I Length, O Pitch or P Velocity while held. Release to finish.'):config[3];
    root.querySelector('.keyboard-grid-option').hidden=mode!=='drum';
    root.querySelectorAll('[data-mode]').forEach(b=>b.setAttribute('aria-pressed',String(b.dataset.mode===mode)));
    for(const [id,button] of buttons){const control=map[id];button.dataset.group=control?.group||'unused';button.replaceChildren();const physical=document.createElement('span'),action=document.createElement('strong');physical.textContent=labelFor(id);action.textContent=control?.label||'·';button.append(physical,action);button.setAttribute('aria-label',`${labelFor(id)}: ${control?.label||'No mode-specific control'}`);}
    inspect(selected);paintLive();
  }
  for(const [id,title] of modes){const button=document.createElement('button');button.textContent=title;button.dataset.mode=id;button.onclick=()=>{root.querySelector('#keyboard-live').checked=false;mode=id;render();};root.querySelector('.keyboard-modes').append(button);}
  const follow=root.querySelector('#keyboard-live');
  function paintLive(){
    const following=follow.checked&&!!hardware,live=following&&ready;
    root.classList.toggle('keyboard-following',live);root.classList.toggle('keyboard-stale',following&&!ready);
    const marks=keyboardMarks(live?hardware:null,mode,root.querySelector('#keyboard-grid').checked);
    const currentControls=controlsFor(mode,root.querySelector('#keyboard-os').value,root.querySelector('#keyboard-grid').checked,ordered(),held());
    for(const [id,button] of buttons){
      const mark=marks[id];
      for(const state of ['current','hit','playhead','selected','sounding','running','armed'])button.classList.toggle(`key-${state}`,!!mark[state]);
      button.classList.toggle('key-led',!!mark.rgb);button.style.setProperty('--led-color',mark.rgb?`rgb(${mark.rgb.join(',')})`:'transparent');
      const control=currentControls[id];
      const names=mark.notes?.map(noteName);
      const action=mark.chordLabel??(names?.length?`${names[0]}${names.length>1?` +${names.length-1}`:''}`:control?.label||'·');
      if(button.querySelector('strong').textContent!==action)button.querySelector('strong').textContent=action;
      const annotations=[mark.current?'current track':'',mark.hit?'step populated':'',names?`stored notes: ${names.join(', ')||'none'}`:'',mark.playhead?'playhead':'',mark.selected?'selected pitch or chord':'',mark.sounding?'pitch class sounding':'',mark.armed?'record armed':''].filter(Boolean);
      const label=`${labelFor(id)}: ${control?.label||'No mode-specific control'}${annotations.length?', '+annotations.join(', '):''}`;
      if(button.getAttribute('aria-label')!==label)button.setAttribute('aria-label',label);
      button.title=label;
    }
    const status=!follow.checked?'Reference · choose a mode':!hardware?'Connect to see live state':!ready?'Live updates paused or unavailable':hardware.visual?(hardware.visual.leds?'Live · firmware LED colors':'Live · modes; LED data unavailable'):'Basic live · firmware update needed for modes / LEDs';
    const statusNode=root.querySelector('#keyboard-live-status');if(statusNode.textContent!==status)statusNode.textContent=status;
    root.querySelector('#keyboard-live-position').textContent=live?`TRACK ${String(hardware.track+1).padStart(2,'0')} · ${!hardware.active?'SEQ OFF':hardware.running?'RUNNING':'STOPPED'}${hardware.armed?' · REC ARMED':''} · ${hardware.step+1}/${hardware.length}${hardware.visual?.holdEdit?` · HOLD EDIT${hardware.type!==0?' · '+hardware.selected.map(noteName).join(' / '):' · VELOCITY'}`:''}`:'';
    root.querySelector('#keyboard-os').disabled=live&&!!hardware.visual?.os;root.querySelector('#keyboard-grid').disabled=live&&!!hardware.visual;
    inspect(selected);
  }
  function updateLive(state,isReady){
    const previousHold=hardware?.visual?.holdEdit;
    hardware=state;ready=isReady;
    if(follow.checked&&hardware&&ready){
      const nextMode=hardware.visual?.mode||(!hardware.active?'off':hardware.editStep!==65535?(hardware.type===0||hardware.child?'drum-edit':'melodic-edit'):hardware.type===0||hardware.child?'drum':'melodic');
      let changed=mode!==nextMode||previousHold!==hardware.visual?.holdEdit;mode=nextMode;
      if(hardware.visual){
        const os=root.querySelector('#keyboard-os'),layout=root.querySelector('#keyboard-grid');
        changed||=layout.checked!==hardware.visual.grid||!!hardware.visual.os&&os.value!==hardware.visual.os;
        layout.checked=hardware.visual.grid;if(hardware.visual.os)os.value=hardware.visual.os;
      }
      if(changed){render();return;}
    }
    paintLive();
  }
  follow.onchange=()=>updateLive(hardware,ready);
  root.querySelector('#keyboard-os').onchange=render;root.querySelector('#keyboard-grid').onchange=render;render();
  return {updateLive,followsLive:()=>follow.checked};
}
