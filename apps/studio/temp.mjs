export function tempPresentation(state, ready=true) {
  if (!state) return {mode:'offline',label:'TEMP performance',detail:'Connect your keyboard to see TEMP status.',blocked:false};
  if (!ready) return {mode:'unknown',label:'TEMP status unavailable',detail:'Waiting for live keyboard state. Finish TEMP on the keyboard before transferring a project.',blocked:!!state.temp?.active};
  if (!state.temp) return {mode:'unsupported',label:'TEMP shortcuts',detail:'Update keyboard firmware for live TEMP status. Shortcuts may already work on your installed firmware.',blocked:false};
  if (!state.active) return {mode:'off',label:'Sequencer off',detail:'Enter sequencer mode before starting TEMP.',blocked:false};
  if (state.temp.queued) return {mode:'queued',label:'Drop queued · next bar',detail:'Esc blinks purple. Shift + Esc cancels the queued drop and keeps your buildup going.',blocked:true};
  if (state.temp.active) return {mode:'active',label:'TEMP active',detail:'Esc is solid purple. Edits are temporary; saves, backups and module transfers are paused.',blocked:true};
  return {mode:'idle',label:'TEMP ready',detail:'Hold Enter and tap Esc to capture all tracks. Esc turns purple only after the snapshot fits.',blocked:false};
}
export function renderTemp(container,state,ready=true) {
  if (!container.dataset.initialized) {
    container.innerHTML='<div class="temp-heading"><i class="temp-light" aria-hidden="true"></i><strong data-temp-title role="status"></strong><span class="temp-caption">PERFORMANCE / TEMP</span><button type="button" class="temp-help" aria-label="TEMP shortcuts" aria-expanded="false">?</button></div><p data-temp-detail></p><div class="temp-shortcuts"><span><kbd>Enter + Esc</kbd> Start / keep edits</span><span><kbd>Esc</kbd> Restore now</span><span><kbd>Shift + Esc</kbd> Queue / cancel drop</span></div><small>Drop timing follows the global 16-step bar. Keeping edits does not immediately save to flash.</small>';
    container.querySelector('.temp-help').onclick=()=>{const open=container.classList.toggle('temp-expanded');container.querySelector('.temp-help').setAttribute('aria-expanded',String(open));};
    container.dataset.initialized='true';
  }
  const view=tempPresentation(state,ready);
  container.hidden=false;container.dataset.temp=view.mode;
  container.querySelector('[data-temp-title]').textContent=view.label;
  container.querySelector('[data-temp-detail]').textContent=view.detail;
  return view;
}
