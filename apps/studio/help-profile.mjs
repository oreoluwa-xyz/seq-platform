import {decodeFirmwareIdentity} from './firmware.mjs';
export const helpProfiles = {
  unknown: {label:'Unknown firmware',length:false,trackMute:false,known:false},
  legacy: {label:'Earlier controls · v0.1.11',length:false,trackMute:false,known:true},
  length: {label:'Space length · 2026.10.16.1',length:true,trackMute:false,known:true},
  current: {label:'Current controls · whole-track mute',length:true,trackMute:true,known:true}
};
export function identityProfile(identity) {
  if(identity?.target!=='keychron/v1_max/ansi_encoder:seq_test')return 'unknown';
  return ({'2026.10.15.3':'legacy','2026.10.16.1':'length','2026.10.16.2':'current'})[identity.version]||'unknown';
}
export async function readHelpIdentity(device) {
  try {
    const head=await device.request(24),tail=await device.request(24,0,64);
    if(head.length!==64||tail.length!==49)return null;
    return decodeFirmwareIdentity(new Uint8Array([...head,...tail]));
  } catch {return null;}
}
export function spaceGuide(profile) {
  const p=helpProfiles[profile]||helpProfiles.unknown;
  const rows=[['Space + digits','Jump to a bar'],['Space + knob','Swing'],['Space + Enter','Stop and save'],['Space + Home','Stop and change clock source'],['Space + Backspace','Clear mutes']];
  if(!p.known)return [['Firmware unknown','Choose a reference version to see its shortcuts.']];
  rows.unshift([p.length?'Space + F':'Space + =','Follow playhead']);
  if(p.length)rows.push(['Ctrl + Space + knob','Length ±1 step; Shift: ±16'],['Space + =','Double and repeat content'],['Shift + Space + =','Double with empty space']);
  else rows.push(['Tab + =','Double and repeat; both Shifts: empty space']);
  rows.push(['Space + track keys','Individual lane / melody mutes']);
  if(p.trackMute)rows.push(['Shift + Space + F1–F12','Whole-track mute; retains lane mutes']);
  return rows;
}
export function adaptControlHelp(map, mode, profile='current') {
  const p=helpProfiles[profile]||helpProfiles.unknown;
  if(p.length&&p.trackMute)return map;
  // Copy: profile adaptation must never mutate the shared control definitions.
  map=Object.fromEntries(Object.entries(map).map(([key,value])=>[key,{...value}]));
  if(!p.known){
    for(const control of Object.values(map))control.detail='Reference unavailable for this firmware. Choose a known control version above; these labels are not verified for your keyboard.';
    if(mode==='navigation')for(const key of Object.keys(map))map[key]={...map[key],label:'Check version'};
    return map;
  }
  if(mode==='navigation'){
    if(!p.length){delete map.F;map['=']={label:'Follow',group:'parameter',detail:'Space + = toggles following the playhead.'};map.Knob={label:'Swing',group:'parameter',detail:'Space + knob adjusts swing. Ctrl + knob changes tempo. Use track-key length entry or Tab + = duplication.'};}
    if(!p.trackMute)for(let n=1;n<=12;n++)map[`F${n}`].detail='While holding Space, double-tap a melodic track to mute its melody. For drums, tap the track then its lane’s F-key. Independent whole-track mute is unavailable in this version.';
  }
  if(!p.length){
    if(map.Space&&mode!=='navigation')map.Space.detail='Hold Space for navigation, lane mutes, layout, saving and sync. Ctrl + Space opens Scale; Ctrl + Shift + Space opens Harmony. Ctrl + knob changes tempo.';
    for(const key of ['LCtrl','RCtrl'])if(map[key])map[key].detail='Ctrl + knob changes tempo; Shift changes 5 BPM. Ctrl + Space opens Scale.';
  }
  return map;
}
export function setupHelpProfile(root, onChange=()=>{}) {
  const panel=document.createElement('div');panel.className='help-profile';
  const label=document.createElement('label');label.textContent='Control reference ';
  const select=document.createElement('select');select.setAttribute('aria-label','Control reference version');
  for(const [value,title] of [['auto','Connected keyboard / unknown'],...Object.entries(helpProfiles).filter(([id])=>id!=='unknown').map(([id,p])=>[id,p.label])]){const option=document.createElement('option');option.value=value;option.textContent=title;select.append(option);}
  const status=document.createElement('p');status.className='help-profile-status';status.setAttribute('role','status');
  const details=document.createElement('details');details.className='space-guide';const summary=document.createElement('summary');summary.textContent='Space controls';const list=document.createElement('dl');details.append(summary,list);label.append(select);panel.append(label,status,details);root.prepend(panel);
  let identity=null,ready=false,live=null,lastUpdate=null;
  const profile=()=>select.value==='auto'?identityProfile(ready?identity:null):select.value;
  function render(){
    const id=profile();
    status.textContent=(select.value==='auto'?(ready&&identity?`Reported firmware ${identity.version}. `:'No current firmware identity. '):'Manual reference selection. ')+(id==='unknown'?'Shortcuts are unverified; select your control version.':helpProfiles[id].label+'.');
    if(ready&&live)status.textContent+=` Reported features: ${[live.palette?.chordSupported?'chord palette':null,live.variationSupported?'variations':null,live.chordSupported?'chord editor':null].filter(Boolean).join(', ')||'basic live state'}.`;
    list.replaceChildren(...spaceGuide(id).flatMap(([gesture,action])=>{const dt=document.createElement('dt'),dd=document.createElement('dd');dt.textContent=gesture;dd.textContent=action;return [dt,dd];}));
    onChange(id);
  }
  select.onchange=render;render();
  return {profile,element:panel,setReference(value){select.value=Object.hasOwn(helpProfiles,value)&&value!=='unknown'?value:'auto';render();},update(value,isReady,state=null){const signature=JSON.stringify([value,isReady,state?.palette?.chordSupported,state?.variationSupported,state?.chordSupported]);if(signature===lastUpdate)return;lastUpdate=signature;identity=value;ready=isReady;live=state;render();}};
}

