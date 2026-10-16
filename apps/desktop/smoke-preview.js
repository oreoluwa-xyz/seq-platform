(async () => {
    const {renderProjectHealth} = await import('/ui/project-health.mjs');
    const {renderTemp} = await import('/ui/temp.mjs');
    const {renderHardwareOrbit} = await import('/ui/orbit.mjs');
    const {renderLiveSteps} = await import('/ui/steps.mjs');
    const {renderRhythmControls} = await import('/ui/rhythm-controls.mjs');
    const state = {track: 0, child: false, type: 1, engine: 0, active: true, running: true, step: 4, length: 16, bar: 0, pattern: 0x4925, sounding: [65],
        projectedNotes: Array.from({length:16}, (_, i) => 0x4925 & (1 << i) ? [[60,63,65,67][i % 4]] : []),
        euclid: {editable:true, orderedKeys:true, lane:null, euclidean:true, enabled:true, steps:16, fills:6, rotation:0, probability:100, accent:12, generated:0x4925}};
    const {renderPalette}=await import('/ui/palette.mjs');
    const {renderVariation,defaultVariation}=await import('/ui/variation.mjs');
    const {renderScale}=await import('/ui/scale.mjs');
    const {renderPerformance}=await import('/ui/performance.mjs');
    state.scale={enabled:false,root:0,kind:0,mode:0,localRoot:0,localKind:0,direction:0,revision:1,count:11};
    state.palette={editable:true,captureEditable:true,removeSupported:true,chordSupported:true,mode:'palette',overrides:0x3844,saved:[60,63,65,67,70].map(note=>({note,offset:note-60})),root:0,pin:null,replace:false,qualities:Array(8).fill(0),editDigit:null,capture:[],slots:Array.from({length:16},(_,i)=>0x4925&(1<<i)?i%5:255)};
    state.variation={track:0,revision:1,pending:false,melodic:true,current:defaultVariation(),next:defaultVariation(),accumulator:{enabled:false,random:false,run:0,backward:false}};
    state.temp={active:true,queued:true};
    state.visual={mode:"euclid"};
    document.body.dataset.liveMode="euclid";
    document.querySelectorAll("#tracks .track").forEach((node,i)=>{node.classList.toggle("hardware-current",i===0);node.style.setProperty("--hardware-led",i<4?"rgb(230,140,30)":"rgb(12,12,12)");});
    state.health={known:true,dirty:true,saved:true,saveStatus:0,track:0,lane:2,lanes:8,laneCapacity:48,noteCapacity:128,notes:24,totalNotes:48,chords:8,chordCapacity:64,cc:40,ccCapacity:512,trackCC:8,trackCCCapacity:128,modifiers:2,modifierCapacity:16,saveBytes:2400,saveCapacity:3072,tempBytes:4100,tempCapacity:7424,restoreBytes:3900,trackBytes:500,transferCapacity:6144,tempReady:false};
    renderProjectHealth(document.querySelector('#project-health'),state,true);
    renderTemp(document.querySelector('#live-temp'),state,true);
    document.querySelector('#live-content').hidden = false;

    document.querySelector('#live-status').textContent = 'Layout fixture · no hardware connected';
    document.querySelector('#live-transport').textContent = 'Running';
    document.querySelector('#live-type').textContent = 'Melodic';
    document.querySelector('#live-position').textContent = '5 / 16';
    renderHardwareOrbit(document.querySelector('#hardware-orbit'), state, true);
    renderLiveSteps(document.querySelector('#live-steps'), state, true);
    renderRhythmControls(document.querySelector('#live-euclid-settings'), state, true, () => {});
    renderPalette(document.querySelector('#live-palette'),state,true,()=>{},()=>{},()=>{},()=>{});
    renderVariation(document.querySelector('#live-variation'),state,true,async()=>true);
    renderScale(document.querySelector('#live-scale'),state,true,async()=>true,()=>{});
    renderPerformance(state);
    window.performanceFixtureState=state;

})()
