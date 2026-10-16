import {paletteNavigationSnapshot,paletteWantsView,paintPaletteSelection} from './palette-links.mjs?v=b388aa571a';
/* Performance panes share the live controls with Workshop; only their layout
 * changes. Tabs stay mounted while MIDI snapshots update their availability. */

const panes=[['rhythm','Rhythm'],['palette','Palette'],['variation','Variation'],['scale','Scale']];
export function renderPerformance(state){
  if(!document.body.classList.contains('desktop-app'))return;
  let nav=document.querySelector('#performance-tabs');
  if(!nav){
    nav=document.createElement('div');nav.view={current:'rhythm',previousTrack:null,previousPalette:null,state:null};nav.id='performance-tabs';nav.setAttribute('role','tablist');nav.setAttribute('aria-label','Live instrument controls');
    for(const [id,label] of panes){
      const button=document.createElement('button');button.type='button';button.dataset.pane=id;button.id=`performance-tab-${id}`;button.textContent=label;button.setAttribute('role','tab');
      button.setAttribute('aria-controls',id==='rhythm'?'live-euclid-settings':`live-${id}`);
      button.onclick=()=>{nav.view.current=id;nav.view.previousPalette=paletteNavigationSnapshot(nav.view.state);renderPerformance(nav.view.state);};
      nav.append(button);
    }
    nav.onkeydown=e=>{
      if(!['ArrowLeft','ArrowRight','Home','End'].includes(e.key))return;
      const buttons=[...nav.querySelectorAll('button:not([hidden])')],index=buttons.indexOf(document.activeElement);
      if(index<0)return;e.preventDefault();
      const next=e.key==='Home'?0:e.key==='End'?buttons.length-1:(index+(e.key==='ArrowRight'?1:-1)+buttons.length)%buttons.length;
      buttons[next].click();buttons[next].focus();
    };
    const readouts=document.querySelector('.live-readouts'),navigation=document.createElement('div');
    navigation.className='performance-navigation';readouts.before(navigation);navigation.append(readouts,nav);
  }
  let rhythmSummary=document.querySelector('#palette-rhythm-summary');
  if(!rhythmSummary){rhythmSummary=document.createElement('div');rhythmSummary.id='palette-rhythm-summary';document.querySelector('.hardware-rhythm>div:nth-child(2)').prepend(rhythmSummary);}
  let selectionStatus=document.querySelector('#palette-selection-status');
  if(!selectionStatus){selectionStatus=document.createElement('span');selectionStatus.id='palette-selection-status';selectionStatus.setAttribute('role','status');rhythmSummary.after(selectionStatus);}
  const view=nav.view;
  if(state!==undefined)view.state=state;else state=view.state;
  rhythmSummary.textContent=state?.euclid?`Euclid · ${state.euclid.fills}/${state.euclid.steps} hits · rotation ${state.euclid.rotation}`:'Live rhythm';
  let {current,previousTrack,previousPalette}=view;
  const available={rhythm:true,palette:!document.querySelector('#live-palette').hidden,variation:!document.querySelector('#live-variation').hidden,scale:!document.querySelector('#live-scale').hidden};
  const nextPalette=paletteNavigationSnapshot(state);
  if(state?.track!==previousTrack){current='rhythm';previousTrack=state?.track;}
  if(paletteWantsView(previousPalette,nextPalette)&&available.palette)current='palette';previousPalette=nextPalette;
  if(!available[current])current='rhythm';
  const selected=state?.scale?.editing&&available.scale?'scale':current;
  Object.assign(view,{current,previousTrack,previousPalette});
  if(document.body.dataset.performancePane!==selected)document.querySelector('#live-content').scrollTop=0;
  document.body.dataset.performancePane=selected;
  for(const [id,label] of panes){
    const button=nav.querySelector(`[data-pane="${id}"]`);button.hidden=!available[id];button.setAttribute('aria-selected',String(id===selected));button.tabIndex=id===selected?0:-1;
    button.textContent=id==='palette'?`Palette · ${state?.palette?.saved?.length??0}`:id==='variation'&&state?.variation?.pending?'Variation · queued':label;
  }
  paintPaletteSelection(document.querySelector('#live-palette'));
  if(document.body.classList.contains('desktop-performance')){
    if(selected==='variation')document.querySelector('#live-variation').open=true;
    if(selected==='scale')document.querySelector('#live-scale').open=true;
  }
}
