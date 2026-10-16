import assert from 'node:assert/strict';
import {decodeLive,decodeVisual,decodeDetailed,decodeRhythm,decodePalette,decodeCC,LiveMonitor} from './live.mjs?v=b5fe2a212b';
import {spawn} from 'node:child_process';
import {createInterface} from 'node:readline';
import {defaultModule,validateModule,encodeModule,decodeModule,composeModule,splitModule,rhythmComponent,assertSupported,chordNotes,parseNotes,checksum} from './module.mjs?v=8c4bcd2da4';
import {SequencerConnection,sameBytes,projectFile} from './midi.mjs?v=3103146cad';
for(const engine of ['euclid','chord-bank']) {
  const m=defaultModule(engine),bytes=encodeModule(m),decoded=decodeModule(bytes);
  assert(sameBytes(encodeModule(decoded),bytes));
  for(let i=0;i<bytes.length;i++)assert.throws(()=>decodeModule(bytes.slice(0,i)));
  const wrong=structuredClone(m);wrong.config.javascript='alert(1)';assert.throws(()=>validateModule(wrong));
}
assert.deepEqual(parseNotes('C4 E4 G4 Bb4 60'),[60,64,67,70]);
assert.throws(()=>parseNotes('NaN'));assert.throws(()=>parseNotes('128'));
const m=defaultModule();m.config.transpose=12;m.config.inversion=1;assert.deepEqual(chordNotes(m),[64,67,71,72]);
const invalid=defaultModule();invalid.config.slots[0].notes=[60,60];assert.throws(()=>validateModule(invalid));
assert.equal(Buffer.from(checksum(new TextEncoder().encode('hello'))).toString('hex'),'ab2c9f4f');
// macOS can expose a SysEx reply as many small MIDI events.
for (const fragmentSize of [1,2,3,54]) {
  const input=new EventTarget();
  const output={send:()=>{const bytes=[0xF0,0x7D,83,69,81,1,74,0,0,0,0,1,0,0xF7];
    for(let i=0;i<bytes.length;i+=fragmentSize){const event=new Event('midimessage');event.data=Uint8Array.from(bytes.slice(i,i+fragmentSize));input.dispatchEvent(event);}
  }};
  const device=new SequencerConnection(input,output);
  assert.deepEqual(await device.request(10),Uint8Array.of(1));device.close();
}
const project=await projectFile([Uint8Array.of(1,2,3)]);assert.equal(project.objects[0].data,'010203');assert.equal(project.objects[0].sha256.length,64);
if(process.argv[2]) {
  const engine=spawn(process.argv[2],['--bridge'],{stdio:['pipe','pipe','inherit']});
  const input=new EventTarget();
  const lines=createInterface({input:engine.stdout});
  lines.on('line',line=>{const bytes=Uint8Array.from(Buffer.from(line,'hex'));for(let i=0;i<bytes.length;i+=3){const event=new Event('midimessage');event.data=Uint8Array.from([...bytes.slice(i,i+3),0xF8]);input.dispatchEvent(event);}});
  const output={send:bytes=>engine.stdin.write(Buffer.from(bytes).toString('hex')+'\n')};
  const device=new SequencerConnection(input,output);
  try {
    const capabilities=await device.capabilities();assert.equal(capabilities[0],1);
    const {decodeProjectHealth}=await import('./project-health.mjs?v=23482001a9');
    const health=decodeProjectHealth(await device.request(25,0));assert.equal(health.track,0);assert.equal(health.known,true);assert.equal(health.transferring,false);assert(health.saveBytes>0);
    const status=decodeLive(await device.request(11));assert.equal(status.track,0);assert.equal(status.transferring,false);assert.equal(status.active,true);
    const selected=decodeLive(await device.request(12,2));assert.equal(selected.track,2);assert.equal(selected.transferring,false);
    const visualMonitor=new LiveMonitor(device,{wantVisual:()=>true,onState:()=>{},onStatus:()=>{}});
    visualMonitor.stopped=false;
    const visual=await visualMonitor.readState(null);
    visualMonitor.stop();
    assert.equal(visual.globalLoopSupported,true);assert.equal(visual.loopSteps,0);assert.equal(visual.stepNotes.length,16);assert.deepEqual(visual.stepNotes[0],[]);assert.equal(visual.track,2);assert.equal(visual.visual.mode,'drum');assert.equal(visual.transferring,false);assert.equal(visual.chord,255);assert.equal(visual.chordMode.enabled,false);assert.equal(visual.chordMode.track,2);
    await device.request(12,0);
    const previous=await device.read(1);
    const wrapper=previous.slice();wrapper[2]=1;wrapper[3]=1;await device.upload(1,wrapper);
    for(const kind of ['euclid']) {
      const module=defaultModule(kind);if(kind==='chord-bank'){module.config.inversion=-2;module.config.transpose=-7;module.config.slots[11].notes=[];}
      const bytes=encodeModule(module);
      await device.upload(13,bytes,true);await device.upload(13,bytes);
      assert(sameBytes(await device.read(13),bytes),'Browser and firmware module codecs must match byte for byte');
    }
    for(const engineKind of ['euclid'])for(const rhythm of [{id:'rhythm.stored',version:1,params:{}},rhythmComponent()]){
      const composition=composeModule(defaultModule(engineKind),rhythm),bytes=encodeModule(composition);
      assertSupported(composition,capabilities.components);
      await device.upload(13,bytes,true);await device.upload(13,bytes);
      assert(sameBytes(await device.read(13),bytes),'Component codecs and firmware export agree');
      const track=await device.read(1);await device.upload(1,track);assert(sameBytes(await device.read(13),bytes),'Project backup preserves composition');
    }
    for(const composed of [false,true]){
      let empty=defaultModule('euclid');empty.config.pool=[];
      if(composed)empty=composeModule(empty,rhythmComponent(empty));
      const bytes=encodeModule(empty);await device.upload(13,bytes);
      assert(sameBytes(await device.read(13),bytes));
      const saved=await device.read(1);await device.upload(1,saved);
      assert(sameBytes(await device.read(13),bytes),'Empty palette survives C/JS project backup');
    }
    for(const composed of [false,true]) {
      let chords=defaultModule('euclid');chords.version=4;chords.config.pitchMode='chords';chords.config.pool=[0,2];chords.config.qualities=[1,11,0,0,0,0,0,0];
      if(composed)chords=composeModule(chords,{id:'rhythm.stored',version:1,params:{}});
      assertSupported(chords,capabilities.components);
      const wire=encodeModule(chords);await device.upload(13,wire);
      assert(sameBytes(await device.read(13),wire));
      const track=await device.read(1);await device.upload(1,track);
      assert(sameBytes(await device.read(13),wire),'Chord qualities and pitch mode survive real C project restore');
    }
    await device.upload(13,encodeModule(defaultModule('euclid')));await device.finish();
    {
      const {decodeVariation,variationPayload,defaultVariation}=await import('./variation.mjs?v=90308f36a3');
      const before=await device.read(1);await device.finish();
      let live=decodeVariation(await device.request(26,0));
      const payload=variationPayload(0,0,false,live.revision,{...defaultVariation(),enabled:true,order:3,seed:123456});
      await device.request(27,0,0,payload);
      await assert.rejects(device.request(27,0,0,payload));
      live=decodeVariation(await device.request(26,0));assert.equal(live.current.seed,123456);assert(live.current.enabled);
      const saved=await device.read(1);await device.finish();
      await device.request(27,0,0,variationPayload(3,0,false,live.revision));
      assert.equal(decodeVariation(await device.request(26,0)).current.enabled,false);
      await device.upload(1,saved);await device.finish();
      assert.equal(decodeVariation(await device.request(26,0)).current.seed,123456);
      await device.upload(1,before);await device.finish();
    }
    await device.request(19,0,0,Uint8Array.of(1,7,0,4,0));
    assert.equal(decodeModule(await device.read(13)).config.fills,7);await device.finish();
    await assert.rejects(device.request(19,0,0,Uint8Array.of(1,8,0,4,0)));

    await device.request(18,0,0,Uint8Array.of(0,1));
    assert.equal(decodeModule(await device.read(13)).version,1);
    await device.finish();await device.request(18,0,0,Uint8Array.of(0,0));
    assert.equal(decodeModule(await device.read(13)).config.pitchMode,'palette');
    await device.finish();await device.request(18,0,0,Uint8Array.of(1));
    await device.request(18,0,0,Uint8Array.of(2,1));
    await device.request(18,0,0,Uint8Array.of(2,0));
    const paletteModule=defaultModule('euclid');paletteModule.config.root=5;paletteModule.config.pool=[0,-5,2];paletteModule.config.pin=2;
    await device.upload(13,encodeModule(paletteModule));await device.finish();
    const removal=Uint8Array.of(3,1,3,5,0,251,2,0,0,0,0,0);
    await device.request(18,0,0,removal);
    const afterRemoval=decodeModule(await device.read(13));
    assert.deepEqual(afterRemoval.config.pool,[0,2]);assert.equal(afterRemoval.config.pin,1);assert.equal(afterRemoval.config.root,5);
    await device.finish();await assert.rejects(device.request(18,0,0,removal));
    const current=await device.read(1);assert.equal(current[2],1);assert.equal(current[3],1); // Adds wrapper without destroying drums.
    await device.upload(1,previous);assert(sameBytes(await device.read(1),previous));
    await device.finish();
    const first=await device.request(16),image=new Uint8Array(896);image.set(first);
    for(let offset=64;offset<896;offset+=64)image.set(await device.request(16,first[62],offset),offset);
    assert.ok(decodeRhythm(image).euclid);
    const paletteFirst=await device.request(17),paletteImage=new Uint8Array(960);paletteImage.set(paletteFirst);
    for(let offset=64;offset<960;offset+=64)paletteImage.set(await device.request(17,paletteFirst[62],offset),offset);
    assert.equal(decodePalette(paletteImage).paletteSupported,true);
    const ccFirst=await device.request(21),ccImage=new Uint8Array(192);ccImage.set(ccFirst);
    for(let offset=64;offset<192;offset+=64)ccImage.set(await device.request(21,ccFirst[62],offset),offset);
    assert.equal(decodeCC(ccImage).slots.length,16);
    const scaleRevision=decodePalette(paletteImage).scale.revision;
    await device.request(20,0,0,Uint8Array.of(0,1,2,1,scaleRevision&255,scaleRevision>>8));
    await assert.rejects(device.request(20,0,0,Uint8Array.of(0,1,3,1,scaleRevision&255,scaleRevision>>8)));
    const globalScale=await device.read(0);await device.upload(0,globalScale);
    assert(sameBytes(await device.read(0),globalScale),'Global scale survives firmware project backup');
    const {decodeMelody,harmonize,encodeChordTrack}=await import('./harmony.mjs?v=5bc5306d12');
    await assert.rejects(device.read(25)); // Drum tracks have no melodic source.
    const fixture={length:16,global:{root:0,kind:0},steps:Array.from({length:16},(_,i)=>({note:[60,62,64,65][Math.floor(i/4)],onset:i%4===0}))};
    const harmonyTrack=encodeChordTrack(harmonize(fixture,{rate:'beat'}),4);
    await device.upload(12,harmonyTrack,true).catch(e=>{throw Error(`Harmony validation: ${e.message}`)});await device.upload(12,harmonyTrack);
    const harmonyReadback=await device.read(12);assert(sameBytes(harmonyReadback,harmonyTrack),`Generated chord-track codec differs: lengths ${harmonyReadback.length}/${harmonyTrack.length}; ${[...harmonyReadback].map((v,i)=>v!==harmonyTrack[i]?`${i}:${v}/${harmonyTrack[i]}`:'').filter(Boolean).join(', ')}`);
    const {buildProgression,deriveNotes}=await import('./harmony.mjs?v=5bc5306d12');
    const progression=buildProgression([{candidate:0,duration:15,inversion:1},{candidate:4,duration:9}]);
    for(const pattern of ['chords','arp-up','arp-down','arp-bounce','bass']){
      const encoded=encodeChordTrack(deriveNotes(progression,{pattern,stride:2,octave:pattern==='bass'?-2:0}),4);
      await device.upload(11,encoded,true);await device.upload(11,encoded);
      assert(sameBytes(await device.read(11),encoded),`${pattern} output survives real firmware codec`);
    }
    const {decodeChordProject,encodeChordProject,recipeProgression}=await import('./chord-project.mjs?v=3aa3001b5b');
    const recipeTrack=encodeChordTrack(deriveNotes(buildProgression([{candidate:0,duration:8},{candidate:3,duration:8}],{root:2,kind:1})),4,{editable:true});
    await device.upload(12,recipeTrack,true);await device.upload(12,recipeTrack);
    const recipeSource=decodeChordProject(await device.read(12));assert.equal(recipeSource.recipes.length,2);
    assert(sameBytes(encodeChordProject(recipeSource,recipeSource.recipes),recipeSource.bytes),'No-op recipe round-trip preserves track data');
    const changedRecipes=structuredClone(recipeSource.recipes);changedRecipes[0].root=64;changedRecipes[0].type=8;changedRecipes[0].velocity=73;changedRecipes[0].length=6;
    const revised=encodeChordProject(recipeSource,changedRecipes);await device.upload(12,revised,true);await device.upload(12,revised);
    const revisedSource=decodeChordProject(await device.read(12));assert(sameBytes(revisedSource.bytes,revised));assert.equal(revisedSource.recipes[0].velocity,73);assert.equal(revisedSource.recipes[0].length,6);
    const companion=encodeChordTrack(deriveNotes(recipeProgression(changedRecipes,16),{pattern:'bass',stride:4}),5);
    await device.upload(11,companion);assert(sameBytes(await device.read(12),revised),'Companion output leaves its source intact');
    const {conformRecipesToScale,decodeProjectScale}=await import('./chord-project.mjs?v=3aa3001b5b');
    assert.deepEqual(decodeProjectScale(await device.read(0)),{enabled:true,root:2,kind:1});
    const inScale=conformRecipesToScale(revisedSource.recipes,16,{enabled:true,root:0,kind:0});
    const conformedBytes=encodeChordProject(revisedSource,inScale);await device.upload(9,conformedBytes,true);await device.upload(9,conformedBytes);
    assert(sameBytes(await device.read(9),conformedBytes),'Scale conformity survives actual firmware validation and readback');
    const withOtherNotes=encodeChordProject({...revisedSource,foreign:[{start:6,end:7,pitch:21,velocity:57}]},changedRecipes);
    const otherSource=decodeChordProject(withOtherNotes),withAutomation=[...withOtherNotes];
    withAutomation[37]=45;withAutomation[otherSource.notesEnd]=1;withAutomation.splice(otherSource.notesEnd+2,0,6,0,2,55);
    await device.upload(10,Uint8Array.from(withAutomation));const automated=decodeChordProject(await device.read(10));
    const automatedRecipes=structuredClone(automated.recipes);automatedRecipes[1].velocity=71;
    const automationEdit=encodeChordProject(automated,automatedRecipes);await device.upload(10,automationEdit);const automationRead=decodeChordProject(await device.read(10));
    assert.equal(automationRead.foreign[0].pitch,21);assert.equal(automationRead.bytes[37],45);assert.deepEqual([...automationRead.bytes.slice(automationRead.notesEnd,automationRead.recipeStart)],[...automated.bytes.slice(automated.notesEnd,automated.recipeStart)],'CC automation, palette and scale settings survive chord edits');
    const malformed=revised.slice();malformed[revisedSource.recipeStart+15]++;await assert.rejects(device.upload(12,malformed,true));assert(sameBytes(await device.read(12),revised));
    const inversionTrack=encodeChordTrack(deriveNotes(progression),4,{editable:true});
    await device.upload(8,inversionTrack,true);await device.upload(8,inversionTrack);
    const inversionSource=decodeChordProject(await device.read(8));assert(sameBytes(inversionSource.bytes,inversionTrack));assert.equal(inversionSource.recipes[0].inversion,1);
    const inversionConformed=conformRecipesToScale(inversionSource.recipes,inversionSource.length,{enabled:true,root:3,kind:9});
    assert.equal(inversionConformed[0].inversion,1);const inversionBytes=encodeChordProject(inversionSource,inversionConformed);await device.upload(8,inversionBytes);assert(sameBytes(await device.read(8),inversionBytes));
    const malformedInversion=inversionBytes.slice();malformedInversion[inversionSource.recipeStart+21]=4;await assert.rejects(device.upload(8,malformedInversion,true));assert(sameBytes(await device.read(8),inversionBytes));
    const {customRecipe,recipeNotes,quantizeRecipesToScale,adoptionCandidates,adoptChord}=await import('./chord-project.mjs?v=3aa3001b5b');
    const custom=customRecipe([60,61,64,67],64,'Colour',{start:0,length:8,velocity:79});
    const customProgression=recipeProgression([custom],16),customTrack=encodeChordTrack(customProgression,2,{editable:true});
    await device.upload(7,customTrack,true);await device.upload(7,customTrack);assert(sameBytes(await device.read(7),customTrack),'C3 custom root, name and intervals round-trip in firmware');
    const separate=customRecipe([60,64,67],60,'Separate',{start:0,length:4,velocity:88});separate.type=10;separate.scaleRoot=0;separate.scaleKind=0;delete separate.name;delete separate.intervals;
    const otherScale=quantizeRecipesToScale([separate],16,{enabled:true,root:1,kind:0});const scaleSource=decodeChordProject(encodeChordTrack(recipeProgression([separate],16),0,{editable:true}));const distinct=encodeChordProject(scaleSource,otherScale);await device.upload(3,distinct);assert(sameBytes(await device.read(3),distinct),'Provider and quantizer retain separate scale contexts');
    const customSource=decodeChordProject(customTrack),quantized=quantizeRecipesToScale([custom],16,{enabled:true,root:0,kind:0});
    const quantizedBytes=encodeChordProject(customSource,quantized);await device.upload(7,quantizedBytes);assert(sameBytes(await device.read(7),quantizedBytes));assert.deepEqual(recipeNotes(quantized[0]),[60,64,67]);
    const invalidCustom=quantizedBytes.slice();invalidCustom[customSource.recipeStart+12]=2;await assert.rejects(device.upload(7,invalidCustom,true));assert(sameBytes(await device.read(7),quantizedBytes));
    const {setChordScaleFree}=await import('./chord-project.mjs?v=3aa3001b5b');
    const extended={start:0,length:4,velocity:100,root:67,type:13,scaleRoot:5,scaleKind:5,inversion:0};
    const extendedBytes=encodeChordProject(customSource,[extended]);await device.upload(7,extendedBytes,true);await device.upload(7,extendedBytes);
    assert(sameBytes(await device.read(7),extendedBytes),'C4 scale extensions survive firmware write/readback');
    const free=setChordScaleFree(extended,true,{enabled:true,root:5,kind:5}),freeBytes=encodeChordProject(customSource,[free]);
    await device.upload(7,freeBytes);assert(sameBytes(await device.read(7),freeBytes),'C4 single-chord Free policy survives firmware readback');
    const invalidPolicy=freeBytes.slice();invalidPolicy[decodeChordProject(freeBytes).recipeStart+12]=2;await assert.rejects(device.upload(7,invalidPolicy,true));assert(sameBytes(await device.read(7),freeBytes));
    const naked=encodeChordTrack(customProgression,1),nakedSource=decodeChordProject(naked,{allowUnowned:true});
    const adopted=adoptChord(nakedSource,[],adoptionCandidates(nakedSource)[0],64,'Adopted');const adoptedBytes=encodeChordProject(adopted.source,adopted.recipes);await device.upload(6,adoptedBytes);assert(sameBytes(await device.read(6),adoptedBytes));
    const legacyBank=Uint8Array.from([...naked.slice(0,nakedSource.recipeStart),...encodeModule(defaultModule('chord-bank'))]);
    await device.upload(4,legacyBank);const migratedBank=decodeChordProject(await device.read(4),{allowUnowned:true});assert.deepEqual(migratedBank.events,nakedSource.events);await assert.rejects(device.upload(13,encodeModule(defaultModule('chord-bank')),true));
    const legacySource=decodeChordProject(legacyBank,{allowUnowned:true}),legacyAdopt=adoptChord(legacySource,[],adoptionCandidates(legacySource)[0],64,'Imported');assert(legacySource.replacementModule);
    const converted=encodeChordProject(legacyAdopt.source,legacyAdopt.recipes);await device.upload(4,converted);const convertedRead=await device.read(4);assert(sameBytes(convertedRead,converted),`Adoption codec ${convertedRead.length}/${converted.length}: ${[...convertedRead].map((v,i)=>v!==converted[i]?`${i}:${v}/${converted[i]}`:'').filter(Boolean).join(', ')}`);
    const bankProgression=deriveNotes(buildProgression([{candidate:0,duration:8,inversion:1}],{provider:'harmony.bank',bank:defaultModule('chord-bank')}));const bankRecipes=encodeChordTrack(bankProgression,3,{editable:true});await device.upload(5,bankRecipes);assert(sameBytes(await device.read(5),bankRecipes));
    const melodyCapture=decodeMelody(await device.read(36));assert.equal(melodyCapture.length,16);assert.equal(melodyCapture.track,11);assert(melodyCapture.steps[0].onset);


  } finally {device.close();lines.close();engine.kill();}
  console.log('Browser ↔ real firmware protocol bridge passed.');
}
console.log('Module validation, notes, file, and wire-codec tests passed.');

