export function ccKeyLabel(code){
  if(code>=4&&code<=29)return String.fromCharCode(65+code-4);
  if(code>=30&&code<=38)return String(code-29);
  return ({39:'0',45:'−',46:'=',47:'[',48:']',49:'\\',51:';',52:"'",54:',',55:'.',56:'/'}[code])??`Key ${code}`;
}
const docks=new WeakMap();
export function ccStatus(cc,ready){
  if(!ready)return 'Signal lost · values may be stale';
  const mode=cc.manual?'Manual · recording and playback paused':cc.running?'Automation · playback + recording':'Automation · records when running';
  return `${mode}${cc.temporary?' · temporary tweak':''}`;
}
export function renderCC(root,state,ready){
  const cc=state?.cc,visible=!!(cc?.active&&(cc.latched||document.body.classList.contains('desktop-performance')));
  root.hidden=!visible;
  document.body.classList.toggle('cc-dock-open',visible);
  if(!visible)return;
  let view=docks.get(root);
  if(!view){
    root.innerHTML=`<div class="cc-dock-heading"><div><span class="eyebrow">MIDI CC · LATCHED</span><strong class="cc-target"></strong><span class="cc-status"></span></div><button class="cc-collapse" aria-expanded="true">Minimize</button></div><div class="cc-dock-body"><div class="cc-slots"></div><p class="cc-gesture"></p></div>`;
    view={collapsed:false};docks.set(root,view);
    root.querySelector('.cc-collapse').onclick=()=>{view.collapsed=!view.collapsed;root.querySelector('.cc-dock-body').hidden=view.collapsed;const button=root.querySelector('.cc-collapse');button.textContent=view.collapsed?'Expand':'Minimize';button.setAttribute('aria-expanded',String(!view.collapsed));};
    const align=()=>{
      const workspace=document.querySelector('.live-panel')??document.querySelector('main');
      const bounds=workspace?.getBoundingClientRect(),edge=12;
      const left=Math.max(edge,bounds?.left??edge),right=Math.min(document.documentElement.clientWidth-edge,bounds?.right??document.documentElement.clientWidth-edge);
      root.style.left=`${left}px`;root.style.width=`${Math.max(0,right-left)}px`;
    };
    const observer=new ResizeObserver(()=>{align();document.body.style.setProperty('--cc-dock-space',`${root.getBoundingClientRect().height+32}px`)});
    observer.observe(root);const workspace=document.querySelector('.app-shell');if(workspace)observer.observe(workspace);
    window.addEventListener('resize',align);align();
  }
  root.querySelector('.eyebrow').textContent=cc.latched?'MIDI CC · LATCHED':'MIDI CC · HELD';
  root.classList.toggle('cc-stale',!ready);
  root.querySelector('.cc-target').textContent=`Track ${String(cc.track+1).padStart(2,'0')}${cc.trackOverride?' · held target':''}`;
  root.querySelector('.cc-status').textContent=ccStatus(cc,ready);
  const grid=root.querySelector('.cc-slots');
  while(grid.children.length<cc.slots.length){const cell=document.createElement('div');cell.className='cc-slot';cell.innerHTML=`<div class="cc-slot-label"><kbd></kbd><span></span></div><div class="cc-knob" role="meter" aria-valuemin="0" aria-valuemax="127"><svg viewBox="0 0 56 56" aria-hidden="true"><path class="cc-knob-track" d="M12.44 43.56 A22 22 0 1 1 43.56 43.56" pathLength="100"/><path class="cc-knob-value" d="M12.44 43.56 A22 22 0 1 1 43.56 43.56" pathLength="100"/></svg><div class="cc-knob-face"><i></i><strong></strong></div></div><small></small>`;grid.append(cell)}
  while(grid.children.length>cc.slots.length)grid.lastElementChild.remove();
  cc.slots.forEach((slot,i)=>{
    const cell=grid.children[i],key=ccKeyLabel(slot.keycode),selected=cc.selected===i;
    cell.classList.toggle('selected',selected);cell.classList.toggle('held',slot.held);
    cell.querySelector('kbd').textContent=key;cell.querySelector('span').textContent=`CC ${slot.controller}`;
    cell.querySelector('strong').textContent=String(slot.value);
    const knob=cell.querySelector('.cc-knob');
    knob.style.setProperty('--cc-angle',`${-135+270*slot.value/127}deg`);
    knob.querySelector('.cc-knob-value').style.strokeDashoffset=String(100-100*slot.value/127);
    knob.setAttribute('aria-valuenow',String(slot.value));knob.setAttribute('aria-label',`CC ${slot.controller} value`);
    cell.querySelector('small').textContent=`CH ${slot.channel+1}${selected?' · KNOB':''}`;
    cell.title=`Hold ${key} + turn knob to change CC ${slot.controller}. Shift accelerates. Double-tap and hold for a temporary tweak.`;
  });
  root.querySelector('.cc-status').title=ccStatus(cc,ready);
  const selected=cc.selected===null?null:cc.slots[cc.selected];
  root.querySelector('.cc-gesture').textContent=`${cc.automationSupported?`${cc.automationShiftPgDn?'Shift + PgDn':'T'}: ${cc.manual?'resume automation':'switch to Manual'} · `:''}${selected?`Hold ${ccKeyLabel(selected.keycode)} + turn knob → CC ${selected.controller}`:'Hold a labelled key + turn knob'} · ±3 per turn · Shift: ±15 · Double-tap + hold: temporary · F1–F12: target track · \\: exit CC`;
  root.querySelector('.cc-gesture').title=root.querySelector('.cc-gesture').textContent;
}