export function profileSections(sections, profile) {
  const p=helpProfiles[profile]||helpProfiles.unknown;
  if(p.length&&p.trackMute)return sections;
  return sections.map(section=>{
    if(['Track length with the knob','Navigation And Follow','Mutes','Firmware version and control changes'].includes(section.title)){
      const lines=spaceGuide(profile).map(([key,action])=>`- **${key}**: ${action}`);
      if(p.known&&!p.length)lines.push('', 'For exact length, hold a track key, tap Backspace, type the step count and release. Knob-based length editing is unavailable in this reference.');
      if(p.known&&!p.trackMute)lines.push('', 'Whole-track mute is unavailable. Space-only individual lane and melody mutes remain available.');
      return {...section,lines};
    }
    if(!p.known)return {...section,lines:['**Firmware compatibility is unknown.** The general reference below describes current controls; select a known version for matching Space shortcuts.', '', ...section.lines]};
    if(!p.length)return {...section,lines:section.lines.map(line=>line
      .replace(/`Space` \+ `=`/g,'`Tab` + `=`')
      .replace('Add either Shift for a blank extension.', 'Add both Shift keys for a blank extension.')
      .replace('Add either Shift to leave the new region blank.', 'Add both Shift keys to leave the new region blank.')
      .replace('tap **Ctrl + Space** to open Scale mode on Space release. Turning the knob while held edits track length instead and suppresses the editor.', 'press **Ctrl + Space** to open Scale mode.')
      .replace('by tapping **Ctrl + Shift + Space** (release Space without turning the knob)', 'with **Ctrl + Shift + Space**')
      .replace('With Space held it changes track length instead; otherwise it takes priority over the mode’s knob parameter.', 'It takes priority over the mode’s knob parameter.')
      .replace('unless Space is held for track length', 'in active sequencer modes'))};
    return section;
  });
}

export function renderSpaceHint(hint, identity, state, ready, busy=false) {
  const show=ready&&!busy&&state?.visual?.mode==='navigation';
  hint.hidden=!show;
  if(!show)return;
  const profile=identityProfile(identity);
  hint.textContent=profile==='current'?'SPACE · F Follow · = Repeat · Shift+= Empty · Ctrl+knob Length · Shift+F1–F12 Track mute':profile==='length'?'SPACE · F Follow · = Repeat · Shift+= Empty · Ctrl+knob Length':profile==='legacy'?'SPACE · = Follow · knob Swing · track keys Lane mutes':'SPACE · Firmware unknown — choose a control reference in Manual';
  hint.title=spaceGuide(profile).map(row=>row.join(': ')).join('\n');
}