// Live polling is serialized with transfers and stops on old firmware.
{
  const statuses=[],states=[];let calls=0;
  const bytes=new Uint8Array(60);bytes[0]=1;bytes[6]=255;bytes[12]=16;bytes[58]=bytes[59]=255;
  bytes[24+60/8|0]=1<<(60%8);assert.deepEqual(decodeLive(bytes).sounding,[60]);
  assert.throws(()=>decodeLive(bytes.slice(1)));const bad=bytes.slice();bad[2]=12;assert.throws(()=>decodeLive(bad));
  const connection={pending:false,request:async(command,track)=>{calls++;const out=bytes.slice();out[2]=command===12?track:0;return out;}};
  const monitor=new LiveMonitor(connection,{onState:s=>states.push(s),onStatus:s=>statuses.push(s)});
  monitor.detailSupported=false;monitor.rhythmSupported=false;monitor.paletteSupported=false;monitor.stopped=false;await monitor.pause();await monitor.tick();clearTimeout(monitor.timer);assert.equal(calls,0);
  monitor.resume();monitor.select(7);await monitor.tick();clearTimeout(monitor.timer);assert.equal(states.at(-1).track,7);
  connection.pending=true;await monitor.tick();clearTimeout(monitor.timer);assert.equal(calls,1);connection.pending=false;
  connection.request=async()=>{const e=Error('unsupported');e.status=1;throw e;};await monitor.tick();assert.equal(statuses.at(-1),'unsupported');assert.equal(monitor.stopped,true);
  const lost=new LiveMonitor({request:async()=>{throw Error('timeout');}},{onState:()=>assert.fail(),onStatus:s=>statuses.push(s)});lost.stopped=false;await lost.tick();lost.stop();assert.equal(statuses.at(-1),'stale');
}
console.log('Live snapshot, track selection, polling lifecycle, and compatibility tests passed.');

