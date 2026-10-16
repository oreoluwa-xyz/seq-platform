import {renderVariation,defaultVariation} from './variation.mjs?v=90308f36a3';
const root=document.querySelector('#live-variation'),result=document.querySelector('#result');
const state={track:0,active:true,running:true,type:1,child:false,scale:{enabled:true,mode:0},variation:{track:0,revision:1,pending:false,melodic:true,current:{...defaultVariation(),enabled:true,order:3,seed:42},next:{...defaultVariation(),enabled:true,order:3,seed:42},accumulator:{enabled:true,random:false,run:4,backward:false}}};
const check=(v,label)=>{if(!v)throw Error(label)};
let calls=[];const render=()=>renderVariation(root,state,true,async(t,p)=>{calls.push([t,Array.from(p)]);return true});
try{
 render();check(!root.hidden&&root.textContent.includes('Reshuffle'),'Melodic controls visible');
 const input=root.querySelector('[data-mutation]');input.focus();input.value='42';input.dispatchEvent(new Event('input'));
 render();check(root.querySelector('[data-mutation]')===input&&input.value==='42'&&document.activeElement===input,'Polling preserves focused input and draft');
 state.variation.revision=2;render();check(root.querySelector('[data-apply]').disabled&&root.textContent.includes('settings changed'),'Stale draft blocked');
 root.querySelector('[data-discard]').click();check(!root.querySelector('[data-apply]').disabled,'Discard refreshes draft');
 const scope=root.querySelector('[data-scope]');scope.value='1';scope.dispatchEvent(new Event('change'));
 const when=root.querySelector('[data-when]');when.value='1';when.dispatchEvent(new Event('change'));
 root.querySelector('[data-reroll]').click();await new Promise(r=>setTimeout(r,0));check(calls.length===1&&calls[0][1].slice(0,5).join(',')==='1,1,1,2,0','Scope and next-bar payload');
 state.variation.pending=true;render();check(!root.querySelector('[data-cancel]').hidden&&root.textContent.includes('Queued'),'Pending status and cancellation');
 state.scale.enabled=false;render();check(root.textContent.includes('Palette range (semitones)'),'Scale-off range label');
 state.track=1;state.variation.track=1;render();check(root.querySelector('[data-mutation]').value==='0','Track switch clears old draft');
 state.child=true;render();check(root.hidden,'Drum child does not expose melodic controls');state.child=false;render();
 result.textContent='PASS · stable inputs, stale drafts, all-track and next-bar actions, pending state, scale context, track switching';document.title='PASS · Variation';
}catch(error){result.textContent=error.stack;document.title='FAIL · Variation';throw error;}
