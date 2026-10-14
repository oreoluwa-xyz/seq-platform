import {helpFor,guideComponents,moduleFromLive} from './component-help.mjs?v=008213ee62';
import {setupKeyboard} from './keyboard.mjs?v=b9195af0d3';

// A small, text-only renderer for the repository manual's Markdown subset.
// Build DOM nodes rather than interpreting documentation as HTML.
export function manualSections(markdown) {
  const sections=[];let current={title:'Overview',lines:[]};
  for(const line of markdown.replace(/\r/g,'').split('\n')) {
    const heading=line.match(/^(?:#{1,3}\s+(.+)|\*\*([^*]+)\*\*)\s*$/);
    if(heading) {
      const title=heading[1]||heading[2];
      if(title==='Sequencer Manual')continue;
      if(current.lines.some(l=>l.trim()))sections.push(current);
      current={title,lines:[]};
    }else current.lines.push(line);
  }
  if(current.lines.some(l=>l.trim()))sections.push(current);
  return sections;
}
function inline(parent,text) {
  const pattern=/`([^`]+)`|\*\*([^*]+)\*\*|\[([^\]]+)\]\(([^)]+)\)/g;let last=0;
  for(const match of text.matchAll(pattern)) {
    parent.append(document.createTextNode(text.slice(last,match.index)));
    let node;
    if(match[1]!==undefined){node=document.createElement('code');node.textContent=match[1];}
    else if(match[2]!==undefined){node=document.createElement('strong');node.textContent=match[2];}
    else {
      const url=new URL(match[4],new URL('../MANUAL.md',location.href));
      node=document.createElement(['http:','https:'].includes(url.protocol)?'a':'span');node.textContent=match[3];
      if(node.tagName==='A'){node.href=url.href;node.target='_blank';node.rel='noopener';}
    }
    parent.append(node);last=match.index+match[0].length;
  }
  parent.append(document.createTextNode(text.slice(last)));
}
function renderSection(section) {
  const fragment=document.createDocumentFragment(),heading=document.createElement('h3');heading.textContent=section.title;fragment.append(heading);
  const lines=section.lines;let i=0;
  while(i<lines.length){
    const line=lines[i];if(!line.trim()){i++;continue;}
    if(line.startsWith('```')){
      const pre=document.createElement('pre'),code=document.createElement('code'),body=[];i++;
      while(i<lines.length&&!lines[i].startsWith('```'))body.push(lines[i++]);i++;code.textContent=body.join('\n');pre.append(code);fragment.append(pre);continue;
    }
    if(line.startsWith('|')){
      const wrap=document.createElement('div'),table=document.createElement('table');wrap.className='manual-table';let header=true;
      while(i<lines.length&&lines[i].startsWith('|')){
        const cells=lines[i++].replace(/^\||\|\s*$/g,'').split('|').map(c=>c.trim());
        if(cells.every(c=>/^:?-+:?$/.test(c)))continue;
        const tr=document.createElement('tr');for(const cell of cells){const td=document.createElement(header?'th':'td');inline(td,cell);tr.append(td);}table.append(tr);header=false;
      }
      wrap.append(table);fragment.append(wrap);continue;
    }
    const item=line.match(/^(\d+\. |[-*] )(.+)/);
    if(item){
      const ordered=/^\d/.test(item[1]),list=document.createElement(ordered?'ol':'ul');
      if(ordered)list.start=parseInt(item[1],10);
      while(i<lines.length){
        const entry=lines[i].match(ordered?/^\d+\. (.+)/:/^[-*] (.+)/);if(!entry)break;
        let text=entry[1];i++;while(i<lines.length&&/^\s+\S/.test(lines[i]))text+=' '+lines[i++].trim();
        const li=document.createElement('li');inline(li,text);list.append(li);
      }
      fragment.append(list);continue;
    }
    const body=[line];i++;
    while(i<lines.length&&lines[i].trim()&&!/^(\||```|\d+\. |[-*] )/.test(lines[i]))body.push(lines[i++]);
    const p=document.createElement('p');inline(p,body.join(' '));fragment.append(p);
  }
  return fragment;
}
export function setupManual({getDraft}={}){
  const dialog=document.getElementById('manual-dialog'),search=document.getElementById('manual-search'),nav=document.getElementById('manual-sections'),content=document.getElementById('manual-content'),count=document.getElementById('manual-count');
  const keyboard=setupKeyboard();
  let visual=true,trackView=false,hardware=null,hardwareReady=false,guideSignature='';
  const trackButton=document.getElementById('manual-track'),trackContent=document.getElementById('manual-track-content'),source=document.getElementById('manual-track-source');
  function renderTrack(){
    const installed=source.value==='installed',draft=installed?null:getDraft?.();
    const module=installed?hardwareReady?moduleFromLive(hardware):null:draft?.module;
    const track=installed?hardware?.track:draft?.track;
    const signature=JSON.stringify([installed,module,track,hardwareReady]);
    if(signature===guideSignature)return;guideSignature=signature;
    trackContent.replaceChildren();
    const add=(tag,text)=>{const node=document.createElement(tag);node.textContent=text;trackContent.append(node);};
    add('h3',`${installed?'On keyboard':'Local draft'}${track===undefined?'':` · Track ${String(track+1).padStart(2,'0')}`}`);
    add('p',installed?'Uses the keyboard’s current track. Live state identifies its setup; full parameter values are not read here.':'This is the editor’s selected track. Draft edits affect the keyboard only after installation.');
    if(!module){add('p',installed?'Connect the keyboard and wait for live state to see its guide.':'Select a module in the editor to see its guide.');return;}
    if(module.version!==2)add('p',module.engine==='chord-bank'?'Chord-bank performance: audition or stamp chords with the keyboard. Playback follows recorded notes. Generated rhythm examples below apply when you choose a separate rhythm.':'Euclid instrument: rhythm and palette work together with the instrument’s keyboard controls and accumulator.');
    else add('p','Timing and notes are selected independently. Recorded material is preserved; the accumulator is inactive in this composition.');
    for(const component of guideComponents(module)){
      const help=helpFor(component);add('h4',help.title);add('p',help.purpose);
      for(const [label,items] of [['Parameters',help.parameters],['Controls',help.gestures]]){
        if(!items.length)continue;add('strong',label);const list=document.createElement('ul');
        for(const text of items){const item=document.createElement('li');item.textContent=text;list.append(item);}trackContent.append(list);
      }
      if(help.example)add('p',`Try it: ${help.example}`);add('p',help.compatibility);
    }
  }
  source.onchange=()=>{guideSignature='';renderTrack();};
  trackButton.onclick=()=>{showView(false,true);renderTrack();};
  const visualButton=document.getElementById('manual-visual'),writtenButton=document.getElementById('manual-written');
  function showView(value,track=false){
    trackView=track;document.getElementById('manual-track-guide').hidden=!track;trackButton.setAttribute('aria-pressed',String(track));
    visual=value;document.getElementById('keyboard-guide').hidden=!visual;document.getElementById('manual-written-guide').hidden=visual||track;
    visualButton.setAttribute('aria-pressed',String(visual));writtenButton.setAttribute('aria-pressed',String(!visual&&!track));
  }
  visualButton.onclick=()=>showView(true);writtenButton.onclick=()=>{showView(false);search.focus();};
  let sections=null,loading=null,selected=0,returnFocus=null;
  function render(){
    if(!sections)return;
    const query=search.value.trim().toLowerCase(),matches=sections.map((s,i)=>({s,i})).filter(({s})=>`${s.title}\n${s.lines.join('\n')}`.toLowerCase().includes(query));
    if(!matches.some(({i})=>i===selected))selected=matches[0]?.i;
    count.textContent=query?`${matches.length} matching section${matches.length===1?'':'s'}`:`${sections.length} sections`;
    nav.replaceChildren(...matches.map(({s,i})=>{const button=document.createElement('button');button.textContent=s.title;button.setAttribute('aria-current',i===selected?'page':'false');button.onclick=()=>{selected=i;render();content.scrollTop=0;content.focus();};return button;}));
    content.replaceChildren(selected===undefined?document.createTextNode('No matching sections. Try “recording”, “chord”, or a key name.'):renderSection(sections[selected]));content.scrollTop=0;
  }
  async function open(){
    if(dialog.open){dialog.close();return;}
    returnFocus=document.activeElement;dialog.showModal();(trackView?trackButton:visual?visualButton:search).focus();renderTrack();
    if(sections){render();return;}
    content.textContent='Loading the sequencer manual…';
    loading??=fetch('../MANUAL.md',{cache:'no-cache'}).then(response=>{if(!response.ok)throw Error('Manual unavailable.');return response.text();}).then(text=>{sections=manualSections(text);}).finally(()=>loading=null);
    try{await loading;render();}catch{content.textContent='Could not load the manual. Close and reopen to retry, or use “Open source manual” below.';}
  }
  document.getElementById('manual-open').onclick=open;
  document.getElementById('manual-close').onclick=()=>dialog.close();
  dialog.addEventListener('close',()=>{if(returnFocus?.isConnected)returnFocus.focus({preventScroll:true});});
  search.addEventListener('input',render);
  document.addEventListener('keydown',event=>{
    if((event.metaKey||event.ctrlKey)&&!event.altKey&&!event.shiftKey&&(event.code==='Slash'||event.key==='/')){
      event.preventDefault();if(!event.repeat)open();
    }
  });
  return {refresh:()=>{if(dialog.open&&trackView)renderTrack();},updateLive:(state,ready)=>{keyboard.updateLive(state,ready);hardware=state;hardwareReady=ready;if(dialog.open&&trackView)renderTrack();},wantsVisual:()=>dialog.open&&visual&&keyboard.followsLive()};
}