// Visual snapshots are assembled atomically, with fallback only on unsupported firmware.
{
  const bytes=new Uint8Array(352);bytes[0]=2;bytes[1]=3;bytes[6]=255;bytes[12]=16;bytes[58]=bytes[59]=255;
  bytes[60]=3;bytes[61]=15;bytes[62]=7;bytes[63]=1;bytes[64]=255;
  const decoded=decodeVisual(bytes);assert.equal(decoded.visual.mode,'euclid');assert.equal(decoded.visual.os,'win');assert.equal(decoded.visual.leds[0],255);
  assert.throws(()=>decodeVisual(bytes.slice(0,351)));
  const invalid=bytes.slice();invalid[63]=2;assert.throws(()=>decodeVisual(invalid));
  const calls=[],states=[],statuses=[];
  const device={request:async(cmd,obj=0,offset=0)=>{calls.push([cmd,obj,offset]);assert.equal(cmd,13);assert.equal(obj,offset?7:0);return bytes.slice(offset,offset+64);}};
  const monitor=new LiveMonitor(device,{wantVisual:()=>true,onState:s=>states.push(s),onStatus:s=>statuses.push(s)});
  monitor.detailSupported=false;monitor.rhythmSupported=false;monitor.paletteSupported=false;monitor.stopped=false;await monitor.tick();monitor.stop();assert.equal(calls.length,6);assert.equal(states.length,1);assert.equal(states[0].visual.mode,'euclid');
  const legacy=bytes.slice(0,60);legacy[0]=1;let attempts=0;
  const fallback=new LiveMonitor({request:async cmd=>{if(cmd===13||cmd===14||cmd===16||cmd===17){attempts++;throw Object.assign(Error('old'),{status:1});}return legacy;}},{wantVisual:()=>true,onState:s=>states.push(s),onStatus:()=>{}});
  fallback.stopped=false;await fallback.tick();clearTimeout(fallback.timer);await fallback.tick();fallback.stop();assert.equal(attempts,4);assert.equal(states.at(-1).visual,undefined);
  const interrupted=new LiveMonitor({request:async(cmd,obj,offset=0)=>{if(offset)throw Object.assign(Error('superseded'),{status:2});return bytes.slice(0,64);}},{wantVisual:()=>true,onState:()=>assert.fail('Must not publish partial frames'),onStatus:s=>statuses.push(s)});
  interrupted.detailSupported=false;interrupted.rhythmSupported=false;interrupted.paletteSupported=false;interrupted.stopped=false;await interrupted.tick();interrupted.stop();assert.equal(statuses.at(-1),'stale');
  let pieces=0;
  const paused=new LiveMonitor({request:async()=>{pieces++;paused.paused=true;return bytes.slice(0,64);}},{wantVisual:()=>true,onState:()=>assert.fail('Paused frame must be discarded'),onStatus:()=>{}});
  paused.detailSupported=false;paused.stopped=false;await paused.tick();paused.stop();assert.equal(pieces,1);
}
{
  const {keyboardMarks}=await import('./keyboard.mjs?v=11bbe2f648');
  const state={active:true,running:true,track:2,bar:1,step:18,length:32,pattern:4,armed:true,selected:[60],sounding:[72],chord:0};
  let marks=keyboardMarks(state,'drum',true);assert.ok(marks.F3.current);assert.ok(marks['3'].playhead&&marks['3'].hit);assert.ok(!marks.D.playhead);
  marks=keyboardMarks(state,'melodic',true);assert.ok(marks.D.playhead);assert.ok(marks.Q.selected&&marks.Q.sounding);
  marks=keyboardMarks({...state,active:false},'off');assert.ok(!marks.F3.current&&!marks.Home.running);
}
console.log('Visual paging, legacy fallback, interrupted snapshots, and keyboard projections passed.');

// Detailed snapshots carry all stored pitches atomically, even with the manual closed.
{
  const bytes=new Uint8Array(608);bytes[0]=3;bytes[1]=3;bytes[3]=1;bytes[6]=255;bytes[12]=32;bytes[14]=1;
  bytes[58]=bytes[59]=255;bytes[60]=2;bytes[62]=9;bytes[63]=1;
  for(const n of [0,60,64,67,127])bytes[352+(n>>3)]|=1<<(n&7);
  bytes[368+7]=16;
  const decoded=decodeDetailed(bytes);
  assert.deepEqual(decoded.stepNotes[0],[0,60,64,67,127]);assert.deepEqual(decoded.stepNotes[1],[60]);assert.deepEqual(decoded.stepNotes[2],[]);
  assert.equal(decoded.visual.mode,'melodic');assert.equal(decoded.bar,1);
  assert.throws(()=>decodeDetailed(bytes.slice(0,607)));
  const wrong=bytes.slice();wrong[0]=2;assert.throws(()=>decodeDetailed(wrong));
  const states=[],statuses=[],calls=[];
  const monitor=new LiveMonitor({request:async(cmd,obj=0,offset=0)=>{calls.push([cmd,obj,offset]);assert.equal(cmd,14);assert.equal(obj,offset?9:0);return bytes.slice(offset,offset+64);}},{onState:s=>states.push(s),onStatus:s=>statuses.push(s)});
  monitor.rhythmSupported=false;monitor.paletteSupported=false;monitor.stopped=false;await monitor.tick();monitor.stop();assert.equal(calls.length,10);assert.deepEqual(states[0].stepNotes,decoded.stepNotes);
  for(const failure of ['short','superseded','paused']){
    const broken=new LiveMonitor({request:async(cmd,obj,offset=0)=>{
      if(!offset)return bytes.slice(0,64);
      if(failure==='paused'){broken.paused=true;return bytes.slice(offset,offset+64);}
      if(failure==='superseded')throw Object.assign(Error('replaced'),{status:2});
      return bytes.slice(offset,offset+63);
    }},{onState:()=>assert.fail('Never publish a partial step-note snapshot'),onStatus:s=>statuses.push(s)});
    broken.rhythmSupported=false;broken.paletteSupported=false;broken.stopped=false;await broken.tick();broken.stop();
    if(failure!=='paused')assert.equal(statuses.at(-1),'stale');
  }
  const {keyboardMarks}=await import('./keyboard.mjs?v=11bbe2f648');
  const marks=keyboardMarks(decoded,'melodic');assert.deepEqual(marks.A.notes,[0,60,64,67,127]);assert.deepEqual(marks.S.notes,[60]);
  assert.equal(keyboardMarks({...decoded,child:true},'drum').A.notes,undefined);
}
console.log('Stored step-note snapshots, chords, paging and keyboard labels passed.');

for(const source of ['euclid','chord-bank'])for(const rhythm of [{id:'rhythm.stored',version:1,params:{}},rhythmComponent()]){
  const m=composeModule(defaultModule(source),rhythm),wire=encodeModule(m);
  assert(sameBytes(encodeModule(decodeModule(wire)),wire));
  const editable=splitModule(m);assert.deepEqual(composeModule(editable.module,editable.rhythm),m);
  for(let i=0;i<wire.length;i++)assert.throws(()=>decodeModule(wire.slice(0,i)));
  const wrong=structuredClone(m);wrong.components[0].version=99;assert.throws(()=>validateModule(wrong));
  wrong.components[0].version=1;wrong.components.reverse();assert.throws(()=>validateModule(wrong));
  assert.throws(()=>assertSupported(m,null));
  assertSupported(m,Uint8Array.from([1,3,2,8,192,0,1,1,2,1,3,1,4,1,3,2]));
  assert.throws(()=>assertSupported(m,Uint8Array.from([1,2,2,8,192,0,1,9,2,9,3,9,4,9])));
}
console.log('Component composition, versions, compatibility and resource validation passed.');

// Help follows contract versions and distinguishes live setup from a local draft.
const {helpFor,guideComponents,moduleFromLive}=await import('./component-help.mjs?v=1b4a360ca4');
const {COMPONENTS}=await import('./module.mjs?v=8c4bcd2da4');
for(const component of COMPONENTS){assert.ok(component.help);assert.equal(helpFor(component),component.help);}
assert.match(helpFor({id:'rhythm.euclid',version:99}).purpose,/not available/);
assert.equal(moduleFromLive(null),null);
for(const rhythm of ['stored','euclid'])for(const engine of [0,1]){
  const module=moduleFromLive({composed:true,rhythm,engine});
  assert.deepEqual(guideComponents(module).map(c=>c.id),[`rhythm.${rhythm}`,engine?'pitch.chord-bank':'pitch.track']);
}
assert.equal(guideComponents(moduleFromLive({composed:false,engine:1})).length,1);
assert.equal(guideComponents(defaultModule('euclid')).length,2);
console.log('Component help registry, version fallback and live setup projection passed.');

const {liveOrbitSteps}=await import('./orbit.mjs?v=fe48e35c75');
const orbitState={type:1,child:false,bar:1,length:20,pattern:0xffff,active:true,running:true,step:18,stepNotes:Array.from({length:16},(_,i)=>i===2?[60,64,67]:[])};
const orbit=liveOrbitSteps(orbitState,true);
assert.equal(orbit.filter(s=>s.hit).length,4);
assert.equal(orbit.filter(s=>s.playhead).length,1);
assert.deepEqual(orbit[2].notes,[60,64,67]);
assert.equal(orbit[4].outside,true);
assert.equal(liveOrbitSteps(orbitState,false).some(s=>s.playhead),false);
assert.equal(liveOrbitSteps({...orbitState,child:true},true)[2].notes,null);
assert.equal(liveOrbitSteps({...orbitState,step:0},true).some(s=>s.playhead),false);
console.log('Live orbit bar bounds, chord labels, stale state and playhead passed.');

