(async () => {
    const {renderHardwareOrbit} = await import('/ui/orbit.mjs');
    const {renderLiveSteps} = await import('/ui/steps.mjs');
    const {renderRhythmControls} = await import('/ui/rhythm-controls.mjs');
    const state = {track: 0, child: false, type: 1, engine: 0, active: true, running: true, step: 4, length: 16, bar: 0, pattern: 0x4925, sounding: [65],
        projectedNotes: Array.from({length:16}, (_, i) => 0x4925 & (1 << i) ? [[60,63,65,67][i % 4]] : []),
        euclid: {editable:true, orderedKeys:true, lane:null, euclidean:true, enabled:true, steps:16, fills:6, rotation:0, probability:100, accent:12, generated:0x4925}};
    document.querySelector('#live-content').hidden = false;
    document.querySelector('#live-palette').hidden = true;
    document.querySelector('#live-status').textContent = 'Layout fixture · no hardware connected';
    document.querySelector('#live-transport').textContent = 'Running';
    document.querySelector('#live-type').textContent = 'Melodic';
    document.querySelector('#live-position').textContent = '5 / 16';
    renderHardwareOrbit(document.querySelector('#hardware-orbit'), state, true);
    renderLiveSteps(document.querySelector('#live-steps'), state, true);
    renderRhythmControls(document.querySelector('#live-euclid-settings'), state, true, () => {});
})()
