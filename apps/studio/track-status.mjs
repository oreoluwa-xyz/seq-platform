export function trackMuteStatus(state,ready,index){
  if(!ready||!state?.mutes)return {mode:'unknown',label:'Mute status unavailable',badge:''};
  if(state.mutes.muted&(1<<index))return {mode:'muted',label:'Track muted',badge:'M'};
  if(state.mutes.partial&(1<<index))return {mode:'partial',label:'Some lanes or layers muted',badge:'±'};
  return {mode:'clear',label:'Unmuted',badge:''};
}
export function renderTrackMute(node,state,ready,index){
  const status=trackMuteStatus(state,ready,index);
  node.dataset.mute=status.mode;
  let badge=node.querySelector('.track-mute');
  if(!badge){badge=document.createElement('span');badge.className='track-mute';badge.setAttribute('aria-hidden','true');node.append(badge);}
  badge.replaceChildren();
  const short=document.createElement('span');short.className='track-mute-short';short.textContent=status.badge;
  const full=document.createElement('span');full.className='track-mute-full';full.textContent=status.mode==='muted'?'Muted':status.mode==='partial'?'Partial mute':'';
  badge.append(short,full);
  node.title=`Track ${index+1} · ${status.label}`;
  const label=node.getAttribute('aria-label')||`Track ${index+1}`;
  node.setAttribute('aria-label',label.replace(/ · (Track muted|Some lanes or layers muted|Unmuted|Mute status unavailable)$/,'')+' · '+status.label);
}