{
  const bytes=new Uint8Array(896);bytes[0]=4;bytes[1]=3;bytes[3]=1;bytes[6]=255;bytes[12]=16;bytes[62]=2;bytes[63]=1;
  bytes[608]=19;bytes[610]=16;bytes[612]=5;bytes[614]=2;bytes[616]=100;bytes[620]=17;
  bytes[640+8]=1;bytes[656+8]=16;
  const decoded=decodeRhythm(bytes);assert.equal(decoded.euclid.fills,5);assert.deepEqual(decoded.projectedNotes[0],[64]);assert.deepEqual(decoded.projectedNotes[1],[68]);
  assert.deepEqual(liveOrbitSteps(decoded,true)[0].notes,[64]);
  const laneBytes=bytes.slice();laneBytes[609]=7;laneBytes[619]=2;
  assert.equal(decodeRhythm(laneBytes).euclid.lane,2);
  laneBytes[619]=255;assert.equal(decodeRhythm(laneBytes).euclid.lane,null);
  laneBytes[619]=12;assert.throws(()=>decodeRhythm(laneBytes));
  const bad=bytes.slice();bad[612]=17;assert.throws(()=>decodeRhythm(bad));
  assert.throws(()=>decodeRhythm(bytes.slice(0,895)));
  const states=[],calls=[];
  const monitor=new LiveMonitor({request:async(cmd,obj=0,offset=0)=>{calls.push(offset);assert.equal(cmd,16);assert.equal(obj,offset?2:0);return bytes.slice(offset,offset+64);}},{onState:s=>states.push(s),onStatus:()=>{}});
  monitor.paletteSupported=false;monitor.stopped=false;await monitor.tick();monitor.stop();assert.equal(calls.length,14);assert.deepEqual(states[0].projectedNotes,decoded.projectedNotes);
  for(const failure of ['short','paused','token']){
    const monitor=new LiveMonitor({request:async(cmd,obj,offset=0)=>{
      if(!offset)return bytes.slice(0,64);
      if(failure==='paused')monitor.paused=true;
      if(failure==='token')throw Object.assign(Error('Expired'),{status:2});
      return bytes.slice(offset,offset+(failure==='short'?63:64));
    }},{onState:()=>assert.fail('Partial rhythm snapshot'),onStatus:()=>{}});
    monitor.paletteSupported=false;monitor.stopped=false;await monitor.tick();monitor.stop();
  }
}
console.log('Live rhythm settings, projected pitches, paging and partial-frame rejection passed.');

{
  const bytes=new Uint8Array(960);bytes[0]=5;bytes[1]=3;bytes[3]=1;bytes[6]=255;bytes[12]=16;bytes[62]=7;bytes[63]=1;
  bytes[608]=19;bytes[610]=16;bytes[612]=4;bytes[616]=100;
  bytes[896]=31;bytes[897]=2;bytes[898]=255;bytes[900]=3;
  bytes.set([0,4],904);bytes.set([60,64],912);bytes.set([0,4,7],920);bytes.set([60,64,67],928);bytes.fill(255,936,952);bytes[936]=0;bytes[940]=1;
  const state=decodePalette(bytes);assert.equal(state.palette.capturing,true);assert.deepEqual(state.palette.saved,[{offset:0,note:60},{offset:4,note:64}]);assert.equal(state.palette.capture.length,3);
  assert.equal(state.palette.editable,false);assert.equal(state.palette.mode,'transpose');
  const modern=bytes.slice();modern[954]=1;modern[952]=16;modern[940]=255;
  assert.equal(decodePalette(modern).palette.mode,'palette');assert.equal(decodePalette(modern).palette.overrides,16);
  assert.equal(decodePalette(modern).palette.replaceShortcut,false);assert.equal(decodePalette(modern).palette.starter,false);
  modern[958]=3;assert.equal(decodePalette(modern).palette.replaceShortcut,true);assert.equal(decodePalette(modern).palette.starter,true);modern[958]=1;assert.equal(decodePalette(modern).palette.starter,false);
  modern[956]=1;modern[957]=2;assert.equal(decodePalette(modern).palette.captureEditable,true);assert.equal(decodePalette(modern).palette.replace,false);assert.equal(decodePalette(modern).palette.retained,2);modern[956]=7;assert.equal(decodePalette(modern).palette.full,true);assert.equal(decodePalette(modern).palette.replace,true);
  modern[955]=1;assert.equal(decodePalette(modern).palette.mode,'transpose');modern[955]=2;assert.throws(()=>decodePalette(modern));
  const bad=bytes.slice();bad[897]=9;assert.throws(()=>decodePalette(bad));
  const calls=[],states=[];
  const monitor=new LiveMonitor({request:async(cmd,obj=0,offset=0)=>{assert.equal(cmd,17);assert.equal(obj,offset?7:0);calls.push(offset);return bytes.slice(offset,offset+64);}},{onState:s=>states.push(s),onStatus:()=>{}});
  monitor.stopped=false;await monitor.tick();monitor.stop();assert.equal(calls.length,15);assert.equal(states[0].palette.capture.length,3);
  const {paletteShortcut}=await import('./palette.mjs?v=831d6385ec');assert.match(paletteShortcut('win'),/Command-labelled/);assert.match(paletteShortcut('mac'),/Shift \+ Option/);
}
console.log('Palette live capture, saved entries, slot mapping, layouts and paging passed.');

// Palette contracts preserve legacy transposition and reject malformed modes.
for(const pitchMode of ['palette','transpose']) {
  const m=defaultModule('euclid');m.config.pitchMode=pitchMode;
  for(const module of [m,composeModule(m,rhythmComponent())]){
    const wire=encodeModule(module),decoded=splitModule(decodeModule(wire)).module;
    assert.equal(decoded.config.pitchMode??'transpose',pitchMode);
    assert(sameBytes(encodeModule(decodeModule(wire)),wire));
  }
}
const old=defaultModule('euclid');old.version=1;delete old.config.pitchMode;
assert.equal(decodeModule(encodeModule(old)).version,1);
assert.equal(splitModule(composeModule(old,rhythmComponent())).module.version,1);
const badMode=encodeModule(defaultModule('euclid'));badMode[badMode.length-1]=2;
assert.throws(()=>decodeModule(badMode));
console.log('Palette pitch modes, legacy migration and invalid modes passed.');

const {paletteVisible}=await import('./palette.mjs?v=831d6385ec');
const paletteView={active:true,type:1,child:false,engine:0,visual:{mode:'euclid'}};
assert.equal(paletteVisible(paletteView),true);
for(const change of [{active:false},{type:0},{child:true},{engine:1},{visual:{mode:'melodic'}},{visual:{mode:'drum'}},{visual:{mode:'cc'}},{visual:null}])assert.equal(paletteVisible({...paletteView,...change}),false);
console.log('Palette visibility follows melodic Euclid controls only.');

for(const pitchMode of ['palette','transpose'])for(const composed of [false,true]){
  let m=defaultModule('euclid');m.config.pool=[];m.config.pin=null;m.config.pitchMode=pitchMode;
  if(composed)m=composeModule(m,rhythmComponent(m));
  const bytes=encodeModule(m),decoded=decodeModule(bytes);
  assert(sameBytes(encodeModule(decoded),bytes));
}
{
  const bytes=new Uint8Array(960);bytes[0]=5;bytes[1]=1;bytes[3]=1;bytes[6]=255;bytes[12]=16;bytes[62]=1;bytes[63]=1;bytes[896]=1;bytes[898]=255;bytes.fill(255,936,952);bytes[958]=5;
  const p=decodePalette(bytes).palette;assert(p.clearShortcut);assert.deepEqual(p.saved,[]);assert.equal(p.pin,null);assert(p.slots.every(n=>n===255));
}
console.log('Empty palette module codecs and live snapshot passed.');

{
  const {rhythmFields,rhythmEditPayload}=await import('./rhythm-controls.mjs');
  const {controlsFor}=await import('./keyboard.mjs?v=11bbe2f648');
  const state={length:16,euclid:{steps:12,orderedKeys:true}};
  assert.deepEqual(rhythmFields(state).slice(0,5).map(f=>f.shortcut),['Q','W','E','R','T']);
  const controls=controlsFor('euclid');
  assert.deepEqual(['Q','W','E','R','T'].map(k=>controls[k].label),['Steps','Fills','Rotation','Probability','Accent']);
  assert.equal(controlsFor('euclid','mac',false,false).Q.label,'Fills');
  assert.equal(rhythmFields(state)[1].max,12);assert.equal(rhythmFields(state)[2].max,11);
  assert.deepEqual(rhythmEditPayload(0,2048,16),Uint8Array.of(0,0,8,16,0));
  assert.throws(()=>rhythmEditPayload(0,1.5,16));
}
console.log('Ordered rhythm controls, legacy key labels and edit payload passed.');

{
  const {scaleNotes,scaleNames,modeKeys}=await import('./scale.mjs');
  const {controlsFor}=await import('./keyboard.mjs?v=11bbe2f648');
  assert.deepEqual(scaleNotes(2,1),[2,4,5,7,9,11,0]);
  const bytes=new Uint8Array(960);bytes[0]=5;bytes[1]=1;bytes[3]=1;bytes[6]=255;bytes[12]=16;bytes[60]=10;bytes[62]=1;bytes[63]=1;bytes[898]=255;bytes.fill(255,936,952);bytes[622]=1;bytes[623]=1;bytes[624]=2;bytes[625]=1;bytes[630]=1;bytes[632]=2;bytes[633]=5;bytes[634]=7;
  const s=decodePalette(bytes);assert.equal(s.visual.mode,'scale');assert.equal(s.scale.root,2);assert.equal(s.scale.pending.kind,5);assert.equal(s.scale.revision,7);assert.equal(s.scale.count,7);bytes[625]=7;assert.throws(()=>decodePalette(bytes));
  bytes[636]=scaleNames.length;
  const extra=[[0,2,3,5,7,8,11],[0,2,4,5,7,8,11],[0,2,4,7,9],[0,3,5,7,10]];
  for(let kind=7;kind<scaleNames.length;kind++){
    assert.deepEqual(scaleNotes(0,kind),extra[kind-7]);
    bytes[625]=bytes[628]=bytes[633]=kind;
    const decoded=decodePalette(bytes);assert.equal(decoded.scale.kind,kind);assert.equal(decoded.scale.localKind,kind);assert.equal(decoded.scale.pending.kind,kind);
    assert.equal(controlsFor('scale')[modeKeys[kind]].label,scaleNames[kind]);
  }
  bytes[625]=scaleNames.length;assert.throws(()=>decodePalette(bytes));
}
console.log('Scale registry, editor snapshot and invalid-state rejection passed.');

