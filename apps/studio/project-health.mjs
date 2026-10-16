export function decodeProjectHealth(bytes) {
  if(bytes.length!==42||bytes[0]!==1||bytes[1]&128||bytes[2]>3||bytes[3]>=12||bytes[4]>=12)throw Error('Invalid project capacity response.');
  const word=i=>bytes[i]|bytes[i+1]<<8,flags=bytes[1];
  const health={dirty:!!(flags&1),tempActive:!!(flags&2),tempQueued:!!(flags&4),transferring:!!(flags&8),known:!!(flags&16),tempReady:!!(flags&32),saved:!!(flags&64),saveStatus:bytes[2],track:bytes[3],lane:bytes[4],lanes:bytes[5],laneCapacity:bytes[6],noteCapacity:bytes[7],notes:word(8),totalNotes:word(10),chords:word(12),chordCapacity:word(14),cc:word(16),ccCapacity:word(18),trackCC:word(20),trackCCCapacity:word(22),modifiers:word(24),modifierCapacity:word(26),saveBytes:word(28),saveCapacity:word(30),tempBytes:word(32),tempCapacity:word(34),restoreBytes:word(36),trackBytes:word(38),transferCapacity:word(40)};
  for(const [used,capacity] of [['lanes','laneCapacity'],['notes','noteCapacity'],['chords','chordCapacity'],['cc','ccCapacity'],['trackCC','trackCCCapacity'],['modifiers','modifierCapacity']]) {
    if(!health[capacity]||health[used]>health[capacity])throw Error('Invalid project resource counts.');
  }
  if(!health.saveCapacity||!health.tempCapacity||!health.transferCapacity||health.totalNotes>12*health.noteCapacity||health.notes>health.totalNotes||health.trackCC>health.cc||health.restoreBytes>health.tempCapacity||health.tempReady&&(!health.known||health.tempActive||health.transferring||health.tempBytes>health.tempCapacity))throw Error('Invalid project capacity limits.');
  return health;
}
const count=(used,limit)=>`${used.toLocaleString()} / ${limit.toLocaleString()}`;
const bytes=(used,limit)=>`${count(used,limit)} bytes`;
export function projectHealthPresentation(state,ready=true) {
  const h=state?.health;
  if(!state)return {label:'Project status',tone:'unknown',detail:'Connect your keyboard to see capacity and on-board save status.',rows:[]};
  if(!ready||!h)return {label:state.healthSupported===false?'Project status unavailable':'Project status pending',tone:'unknown',detail:state.healthSupported===false?'Update keyboard firmware for capacity and save indicators.':'Waiting for a fresh capacity reading. Previous values are hidden.',rows:[]};
  const temp=state.temp?.active??h.tempActive;
  const overSave=h.known&&h.saveBytes>h.saveCapacity,overTemp=h.known&&h.tempBytes>h.tempCapacity;
  const label=temp?'TEMP edits':h.dirty?'Unsaved changes':h.saveStatus===3?'On-board save invalid':h.saved?'Saved on keyboard':'Not saved on keyboard';
  let detail=temp?'Edits are temporary. Keep or restore TEMP before saving.':h.dirty?'Current edits are in keyboard RAM. Save on-board or back up to your computer.':h.saved?'The current project is saved on the keyboard.':'No valid on-board save is confirmed. Save or back up your project.';
  if(h.saveStatus===2)detail+=h.saved?' The last save was too large; the previous save was preserved.':' The last save was too large. No valid on-board save is confirmed.';
  if(h.saveStatus===3)detail+=' The on-board project could not be validated.';
  if(overSave)detail+=' This project needs a computer backup because it exceeds the on-board slot.';
  const row=(label,value,full=false)=>({label,value,full});
  const rows=[
    row('On-board save',h.known?`${bytes(h.saveBytes,h.saveCapacity)} · ${overSave?'too large':'fits'}`:'Size unavailable during edit',overSave),
    row(temp?'Current edits as TEMP':'TEMP snapshot',h.known?`${bytes(h.tempBytes,h.tempCapacity)} · ${overTemp?'too large':'fits'}`:'Size unavailable during edit',overTemp),
    ...(temp?[row('Original drop retained',bytes(h.restoreBytes,h.tempCapacity))]:[]),
    row('Drum lanes',count(h.lanes,h.laneCapacity),h.lanes===h.laneCapacity),
    row(`Track ${h.track+1} notes`,count(h.notes,h.noteCapacity),h.notes===h.noteCapacity),
    row('Notes across all tracks',`${h.totalNotes.toLocaleString()} · each track has its own limit`),
    row('Chord recipes',h.known?count(h.chords,h.chordCapacity):'Unavailable during edit',h.known&&h.chords===h.chordCapacity),
    row('CC points · all tracks',count(h.cc,h.ccCapacity),h.cc===h.ccCapacity),
    row(`Track ${h.track+1} CC points`,count(h.trackCC,h.trackCCCapacity),h.trackCC===h.trackCCCapacity),
    row(`Track ${h.track+1} lane ${h.lane+1} step edits`,count(h.modifiers,h.modifierCapacity),h.modifiers===h.modifierCapacity),
    row(`Track ${h.track+1} transfer`,h.known?bytes(h.trackBytes,h.transferCapacity):'Size unavailable during edit',h.known&&h.trackBytes>h.transferCapacity)
  ];
  return {label,tone:temp?'temp':overSave||h.saveStatus===2||h.saveStatus===3?'warning':h.dirty||!h.saved?'dirty':'saved',detail,rows};
}
export function renderProjectHealth(container,state,ready=true) {
  if(!container.dataset.initialized){
    container.innerHTML='<summary aria-label="Project capacity and save status"><i aria-hidden="true"></i><strong></strong><span>Capacity</span></summary><div class="project-health-body"><p role="status"></p><dl></dl><small>Sampled about once a second. These limits describe keyboard data; computer backup files are separate. Reading status does not stop playback.</small></div>';
    container.dataset.initialized='true';
  }
  const view=projectHealthPresentation(state,ready);
  container.dataset.tone=view.tone;
  container.querySelector('summary strong').textContent=view.label;
  container.querySelector('summary').title=view.label+' · Show project capacity';
  container.querySelector('p').textContent=view.detail;
  container.querySelector('dl').replaceChildren(...view.rows.map(row=>{
    const node=document.createElement('div'),label=document.createElement('dt'),value=document.createElement('dd');
    label.textContent=row.label;value.textContent=row.value;node.classList.toggle('full',row.full);node.append(label,value);return node;
  }));
  return view;
}
