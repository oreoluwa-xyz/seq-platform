// Help is registered by component contract; imported modules only supply data.
export const componentHelp = {
  'rhythm.stored@1': {
    title:'Follow recorded steps',purpose:'Use the timing already recorded on this track.',
    parameters:['There are no additional rhythm parameters. Edit the recorded steps on the keyboard.'],
    gestures:['Add or edit steps using the track’s drum or melodic controls.'],
    example:'Record an uneven phrase, then pair its timing with a chord bank to hear a chord at each recorded melodic note start.',
    compatibility:'With track notes, recorded note lengths are preserved. With a chord bank, melodic note starts trigger one-step chords.'
  },
  'rhythm.euclid@1': {
    title:'Euclidean pulses',purpose:'Space a chosen number of hits around a repeating loop.',
    parameters:['Steps: length of the rhythm. Hits: how many pulses it contains. Rotation: move the pulses around the loop.','Probability: the chance a hit plays. Accent: additional emphasis during playback.','Generate Euclidean hits: turn off to follow recorded timing. Ratchet threshold applies to drums and is unavailable with a chord bank.'],
    gestures:['Edit the hardware rhythm values directly in the live view. In Euclid controls, select Q Steps, W Fills, E Rotation, R Probability or T Accent, then turn the knob; brackets nudge rotation. With a chord bank, Alt/Option remains dedicated to chord controls. Save the project to keep live changes.'],
    example:'Try 5 hits in 16 steps with a rotation of 1. Pair it with a chord bank for repeating harmony, or track notes for a rhythmic variation of a phrase.',
    compatibility:'The pitch source determines which notes each pulse plays: palette notes with recorded overrides, transposed track notes, or a selected bank chord.'
  },
  'pitch.track@1': {
    title:'Track notes + palette',purpose:'Use recorded pitches where present and your picked notes for empty Euclidean steps.',
    parameters:['Palette root: pitch class from 0 (C) to 11 (B). Palette offsets: relative note offsets applied to recorded or picked notes. Pin: select one palette entry; leave blank to cycle.'],
    gestures:['Check the live palette’s layout hint: Windows layout maps Alt to the Command-labelled key beside Space on Mac keycaps. For the Euclid instrument, physically hold Alt/Option + Shift and tap up to eight melodic note keys; release Alt/Option to commit, even when Euclid is latched. In the live palette choose Add notes to fill free slots or Replace to start fresh. Add Ctrl before the first note for a one-capture Replace shortcut. Hold Shift + Alt/Option and tap Delete to clear the palette, including pending notes; recorded steps remain. The first capture replaces the untouched starter palette automatically. Alt/Option + 1–8 pins an entry; Alt/Option + 0 resumes cycling. These gestures do not select chord-bank slots.', 'On a melodic track, hold note keys and tap a step to record notes. Double-tap a populated step to edit it.'],
    example:'Record a sustained chord and add Euclidean pulses to retrigger it without rewriting your recorded notes.',
    compatibility:'Follow recorded steps preserves note lengths and leaves an empty track silent. Drum tracks retain their drum-note behavior.'
  },
  'pitch.track@2': {
    title:'Palette notes + step overrides',purpose:'Play the named palette notes on generated hits; recorded notes override them where present.',
    parameters:['Palette notes: cycle through the named pitches, or pin one slot. The keyboard octave controls set their register.','Transpose phrase: add the palette intervals to recorded or picked notes instead. This preserves the behavior of older modules.'],
    gestures:['Hold Alt/Option + Shift and tap melodic notes; release Alt/Option to commit. Add notes keeps existing entries and skips duplicates; Replace starts fresh. Add Ctrl before the first note for a one-capture Replace shortcut. Hold Shift + Alt/Option and tap Delete to clear the palette, including pending notes; recorded steps remain. The first capture replaces an untouched starter palette automatically. The palette appears only in melodic Euclid controls. Check the live layout hint for the physical capture key.','Use palette on a recorded step clears only that step’s pitches, preserving surrounding sustained notes. Change the live pitch mode without reinstalling the module.'],
    example:'Capture only F3: every empty Euclidean hit plays F3, even if C3 was picked earlier. Record A3 on a step to override the palette there.',
    compatibility:'Follow recorded steps keeps recorded notes and lengths. Palette mode affects melodic Euclidean playback; drum-note behavior is unchanged. Older saved modules open in Transpose phrase.'
  },
  'pitch.chord-bank@1': {
    title:'Chord bank',purpose:'Choose one of twelve chords to supply the notes for generated hits.',
    parameters:['Each slot holds up to eight notes and a velocity. Transpose shifts the chord; inversion changes its voicing.','Slot length sets the duration of stamped or recorded chords. Generated chords use a one-step gate.'],
    gestures:['Alt/Option + 1…0, −, =: select and audition a slot. Release to end audition.','Alt/Option + step: stamp the chord. Alt/Option + encoder: transpose. Add Shift for inversion.'],
    example:'Set a 4-in-16 Euclidean rhythm, then select different bank slots to change the harmony while keeping the pulse.',
    compatibility:'Generated chords use the melodic layer. Drum ratchets cannot be combined with this component. Stamping writes notes; installing a composition preserves existing notes.'
  }
};
export function helpFor(component){
  return componentHelp[`${component.id}@${component.version}`]||{
    title:component.id,purpose:'Help is not available for this component version.',parameters:[],gestures:[],example:'',compatibility:'Consult the component author’s documentation. Controls from another version may differ.'
  };
}
export function guideComponents(module){
  if(module.version===2)return module.components;
  if(module.engine==='euclid')return [{id:'rhythm.euclid',version:1},{id:'pitch.track',version:module.version===3?2:1}];
  return [{id:'pitch.chord-bank',version:1}];
}
export function moduleFromLive(state){
  if(!state)return null;
  const pitch=state.engine===1?'pitch.chord-bank':'pitch.track',modern=state.palette?.editable&&state.palette.mode==='palette';
  return state.composed?{version:2,components:[{id:state.rhythm==='euclid'?'rhythm.euclid':'rhythm.stored',version:1},{id:pitch,version:modern?2:1}]}:{version:modern?3:1,engine:state.engine===1?'chord-bank':'euclid'};
}