{
  const {ccKeyLabel,ccStatus}=await import('./cc.mjs');
  assert.equal(ccKeyLabel(30),'1');assert.equal(ccKeyLabel(20),'Q');assert.equal(ccKeyLabel(45),'−');
  const cc=new Uint8Array(192);cc[0]=1;cc[1]=31;cc[2]=3;cc[3]=0;cc[4]=16;cc[62]=11;cc[63]=1;
  for(let i=0;i<16;i++){cc[64+i*6]=i<10?30+i:20;cc[66+i*6]=16+i;cc[67+i*6]=3;cc[68+i*6]=64;}
  cc[69]=1;const decoded=decodeCC(cc);assert.equal(decoded.track,3);assert(decoded.latched&&decoded.temporary&&decoded.trackOverride);assert(decoded.slots[0].held);
  assert.equal(decoded.automationSupported,false);
  // Legacy headers contain the general live channel/bank bytes here.
  cc[5]=15;cc[6]=255;assert.equal(decodeCC(cc).automationSupported,false);
  cc[160]=1;cc[161]=1;
  const manual=decodeCC(cc);assert(manual.automationSupported&&manual.manual);
  assert.equal(manual.automationShiftPgDn,false);
  cc[162]=1;assert.equal(decodeCC(cc).automationShiftPgDn,true);
  assert.match(ccStatus(manual,true),/Manual.*recording and playback paused/);
  assert.match(ccStatus({...manual,manual:false,running:true},true),/Automation.*recording/);
  assert.match(ccStatus(manual,false),/Signal lost/);
  cc[161]=2;assert.throws(()=>decodeCC(cc));cc[161]=0;
  const bad=cc.slice();bad[68]=128;assert.throws(()=>decodeCC(bad));assert.throws(()=>decodeCC(cc.slice(0,128)));
  const palette=new Uint8Array(960);palette[0]=5;palette[1]=1;palette[6]=255;palette[12]=16;palette[60]=5;palette[62]=9;palette[63]=1;palette[898]=255;palette.fill(255,936,952);palette[637]=1;
  const states=[],statuses=[];
  const device={request:async(cmd,obj=0,offset=0)=>{assert(cmd===17||cmd===21);const bytes=cmd===17?palette:cc;assert.equal(obj,offset?bytes[62]:0);return bytes.slice(offset,offset+64)}};
  const monitor=new LiveMonitor(device,{onState:s=>states.push(s),onStatus:s=>statuses.push(s)});monitor.stopped=false;
  await monitor.tick();clearTimeout(monitor.timer);assert.equal(states.at(-1).cc.slots[0].value,64);
  device.request=async(cmd,obj=0,offset=0)=>{if(cmd===21&&offset===128)throw Object.assign(Error('superseded'),{status:2});return (cmd===17?palette:cc).slice(offset,offset+64)};
  await monitor.tick();monitor.stop();assert.equal(states.length,1);assert.equal(statuses.at(-1),'stale');
}
console.log('CC state paging, target override, key labels and partial snapshot rejection passed.');

{
  const {decodeMelody,harmonize,encodeChordTrack}=await import('./harmony.mjs?v=5bc5306d12');
  const source={length:16,global:{root:0,kind:0},steps:Array.from({length:16},(_,i)=>({note:i<8?60:62,onset:i%4===0}))};
  const original=JSON.stringify(source),result=harmonize(source,{rate:'beat'});
  assert.equal(result.chords.length,4);assert.equal(result.events.length,12);assert.equal(JSON.stringify(source),original);
  for(const chord of result.chords){assert(chord.matched);assert(chord.notes.some(n=>n%12===chord.anchor%12));assert(chord.notes.every(n=>[0,2,4,5,7,9,11].includes(n%12)))}
  assert.equal(harmonize(source,{rate:'bar'}).chords.length,1);
  for(let kind=0;kind<11;kind++){const r=harmonize(source,{kind});assert(r.events.every(e=>e.pitch>=0&&e.pitch<=127))}
  const bank=defaultModule('chord-bank');const fromBank=harmonize(source,{provider:'harmony.bank',bank,rate:'hit'});assert.equal(fromBank.chords.length,4);
  for(const chord of fromBank.chords)assert(bank.config.slots.some(slot=>slot.name===chord.label&&chord.notes.every(n=>slot.notes.some(p=>p%12===n%12))));
  assert.throws(()=>harmonize(source,{provider:'unknown'}));assert.throws(()=>decodeMelody(new Uint8Array(8)));
  assert.throws(()=>encodeChordTrack({...result,events:Array(129).fill(result.events[0])},0),/128/);
  const silent={...source,steps:source.steps.map(()=>({note:null,onset:false}))};assert.equal(harmonize(silent).chords.length,0);
  assert.throws(()=>encodeChordTrack(harmonize(silent),0),/no notes/);
  for(const note of [0,127])assert(harmonize({...source,steps:source.steps.map(()=>({note,onset:true}))},{rate:'hit'}).events.every(e=>e.pitch>=0&&e.pitch<=127));
}
console.log('Harmony providers, timing, source preservation, MIDI bounds and event limits passed.');
{
  const {buildProgression,progressionCandidates,deriveNotes,encodeChordTrack,harmonyRequestChanged}=await import('./harmony.mjs?v=5bc5306d12');
  const entries=[{candidate:0,duration:8,inversion:1},{candidate:4,duration:4}];
  const original=JSON.stringify(entries),progression=buildProgression(entries);
  assert.deepEqual(progression.chords[0].notes,[64,67,72]);assert.equal(progression.length,12);assert.equal(JSON.stringify(entries),original);
  assert.deepEqual(progression.chords.map(c=>[c.start,c.end]),[[0,7],[8,11]]);
  const up=deriveNotes(progression,{pattern:'arp-up',stride:2});
  assert.deepEqual(up.events.map(e=>e.pitch),[64,67,72,64,67,71]);
  assert.deepEqual(deriveNotes(progression,{pattern:'arp-down',stride:2}).events.slice(0,4).map(e=>e.pitch),[72,67,64,72]);
  assert.deepEqual(deriveNotes(progression,{pattern:'arp-bounce',stride:1}).events.slice(0,8).map(e=>e.pitch),[64,67,72,67,64,67,72,67]);
  assert.deepEqual(deriveNotes(progression,{pattern:'bass',stride:4,octave:-2}).events.map(e=>e.pitch),[40,40,43]);
  const clipped=deriveNotes(buildProgression([{candidate:0,duration:3}]),{pattern:'arp-up',stride:2});assert.equal(clipped.events.at(-1).end,2);
  for(let kind=0;kind<11;kind++)for(let root=0;root<12;root++){
    const candidates=progressionCandidates({kind,root});const p=buildProgression(candidates.map((_,candidate)=>({candidate,duration:4})),{kind,root});
    for(const pattern of ['chords','arp-up','arp-down','arp-bounce','bass'])assert(encodeChordTrack(deriveNotes(p,{pattern}),0).length>0);
  }
  const bank=defaultModule('chord-bank'),p=buildProgression([{candidate:0,duration:16}],{provider:'harmony.bank',bank});assert.deepEqual(p.chords[0].notes,chordNotes(bank,0).sort((a,b)=>a-b));
  assert.throws(()=>buildProgression([{candidate:99}]),/Choose a chord/);assert.throws(()=>buildProgression([{candidate:0,duration:2049}]),/duration/);
  assert.throws(()=>buildProgression([{candidate:0,duration:2048},{candidate:0}]),/2048/);
  assert.throws(()=>buildProgression([{candidate:0,inversion:3}]),/voicing/);assert.throws(()=>buildProgression([{candidate:0,octave:5,inversion:2}]),/MIDI/);
  const long=buildProgression([{candidate:0,duration:129}]);assert.throws(()=>deriveNotes(long,{pattern:'arp-up',stride:1}),/128/);
  assert.equal(deriveNotes(buildProgression([{candidate:0,duration:128}]),{pattern:'arp-up',stride:1}).events.length,128);
  assert.throws(()=>deriveNotes(progression,{pattern:'bad'}));assert.throws(()=>deriveNotes(progression,{stride:3}));
  const snapshot=new Uint8Array(896);snapshot[0]=4;snapshot[1]=1;snapshot[6]=255;snapshot[12]=16;snapshot[62]=9;snapshot[63]=1;
  snapshot[638]=1;assert(decodeRhythm(snapshot).harmonySupported);assert.equal(decodeRhythm(snapshot).harmonyRequest,null);
  snapshot[638]=3;snapshot[639]=255;assert.equal(decodeRhythm(snapshot).harmonyRequest,255);
  assert(!harmonyRequestChanged(null,4));assert(!harmonyRequestChanged(4,4));assert(harmonyRequestChanged(255,0));assert(!harmonyRequestChanged(4,null));
}
console.log('Chord-first progressions, voicings, arpeggio/bass patterns and shortcut edge detection passed.');
{
  const {keyboardMarks,controlsFor}=await import('./keyboard.mjs?v=11bbe2f648');
  const state={active:true,track:0,type:1,bar:1,step:16,length:32,pattern:1,editStep:16,selected:[67],sounding:[],visual:{holdEdit:true}};
  const marks=keyboardMarks(state,'melodic-edit');assert(marks.A.selected);assert(marks.T.selected);assert.deepEqual(marks.T.notes,[67]);
  assert.equal(controlsFor('melodic-edit','mac',false,true,true).Knob.label,'Step edit');
  assert.equal(controlsFor('drum-edit','mac',false,true,true).Knob.label,'Velocity');
  const heldControls=controlsFor('melodic-edit','mac',false,true,true);
  for(const key of ['I','O','P'])assert(!heldControls[key].detail.startsWith('Release'));
  assert(controlsFor('chord-step').Knob.detail.includes('starts on Length'));
  assert.deepEqual(['I','O','P','[',']'].map(key=>controlsFor('chord-step')[key].label),['Length','Root','Velocity','Inversion','Type']);
  const image=new Uint8Array(352);image[0]=2;image[1]=1;image[6]=255;image[12]=16;image[60]=8;image[61]=16;image[62]=1;image[63]=1;
  assert(decodeVisual(image).visual.holdEdit);image[61]=0;assert(!decodeVisual(image).visual.holdEdit);
}
console.log('Hold editor telemetry, edited step and octave-specific keyboard note labels passed.');

{
  const {decodeChordMode,chordLabel}=await import('./chord-mode.mjs?v=e16e3e7465');
  const {keyboardMarks,controlsFor}=await import('./keyboard.mjs?v=11bbe2f648');
  const bytes=new Uint8Array(192);bytes[0]=1;bytes[2]=1;bytes[3]=62;bytes[4]=8;bytes[5]=8;bytes[11]=1;bytes[12]=4;bytes.set([62,65,69,72],16);bytes[26]=16;bytes[28]=1;
  for(let i=0;i<8;i++){let b=64+i*8;bytes[b]=i?1:3;bytes[b+1]=62;bytes[b+2]=8;bytes[b+5]=8;}
  const chordMode=decodeChordMode(bytes);assert.equal(chordLabel(chordMode),'Dm7');assert.equal(chordMode.steps[7].start,0);assert.equal(chordMode.steps[8],null);
  const state={active:true,track:0,type:1,bar:0,length:16,step:0,running:false,selected:[],sounding:[],chord:5,chordMode};
  const marks=keyboardMarks(state,'chord-step');assert(marks.W.selected);assert(marks.R.hit);assert(marks.K.selected);assert.equal(marks.A.chordLabel,'Dm7');assert.equal(marks.S.chordLabel,'↳ Dm7');
  assert(keyboardMarks(state,'chord')['6'].selected,'Bank selection keeps its separate numeric field');
  const v2=bytes.slice();v2[0]=2;v2[7]=3;v2[30]=1;for(let i=0;i<8;i++)v2[71+i*8]=1;
  const inverted=decodeChordMode(v2);assert.equal(inverted.inversion,1);assert.equal(inverted.steps[0].inversion,1);assert.equal(chordLabel(inverted),'Dm7 · inv 1');v2[30]=4;assert.throws(()=>decodeChordMode(v2));
  assert.equal(controlsFor('chord-step')['['].label,'Inversion');
  assert.equal(controlsFor('chord-step').I.label,'Length');assert.equal(controlsFor('chord-step').O.label,'Root');assert.equal(controlsFor('chord-step').P.label,'Velocity');
  assert.throws(()=>decodeChordMode(bytes.slice(0,191)));const bad=bytes.slice();bad[4]=12;assert.throws(()=>decodeChordMode(bad));bad.set(bytes);bad[69]=17;assert.throws(()=>decodeChordMode(bad));
}
console.log('Chord snapshot validation, whole-group keyboard marks and bank compatibility passed.');

