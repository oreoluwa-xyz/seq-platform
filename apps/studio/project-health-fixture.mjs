import {renderProjectHealth} from './project-health.mjs?v=23482001a9';
import {renderTemp} from './temp.mjs?v=f3aa4a44c5';
const h={known:true,dirty:true,saved:true,saveStatus:0,track:1,lane:2,lanes:8,laneCapacity:48,noteCapacity:128,notes:24,totalNotes:48,chords:8,chordCapacity:64,cc:40,ccCapacity:512,trackCC:8,trackCCCapacity:128,modifiers:2,modifierCapacity:16,saveBytes:2400,saveCapacity:3072,tempBytes:4100,tempCapacity:7424,restoreBytes:3900,trackBytes:500,transferCapacity:6144,tempReady:true};
const root=document.querySelector('#health'),temp=document.querySelector('#temp');
function render(mode){
 const state={active:true,temp:{active:mode==='temp',queued:false},health:{...h},healthSupported:mode!=='legacy'};
 if(mode==='saved')state.health.dirty=false;
 if(mode==='full')Object.assign(state.health,{saveBytes:4500,tempBytes:8500,tempReady:false,saveStatus:2});
 if(mode==='busy')Object.assign(state.health,{known:false,tempReady:false});
 if(mode==='legacy')state.health=null;
 renderProjectHealth(root,state,mode!=='stale');renderTemp(temp,state,mode!=='stale');
}
const assert=(ok,message)=>{if(!ok)throw Error(message)};
try{
 render('dirty');assert(root.textContent.includes('Unsaved changes')&&root.querySelectorAll('dl>div').length===10,'Resource rows and save state');
 const summary=root.querySelector('summary');summary.focus();render('saved');assert(root.open&&document.activeElement===summary,'Polling preserves disclosure and keyboard focus');
 render('full');assert(root.dataset.tone==='warning'&&root.querySelectorAll('.full').length===2&&temp.dataset.temp==='full','Overflow shown for save and TEMP');
 render('temp');assert(root.textContent.includes('Original drop retained'),'Original TEMP restore point visible');
 render('stale');assert(!root.querySelector('dl').children.length&&!root.textContent.includes('2,400'),'Stale values removed');
 render('legacy');assert(root.textContent.includes('Update keyboard firmware'),'Older firmware guidance');
 document.querySelector('#scenario').onchange=e=>render(e.target.value);render('dirty');
 document.querySelector('#result').textContent='PASS: save states, capacities, TEMP, overflow, stale clearing and focus retention.';document.title='PASS · Project capacity';
}catch(error){document.querySelector('#result').textContent='FAIL: '+error.message;document.title='FAIL · Project capacity';}