{
  const {buildProgression,deriveNotes,encodeChordTrack,editableRecipes}=await import('./harmony.mjs?v=5bc5306d12');
  const {decodeChordProject,encodeChordProject,recipeNotes}=await import('./chord-project.mjs?v=3aa3001b5b');
  const p=deriveNotes(buildProgression([{candidate:0,duration:4},{candidate:1,duration:4}],{root:2,kind:9}));
  const bytes=encodeChordTrack(p,3,{editable:true}),source=decodeChordProject(bytes);
  const laneRecord=new Uint8Array(26);laneRecord[5]=16;laneRecord[11]=1;laneRecord[21]=100;laneRecord[23]=255;
  const laneOffset=source.notesEnd+2;
  const withLanes=Uint8Array.from([...bytes.slice(0,laneOffset),76,1,1,0,...laneRecord,...bytes.slice(laneOffset)]);
  const laneSource=decodeChordProject(withLanes);
  assert(sameBytes(encodeChordProject(laneSource,laneSource.recipes),withLanes),'Chord editing preserves lane rhythm extension');
  const withManual=Uint8Array.from([...withLanes.slice(0,laneOffset),65,1,1,...withLanes.slice(laneOffset)]);
  const manualSource=decodeChordProject(withManual);
  assert(sameBytes(encodeChordProject(manualSource,manualSource.recipes),withManual),'Chord editing preserves CC Manual mode');
  const invalidManual=withManual.slice();invalidManual[laneOffset+2]=2;assert.throws(()=>decodeChordProject(invalidManual));
  assert.equal(source.recipes.length,2);assert.deepEqual(recipeNotes(source.recipes[0]),p.chords[0].notes);
  assert(sameBytes(encodeChordProject(source,source.recipes),bytes));
  for(let i=0;i<bytes.length;i++)assert.throws(()=>decodeChordProject(bytes.slice(0,i)));
  const overlap=structuredClone(source.recipes);overlap[1].start=1;assert.throws(()=>encodeChordProject(source,overlap),/overlapping/);
  const range=structuredClone(source.recipes);range[0].root=127;assert.throws(()=>encodeChordProject(source,range));
  const preserved={...source,foreign:[{start:7,end:7,pitch:20,velocity:40}]};assert.throws(()=>encodeChordProject(preserved,source.recipes),/overlaps/);
  const shortened=structuredClone(source.recipes);shortened[1].length=2;const withForeign=decodeChordProject(encodeChordProject(preserved,shortened));assert.equal(withForeign.foreign[0].pitch,20);
  const arp=deriveNotes(p,{pattern:'arp-up',stride:2});assert.equal(editableRecipes(arp).length,0);
  const inverted=deriveNotes(buildProgression([{candidate:0,duration:4,inversion:1}]));assert.equal(editableRecipes(inverted).length,1);assert.equal(editableRecipes(inverted)[0].root,60,'Explicit provider root survives inversion');assert.equal(editableRecipes(inverted,{inversions:false}).length,0,'Older firmware receives baked notes');
  const invertedSource=decodeChordProject(encodeChordTrack(inverted,0,{editable:true}));assert.equal(invertedSource.recipes[0].inversion,1);assert.deepEqual(recipeNotes(invertedSource.recipes[0]),[64,67,72]);assert(sameBytes(encodeChordProject(invertedSource,invertedSource.recipes),invertedSource.bytes));assert.throws(()=>recipeNotes({...invertedSource.recipes[0],inversion:3}));assert.throws(()=>decodeChordProject(encodeChordTrack(inverted,0,{editable:true,inversions:false})));
  const shifted=deriveNotes(p,{octave:-1});assert.equal(decodeChordProject(encodeChordTrack(shifted,0,{editable:true})).recipes[0].root,source.recipes[0].root-12);
}
console.log('Workshop recipe read/edit/write, foreign notes, scale context and companion output passed.');

{
  const {conformRecipesToScale,recipeNotes,decodeProjectScale,sameScale}=await import('./chord-project.mjs?v=3aa3001b5b');
  const {scaleNotes}=await import('./scale.mjs');
  const original=[{start:0,length:6,root:61,type:0,velocity:73,scaleRoot:2,scaleKind:1},{start:8,length:8,root:66,type:8,velocity:101,scaleRoot:2,scaleKind:1}];
  const copy=structuredClone(original),scale={enabled:true,root:0,kind:0},next=conformRecipesToScale(original,16,scale);
  assert.deepEqual(original,copy,'Preview cannot mutate source recipes');assert.deepEqual(next.map(c=>c.root),[60,65],'Nearest roots choose lower on ties');assert.deepEqual(next.map(c=>c.type),[10,11]);
  for(let i=0;i<2;i++)for(const key of ['start','length','velocity'])assert.equal(next[i][key],original[i][key]);
  for(let kind=0;kind<11;kind++)for(let root=0;root<12;root++)for(const c of conformRecipesToScale(original,16,{enabled:true,root,kind}))assert(recipeNotes(c).every(n=>scaleNotes(root,kind).includes(n%12)));
  assert.throws(()=>conformRecipesToScale(original,16,{...scale,enabled:false}),/Enable/);
  const high=[{...original[0],root:121,type:2}];assert.equal(recipeNotes(high[0]).length,3);assert.throws(()=>conformRecipesToScale(high,16,{enabled:true,root:2,kind:3}));
  assert.deepEqual(original,copy);
  const global=new Uint8Array(25);global[0]=1;global.set([71,1,1,2,7],20);assert.deepEqual(decodeProjectScale(global),{enabled:true,root:2,kind:7});
  assert(sameScale(scale,{...scale,revision:99}));assert(!sameScale(scale,{...scale,root:1}));assert(!sameScale(scale,{...scale,enabled:false}));
  const {controlsFor}=await import('./keyboard.mjs?v=11bbe2f648');for(const mode of ['drum','melodic','chord-step','cc','scale','navigation'])assert.match(controlsFor(mode).LCtrl.detail,/tempo/);
}
console.log('Global-scale chord preview: roots, chord size, all scales, immutable timing and stale-context checks passed.');

{
  const {customRecipe,recipeNotes,recipeProgression,decodeChordProject,encodeChordProject,quantizeRecipesToScale,adoptionCandidates,adoptChord}=await import('./chord-project.mjs?v=3aa3001b5b');
  const {encodeChordTrack,deriveNotes,buildProgression,editableRecipes,quantizeProgression}=await import('./harmony.mjs?v=5bc5306d12');
  const {scaleNotes}=await import('./scale.mjs');
  const c=customRecipe([48,60,61,64,67,74,78,81],64,'Wide',{start:0,length:4,velocity:83});
  assert.deepEqual(recipeNotes(c),[48,60,61,64,67,74,78,81]);assert.deepEqual(recipeNotes({...c,inversion:1}),[60,61,64,67,72,74,78,81]);
  const quantized=quantizeRecipesToScale([c],8,{enabled:true,root:0,kind:0})[0];assert.equal(quantized.root,64);assert.deepEqual(quantized.intervals,c.intervals);assert(recipeNotes(quantized).length<c.intervals.length);assert.deepEqual(recipeNotes({...quantized,quantize:false}),recipeNotes(c));
  for(let root=0;root<12;root++)for(let kind=0;kind<11;kind++)for(let inversion=0;inversion<8;inversion++){const notes=recipeNotes({...c,quantize:true,scaleRoot:root,scaleKind:kind,inversion});assert.equal(new Set(notes).size,notes.length);assert(notes.every(n=>scaleNotes(root,kind).includes(n%12)));}
  for(const bad of [{intervals:[0,0]},{intervals:[0]},{intervals:Array(9).fill(0)},{name:'a'.repeat(13)},{name:'😎'},{root:0}])assert.throws(()=>recipeNotes({...c,...bad}));
  const plain=decodeChordProject(encodeChordTrack(recipeProgression([c],8),1),{allowUnowned:true});const candidates=adoptionCandidates(plain);assert.equal(candidates.length,1);assert.throws(()=>adoptChord(plain,[],candidates[0],undefined,'Custom'),/root/);
  const adopted=adoptChord(plain,[],candidates[0],64,'Chosen');assert.equal(adopted.source.foreign.length,0);assert.equal(plain.foreign.length,8);assert.deepEqual(decodeChordProject(encodeChordProject(adopted.source,adopted.recipes)).events,plain.events);
  const ambiguous={...plain,events:[...plain.events,{start:1,end:2,pitch:30,velocity:44}],foreign:[...plain.foreign,{start:1,end:2,pitch:30,velocity:44}]};assert.equal(adoptionCandidates(ambiguous).length,0);
  const varied={...plain,foreign:plain.foreign.map((e,i)=>({...e,velocity:i?83:84}))};assert.equal(adoptionCandidates(varied).length,0);
  const bank=defaultModule('chord-bank'),progression=deriveNotes(buildProgression([{candidate:0,duration:4,inversion:2}],{provider:'harmony.bank',bank}));assert.equal(editableRecipes(progression)[0].root,bank.config.slots[0].root);assert.equal(editableRecipes(progression,{custom:false}).length,0);
  delete bank.config.slots[0].root;const unknown=deriveNotes(buildProgression([{candidate:0,duration:4}],{provider:'harmony.bank',bank}));assert.equal(editableRecipes(unknown).length,0,'Never infer unknown bank root');
  const transformed=deriveNotes(quantizeProgression(buildProgression([{candidate:0,duration:4}],{root:1,kind:0}),0,0));assert.equal(editableRecipes(transformed,{custom:false}).length,0);assert.equal(editableRecipes(transformed).length,1);
  const {decodeChordMode}=await import('./chord-mode.mjs?v=e16e3e7465');const live=new Uint8Array(448);live[0]=3;live[2]=1;live[3]=64;live[4]=12;live[5]=4;live[12]=3;live.set([60,64,67],16);live[26]=16;live[31]=1;live[32]=3;live.set([252,0,3],33);live.set([...new TextEncoder().encode('Colour')],41);live[53]=94;live[64]=3;live[65]=64;live[66]=12;live[69]=4;live[72]=1;live[73]=3;live.set([...new TextEncoder().encode('Colour')],74);
  const decoded=decodeChordMode(live);assert.equal(decoded.name,'Colour');assert.equal(decoded.steps[0].name,'Colour');assert.deepEqual(decoded.intervals,[-4,0,3]);live[32]=9;assert.throws(()=>decodeChordMode(live));
}
console.log('Custom chords, explicit bank roots, adoption, quantization recovery, C3 compatibility and telemetry passed.');

// Chord telemetry is a second snapshot: crossing a bar must not freeze a melodic view.
{
  const base=new Uint8Array(960);base[0]=5;base[1]=3;base[3]=1;base[6]=255;base[10]=17;base[12]=32;base[14]=1;base[58]=base[59]=255;base[60]=2;base[62]=7;base[63]=1;base[638]=4;base[898]=255;base.fill(255,936,952);
  const chord=new Uint8Array(192);chord[0]=2;chord[3]=60;chord[5]=1;chord[8]=chord[9]=255;chord[26]=32;chord[62]=8;chord[63]=1;
  const calls=[],statuses=[],states=[];
  const monitor=new LiveMonitor({request:async(cmd,obj=0,offset=0)=>{calls.push([cmd,offset]);return(cmd===17?base:chord).slice(offset,offset+64);}},{onState:s=>states.push(s),onStatus:s=>statuses.push(s)});
  monitor.stopped=false;await monitor.tick();monitor.stop();assert.equal(states[0].bar,1);assert.equal(states[0].step,17);assert.equal(calls.filter(c=>c[0]===22).length,1,'Inactive chord mode needs no step pages');assert.deepEqual(statuses,['live']);
  chord[2]=1;monitor.stopped=false;states.length=0;statuses.length=0;await monitor.tick();monitor.stop();assert.equal(states.length,0,'Do not combine chord labels from another bar');assert.deepEqual(statuses,[],'Bar transition is not a lost USB signal');
  chord[24]=1;monitor.stopped=false;await monitor.tick();monitor.stop();assert.equal(states[0].bar,1);assert.equal(states[0].chordMode.bar,1);
  const {liveBarLabel}=await import('./steps.mjs?v=175bf85458');
  assert.match(liveBarLabel(states[0]),/VIEW BAR 2 \/ 2 · STEPS 17–32/);
  assert.match(liveBarLabel({...states[0],bar:0}),/Playing bar 2 · Space \+ F to follow/);
  assert.doesNotMatch(liveBarLabel({...states[0],bar:0,running:false}),/Playing/);
}
console.log('Multi-bar live view, inactive chord paging and cross-snapshot bar transitions passed.');

{
  const {recipeNotes,setChordScaleFree,conformRecipesToScale}=await import('./chord-project.mjs?v=3aa3001b5b');
  const {chordLabel,scaleChordTypes,decodeChordMode}=await import('./chord-mode.mjs?v=e16e3e7465');
  const {scaleNotes}=await import('./scale.mjs');
  const c={root:67,type:10,start:0,length:4,velocity:100,scaleRoot:5,scaleKind:5,inversion:0};
  assert.deepEqual(recipeNotes(c),[67,70,73]);assert.equal(chordLabel(c),'Gdim');
  assert.deepEqual(recipeNotes({...c,type:11}),[67,70,73,77]);assert.equal(chordLabel({...c,type:11}),'Gm7♭5');
  assert.deepEqual(recipeNotes({...c,type:16,scaleFree:true}),[67,70,73,76]);
  for(let kind=0;kind<11;kind++)for(let root=48;root<60;root++)if(scaleNotes(5,kind).includes(root%12))for(const type of scaleChordTypes){const notes=recipeNotes({...c,root,type,scaleKind:kind});assert(notes.every(n=>scaleNotes(5,kind).includes(n%12)));}
  const free=setChordScaleFree(c,true,{enabled:true,root:5,kind:5});assert(free.scaleFree);assert.deepEqual(recipeNotes(free),recipeNotes(c));
  assert(!setChordScaleFree(free,false,{enabled:true,root:5,kind:5}).scaleFree);assert.throws(()=>setChordScaleFree(free,false,{enabled:false}));
  const snap=new Uint8Array(448);snap[0]=4;snap[2]=1;snap[3]=67;snap[4]=10;snap[5]=4;snap[12]=3;snap[14]=5;snap[15]=5;snap.set([67,70,73],16);snap[26]=16;snap[32]=3;snap[53]=100;snap[57]=1;snap[64]=3;snap[65]=67;snap[66]=10;snap[69]=4;snap[73]=3;snap[86]=5;snap[87]=5;
  const live=decodeChordMode(snap);assert(live.scaleBoundSupported);assert.equal(chordLabel(live.steps[0]),'Gdim');
  snap[56]=2;assert.throws(()=>decodeChordMode(snap));snap[56]=0;snap[86]=12;assert.throws(()=>decodeChordMode(snap));
}
console.log('Scale-bound chord degrees, accurate quality labels, single-chord escape and v4 snapshots passed.');

{
  const {paletteRemovePayload}=await import('./palette.mjs?v=831d6385ec');
  const palette={removeSupported:true,root:5,saved:[{offset:0},{offset:-5},{offset:2}]};
  assert.deepEqual([...paletteRemovePayload(palette,1)],[3,1,3,5,0,251,2,0,0,0,0,0]);
  assert.throws(()=>paletteRemovePayload(palette,3));
  assert.throws(()=>paletteRemovePayload({...palette,removeSupported:false},0));
  const bytes=new Uint8Array(960);bytes[0]=5;bytes[1]=1;bytes[3]=1;bytes[6]=255;bytes[12]=16;bytes[62]=1;bytes[63]=1;bytes[896]=1;bytes[898]=255;bytes.fill(255,936,952);bytes[958]=13;bytes[959]=5;
  assert.equal(decodePalette(bytes).palette.removeSupported,true);assert.equal(decodePalette(bytes).palette.root,5);
  bytes[958]=5;assert.equal(decodePalette(bytes).palette.removeSupported,false);
}
console.log('Individual palette removal payload and capability passed.');

const {tempPresentation}=await import('./temp.mjs?v=f3aa4a44c5');
{
 const bytes=new Uint8Array(960);bytes[0]=5;bytes[1]=1;bytes[6]=255;bytes[12]=16;bytes[62]=1;bytes[63]=1;bytes[898]=255;bytes.fill(255,936,952);
 assert.equal(decodePalette(bytes).temp,null);
 for(const bits of [16,48,112]){bytes[958]=bits;const state=decodePalette(bytes);assert.equal(state.temp.active,!!(bits&32));assert.equal(state.temp.queued,!!(bits&64));assert.equal(tempPresentation(state).mode,bits===112?'queued':bits===48?'active':'idle');}
 assert.equal(tempPresentation(decodePalette(bytes),false).mode,'unknown');assert(tempPresentation(decodePalette(bytes),false).blocked);
 assert.equal(tempPresentation(null).mode,'offline');assert.equal(tempPresentation({active:true}).mode,'unsupported');
 assert.equal(tempPresentation({active:false,temp:{active:false,queued:false}}).mode,'off');
}
console.log('TEMP compatibility, live states and transfer blocking passed.');
{
 const bytes=new Uint8Array(960);bytes[0]=5;bytes[1]=1;bytes[6]=255;bytes[12]=16;bytes[62]=1;bytes[63]=1;bytes[898]=255;bytes.fill(255,936,952);bytes[958]=16;
 bytes[902]=5;
 assert.equal(decodePalette(bytes).loopSteps,0,'Old firmware does not interpret reserved bytes');
 bytes[638]=128;
 const state=decodePalette(bytes);
 assert(state.globalLoopSupported);assert.equal(state.loopSteps,5);assert.equal(state.length,16);
 const {liveOrbitSteps}=await import('./orbit.mjs?v=fe48e35c75');
 assert.equal(liveOrbitSteps(state,true).filter(s=>!s.outside).length,5);
 assert.equal(liveOrbitSteps({...state,loopSteps:0},true).filter(s=>!s.outside).length,16);
 assert.equal(tempPresentation(state).label,'Global loop · 5 steps');assert.equal(tempPresentation(state).blocked,false);
 assert.match(tempPresentation(state).detail,/Release Enter/);
 assert.equal(tempPresentation(state,false).mode,'unknown');
 assert.equal(tempPresentation({...state,temp:{active:true,queued:false}}).blocked,true);
 assert.match(tempPresentation({...state,temp:{active:true,queued:true}}).detail,/still queued/);
 bytes[902]=0;assert.equal(decodePalette(bytes).loopSteps,0);assert.notEqual(tempPresentation(decodePalette(bytes)).mode,'loop');
 bytes[902]=1;bytes[903]=8;assert.throws(()=>decodePalette(bytes),/Invalid global loop/);
}
console.log('Global loop capability, temporary status and legacy fallback passed.');


{
 const {decodeMuteSummary}=await import('./live.mjs?v=b5fe2a212b');
 const {trackMuteStatus}=await import('./track-status.mjs');
 const header=new Uint8Array(64);
 assert.equal(decodeMuteSummary(header),null);
 header[5]=1;header[6]=2;header[8]=4;
 const mutes=decodeMuteSummary(header);assert.deepEqual(mutes,{muted:2,partial:4});
 assert.equal(trackMuteStatus({mutes},true,0).mode,'clear');
 assert.equal(trackMuteStatus({mutes},true,1).mode,'muted');
 assert.equal(trackMuteStatus({mutes},true,2).mode,'partial');
 assert.equal(trackMuteStatus({mutes},false,1).mode,'unknown');
 assert.equal(trackMuteStatus({},true,1).mode,'unknown');
 header[8]=2;assert.throws(()=>decodeMuteSummary(header));header[8]=0;header[7]=16;assert.throws(()=>decodeMuteSummary(header));
 const palette=new Uint8Array(960);palette[0]=5;palette[6]=255;palette[12]=16;palette[60]=1;palette[62]=9;palette[63]=1;palette[898]=255;palette.fill(255,936,952);palette[637]=1;palette[958]=128;
 header.fill(0);header[0]=1;header[5]=1;header[6]=2;header[8]=4;header[62]=3;header[63]=1;
 const calls=[],states=[];
 const device={request:async(cmd,obj=0,offset=0)=>{calls.push([cmd,offset]);if(cmd===21){assert.equal(offset,0);return header;}return palette.slice(offset,offset+64);}};
 const monitor=new LiveMonitor(device,{onState:s=>states.push(s),onStatus:()=>{}});monitor.stopped=false;
 await monitor.tick();clearTimeout(monitor.timer);
 assert.deepEqual(states.at(-1).mutes,{muted:2,partial:4});assert.equal(states.at(-1).cc,undefined);assert.equal(calls.filter(c=>c[0]===21).length,1);
 calls.length=0;palette[958]=0;await monitor.tick();monitor.stop();assert.equal(calls.filter(c=>c[0]===21).length,0);assert.equal(states.at(-1).mutes,undefined);
}
console.log('Mute telemetry, legacy fallback, stale indicators and header-only polling passed.');

// Capacity telemetry is independently sampled, never falls back to transfer/status
// commands, and hides stale or malformed values without losing musical telemetry.
{
 const {decodeProjectHealth,projectHealthPresentation}=await import('./project-health.mjs?v=23482001a9');
 const bytes=new Uint8Array(42),word=(i,n)=>{bytes[i]=n&255;bytes[i+1]=n>>8};
 bytes.set([1,16|32|64,0,0,2,4,48,128]);
 for(const [i,n] of [[8,12],[10,24],[12,8],[14,64],[16,40],[18,512],[20,8],[22,128],[24,2],[26,16],[28,2100],[30,3072],[32,4100],[34,7424],[38,500],[40,6144]])word(i,n);
 const health=decodeProjectHealth(bytes),state={active:true,temp:{active:false,queued:false},health};
 assert.equal(health.notes,12);assert.equal(health.saveBytes,2100);
 assert.equal(projectHealthPresentation(state).label,'Saved on keyboard');
 assert.equal(projectHealthPresentation({...state,health:{...health,dirty:true}}).label,'Unsaved changes');
 assert.equal(projectHealthPresentation({...state,health:{...health,saved:false}}).label,'Not saved on keyboard');
 assert.equal(projectHealthPresentation({...state,health:{...health,saveStatus:3,saved:false}}).label,'On-board save invalid');
 assert.equal(projectHealthPresentation({...state,temp:{active:true}}).label,'TEMP edits');
 assert.equal(projectHealthPresentation(state,false).rows.length,0);
 assert.equal(projectHealthPresentation({healthSupported:false}).label,'Project status unavailable');
 const over={...health,dirty:true,saveBytes:5000,tempBytes:9000,tempReady:false,saveStatus:2};
 assert.equal(projectHealthPresentation({...state,health:over}).tone,'warning');
 assert.match(projectHealthPresentation({...state,health:over}).detail,/previous save was preserved/);
 assert.equal(tempPresentation(state).label,'TEMP ready');
 assert.equal(tempPresentation({...state,health:over}).mode,'full');
 assert.equal(tempPresentation({...state,health:{...health,tempReady:false}}).mode,'busy');
 assert.equal(tempPresentation({...state,health:null}).label,'TEMP capacity unknown');
 assert.throws(()=>decodeProjectHealth(bytes.slice(1)));
 for(const offset of [0,2,3,4,5]){const bad=bytes.slice();bad[offset]=offset===0?2:255;assert.throws(()=>decodeProjectHealth(bad));}
 for(const offset of [6,7,14,18,26]){const bad=bytes.slice();bad[offset]=0;if(offset>=14)bad[offset+1]=0;assert.throws(()=>decodeProjectHealth(bad));}
 const calls=[],live=new Uint8Array(60);live[0]=1;live[6]=255;live[12]=16;
 let failure=null;
 const device={request:async(cmd,track)=>{calls.push(cmd);if(cmd===25){if(failure)throw failure;const result=bytes.slice();result[3]=track;return result;}assert.equal(cmd,11);return live;}};
 const monitor=new LiveMonitor(device,{onState:()=>{},onStatus:()=>{},wantHealth:()=>true});
 monitor.paletteSupported=monitor.rhythmSupported=monitor.detailSupported=false;monitor.stopped=false;
 assert.equal((await monitor.readState(null)).health.notes,12);
 await monitor.readState(null);assert.equal(calls.filter(c=>c===25).length,1);
 live[2]=3;assert.equal((await monitor.readState(null)).health.track,3);
 failure=Error('timeout');monitor.healthAt=0;
 assert.equal((await monitor.readState(null)).health,null);
 await monitor.readState(null);assert.equal(calls.filter(c=>c===25).length,3);
 monitor.resume();failure=Object.assign(Error('unsupported'),{status:1});
 assert.equal((await monitor.readState(null)).healthSupported,false);
 await monitor.readState(null);assert.equal(calls.filter(c=>c===25).length,4);
 assert(calls.every(c=>c===11||c===25));monitor.stop();
}
console.log('Project capacity decoding, save/TEMP states, stale clearing, polling and legacy fallback passed.');

{
  const {paletteChord,paletteQualityPayload}=await import('./palette-chords.mjs?v=b0a37ac77a');
  const scale={enabled:true,root:0,kind:0,mode:0,direction:0};
  assert.deepEqual(paletteChord({note:60},1,scale).notes,[60,63,67]);
  assert.deepEqual(paletteChord({note:62},11,scale).notes,[62,65,69,72]);
  assert.match(paletteChord({note:62},11,scale).label,/Auto seventh → Minor 7/);
  assert.deepEqual(paletteChord({note:62},11,{...scale,mode:1,localRoot:2,localKind:0}).notes,[62,66,69,73]);
  assert.deepEqual(paletteChord({note:62},11,{...scale,enabled:false}).notes,[62]);
  assert.deepEqual(paletteChord({note:125},0,scale).notes,[]);
  for(const composed of [false,true])for(const pitchMode of ['palette','transpose','chords']){
    let module=defaultModule('euclid');module.version=4;module.config.pitchMode=pitchMode;module.config.qualities=[0,1,16,10,11,8,7,6];
    if(composed)module=composeModule(module,rhythmComponent(module));
    const wire=encodeModule(module);assert(sameBytes(encodeModule(decodeModule(wire)),wire));
    for(let n=0;n<wire.length;n++)assert.throws(()=>decodeModule(wire.slice(0,n)));
    const bad=wire.slice();bad[bad.length-1]=12;assert.throws(()=>decodeModule(bad));
    const split=splitModule(module);assert.equal(split.module.version,4);assert.deepEqual(split.module.config.qualities,[0,1,16,10,11,8,7,6]);
  }
  const bytes=new Uint8Array(960);bytes[0]=5;bytes[1]=1;bytes[3]=1;bytes[6]=255;bytes[12]=16;bytes[62]=1;bytes[63]=1;bytes[896]=1;bytes[897]=2;bytes[898]=255;bytes[912]=60;bytes[913]=62;bytes.fill(255,936,952);bytes[954]=2;bytes[955]=2;bytes[920]=0;bytes[921]=1;bytes[928]=2;
  const p=decodePalette(bytes).palette;assert(p.chordSupported);assert.equal(p.editDigit,2);assert.equal(p.mode,'chords');assert.equal(p.qualities[1],1);
  const payload=paletteQualityPayload(p,1,8);assert.equal(payload.length,21);assert.equal(payload[13],1);assert.equal(payload[20],8);
  const {controlsFor}=await import('./keyboard.mjs?v=11bbe2f648');assert.equal(controlsFor('euclid','mac',false,true,false,p).S.label,'Minor');
  bytes[921]=12;assert.throws(()=>decodePalette(bytes));bytes[921]=1;bytes[928]=9;assert.throws(()=>decodePalette(bytes));
}
console.log('Chord palette quality, scale, labels, codecs and stale-edit payload tests passed.');

{
  const {variationBytes,decodeVariation,variationPayload,defaultVariation}=await import('./variation.mjs?v=90308f36a3');
  const value={...defaultVariation(),seed:4294967295,enabled:true,order:4,mutation:100,range:24,voicing:4};
  const wire=new Uint8Array(32);wire[0]=1;wire[31]=1;wire.set(variationBytes(value),5);wire.set(variationBytes(value),16);
  assert.deepEqual(decodeVariation(wire).current,value);
  for(const [field,invalid] of [['order',5],['mutation',101],['range',25],['voicing',5],['rotate',128],['seed',-1]])assert.throws(()=>variationBytes({...value,[field]:invalid}));
  assert.equal(variationPayload(0,1,true,65535,value).length,16);
  assert.equal(variationPayload(5,0,true,1,{enabled:true,random:false,run:2,backward:true})[2],0);
  for(let i=0;i<32;i++)assert.throws(()=>decodeVariation(wire.slice(0,i)));
  const {encodeChordTrack,deriveNotes,buildProgression}=await import('./harmony.mjs?v=5bc5306d12');
  const {decodeChordProject,encodeChordProject}=await import('./chord-project.mjs?v=3aa3001b5b');
  const base=encodeChordTrack(deriveNotes(buildProgression([{candidate:0,duration:16}])),0,{editable:true});
  const source=decodeChordProject(base),extension=Uint8Array.of(86,1,...variationBytes(value));
  const varied=Uint8Array.of(...base.slice(0,source.recipeEnd),...extension,...base.slice(source.recipeEnd));
  const decoded=decodeChordProject(varied),rewritten=encodeChordProject(decoded,decoded.recipes);
  assert.deepEqual(rewritten,varied,'Chord workshop preserves variation settings');
}
console.log('Variation validation, protocol, project persistence and chord-workshop preservation passed.');

{
  const {paletteSlotForStep,paletteNavigationSnapshot,paletteWantsView}=await import('./palette-links.mjs?v=b388aa571a');
  const s={active:true,type:1,child:false,engine:0,bar:1,length:32,pattern:0,
    euclid:{euclidean:true,enabled:true,generated:0b1111},
    palette:{root:0,mode:'chords',pin:null,editDigit:null,overrides:2,saved:[{note:60,offset:0},{note:60,offset:12}],slots:[0,1,1,255],qualities:[0,1]}};
  assert.equal(paletteSlotForStep(s,0),0);
  assert.equal(paletteSlotForStep(s,1),null,'Recorded notes override palette chords');
  assert.equal(paletteSlotForStep(s,2),1,'Same pitch still identifies its own source slot');
  assert.equal(paletteSlotForStep(s,3),null);
  assert.equal(paletteSlotForStep({...s,loopSteps:18},2),null,'Global loop truncates projected hits');
  assert.equal(paletteSlotForStep({...s,euclid:{...s.euclid,enabled:false}},0),null);
  assert.equal(paletteSlotForStep({...s,euclid:{...s.euclid,generated:0}},0),null,'Rests have no palette link');
  assert.equal(paletteSlotForStep({...s,palette:{...s.palette,mode:'transpose'}},1),1,'Recorded phrases still use palette transpositions');
  for(const patch of [{active:false},{type:0},{child:true},{engine:1}])assert.equal(paletteSlotForStep({...s,...patch},0),null);
  s.track=0;
  assert(!paletteWantsView(null,paletteNavigationSnapshot(undefined)),'Offline desktop startup has no palette gesture');
  const snap=()=>paletteNavigationSnapshot(s);let previous=snap();
  assert(!paletteWantsView(null,previous),'Initial polling does not force the palette open');
  s.step=7;s.palette.slots=[1,0];assert(!paletteWantsView(previous,snap()),'Playback does not reopen palette');
  for(const patch of [{held:true,shift:true},{capturing:true},{capturing:false,held:false,shift:false,editDigit:2},{editDigit:3},{pin:1},{mode:'transpose'},{replace:true},{qualities:[1,1]},{saved:[{note:62,offset:2}]}]){
    Object.assign(s.palette,patch);const next=snap();assert(paletteWantsView(previous,next),JSON.stringify(patch));
    assert(!paletteWantsView(next,next),'Repeated snapshot respects manual tab selection');previous=next;
  }
  s.palette.editDigit=null;assert(!paletteWantsView(previous,snap()),'Releasing chord key does not steal the view');
  previous=snap();s.track=1;assert(!paletteWantsView(previous,snap()),'Ordinary track changes open rhythm');
  s.palette.held=s.palette.shift=true;assert(paletteWantsView(previous,snap()),'Editing another track opens its palette');
}
console.log('Palette source-slot mapping and automatic editor navigation passed.');

{
  const {controlsFor}=await import('./keyboard.mjs?v=11bbe2f648');
  const nav=controlsFor('navigation');
  assert.equal(nav.F.label,'Follow');
  assert.match(nav.F.detail,/Space \+ F/);
  assert.equal(nav['='].label,'Double length');
  assert.match(nav['='].detail,/Shift.*empty/);
  assert.match(nav.Knob.detail,/Ctrl \+ Space.*one step.*Shift.*16 steps/);
}
console.log('Space navigation, length and doubling help passed.');
