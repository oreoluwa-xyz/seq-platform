// Firmware bytes and USB DFU remain in the local companion; MIDI stays in the browser.
export function decodeFirmwareIdentity(bytes) {
  if(bytes.length!==113||String.fromCharCode(...bytes.slice(0,8))!=='SEQFWID1')throw Error('Invalid firmware identity response.');
  const field=(start,end)=>{const raw=bytes.slice(start,end),zero=raw.indexOf(0);if(zero<1||raw.slice(zero).some(n=>n!==0)||raw.slice(0,zero).some(n=>n<32||n>126))throw Error('Malformed firmware identity.');return String.fromCharCode(...raw.slice(0,zero));};
  const identity={version:field(8,32),target:field(32,80),buildId:field(80,113)};
  if(!/^[A-Za-z0-9][A-Za-z0-9.+_-]{0,22}$/.test(identity.version)||!/^[0-9a-f]{32}$/.test(identity.buildId)||/^0+$/.test(identity.buildId))throw Error('Invalid firmware build identity.');
  return identity;
}
export async function verifyFirmware(device, release) {
  await device.capabilities();
  const first=await device.request(17);
  if(first.length!==64||!first[62])throw Error('Incomplete live-state header.');
  const page=await device.request(17,first[62],576);
  if(page.length!==64||(page[62]&release.flags)!==release.flags)throw Error('The keyboard does not report this release’s required chord features.');
  const chord=await device.request(22);
  if(chord.length!==64||chord[0]!==release.snapshot||!chord[62])throw Error('The chord-state version does not match this release.');
  const length=release.snapshot>=3?448:192;
  for(let offset=64;offset<length;offset+=64){
    const chunk=await device.request(22,chord[62],offset);
    if(chunk.length!==64)throw Error('Incomplete chord-state response.');
  }
  if(!release.identity)return 'Keyboard responding · expected chord features verified. This legacy release has no build identity; the exact installed release cannot be verified.';
  let identity;
  try {
    const head=await device.request(24),tail=await device.request(24,0,64);
    if(head.length!==64||tail.length!==49)throw Error('Incomplete firmware identity.');
    identity=decodeFirmwareIdentity(new Uint8Array([...head,...tail]));
  } catch(error) {throw Error(`Cannot verify the installed build. The keyboard may still be running older firmware. ${error.message}`);}
  if(identity.target!==release.target||identity.target!==release.identity.target)throw Error(`Firmware board target mismatch: keyboard reports ${identity.target}.`);
  if(identity.buildId!==release.identity.buildId||identity.version!==release.identity.version)throw Error(`Installed firmware differs from the selected release: ${identity.version} · build ${identity.buildId}.`);
  return `Verified installed firmware · ${identity.version} · build ${identity.buildId} · ${identity.target}. Expected chord features verified.`;
}

export function setupFirmware({button,dialog,connected,backup,disconnect,verify,fetcher=fetch}) {
  if(dialog.ownerDocument.querySelector('meta[name="seq-hosting"][content="static"]')) {
    dialog.innerHTML=`<div class="firmware-heading"><div><div class="eyebrow">INSTRUMENT / MAINTENANCE</div><h2>Firmware.</h2></div><button data-close aria-label="Close firmware manager">Close</button></div><div class="firmware-body"><h3>Install firmware with the desktop app</h3><p>This hosted Studio can edit modules and connect over Web MIDI in a supported browser. Firmware installation requires the SEQ desktop app or the local firmware service on your computer.</p><p><a href="https://github.com/oreoluwa-xyz/seq-platform/releases" target="_blank" rel="noopener">Desktop app & firmware releases ↗</a> · GitHub repository access required.</p><p>Your drafts and backups stay in this browser. They are separate from the desktop app’s local workspace.</p></div>`;
    button.onclick=()=>dialog.showModal();
    dialog.querySelector('[data-close]').onclick=()=>dialog.close();
    return {refresh:async()=>{},render:()=>{}};
  }
  dialog.innerHTML=`<div class="firmware-heading"><div><div class="eyebrow">INSTRUMENT / MAINTENANCE</div><h2 id="firmware-title">Firmware.</h2><p>Keep your instrument up to date.</p></div><button data-close aria-label="Close firmware manager">Close</button></div>
  <div class="firmware-body">
    <div class="firmware-device"><div><span class="eyebrow">SUPPORTED INSTRUMENT</span><strong>Keychron V1 Max</strong><span>ANSI · encoder · seq_test</span></div><div><span class="eyebrow">KEYBOARD CONNECTION</span><strong data-connection></strong><span>USB cable required for updates</span><button data-check>Connect & check MIDI</button></div></div><p data-current-check class="hint" role="status"></p>
    <p data-release-identity class="hint"></p>
    <p data-message class="firmware-message" role="status" aria-live="polite"></p>
    <section data-service-help hidden><h3>Start the local firmware service</h3><p>Run this from the seq_test folder, replacing the plain file server on port 8765:</p><pre>python3 tools/seq_server.py serve</pre><p>Then reopen this page. The service uses QMK Toolbox’s dfu-util on macOS, or dfu-util from your PATH.</p></section>
    <div class="firmware-workflow">
      <section><div class="firmware-step"><span>01</span><h3>Choose a release</h3></div><label>Available on this computer<select data-release></select></label><p data-empty hidden>No registered builds yet. Register a compiled V1 Max seq_test image using the command below.</p><p data-notes class="firmware-notes"></p><dl class="firmware-facts"><dt>Size</dt><dd data-size>—</dd><dt>SHA-256</dt><dd data-hash>—</dd></dl><a data-download hidden>Download .bin ↗</a><details><summary>Register a build</summary><p>Only register firmware built for this exact board. The service checks image bounds and integrity; it does not authenticate its publisher.</p><pre>python3 tools/seq_server.py register /path/to/firmware.bin \\
  --target keychron/v1_max/ansi_encoder:seq_test \\
  --label "Release name"</pre><p>Previously registered builds stay available for reinstalling or rolling back.</p></details></section>
      <section><div class="firmware-step"><span>02</span><h3>Prepare the keyboard</h3></div><p>Back up before entering the bootloader. Restarting discards unsaved RAM changes.</p><button data-backup>Back up keyboard project</button><p data-backup-status class="hint">Downloads the full project and retains a browser recovery copy.</p><ol><li>Set the keyboard’s switch to <strong>Cable</strong>.</li><li>Unplug USB. Hold <strong>Esc</strong> (or the reset button under the space bar) while reconnecting.</li><li>Release the key, then detect the bootloader below.</li></ol><label class="check"><input data-prepared type="checkbox"> I have kept my backup, or I don’t need the current unsaved project.</label></section>
    </div>
    <section class="firmware-install"><div class="firmware-step"><span>03</span><h3>Install & reconnect</h3></div><div class="firmware-target"><label>Bootloader<select data-device><option value="">Not detected</option></select></label><button data-refresh>Detect / refresh</button></div><p class="hint">STM32 bootloaders do not identify the keyboard model. Select the serial of the V1 Max you just put into bootloader mode.</p><button data-flash class="primary">Install selected firmware</button><div data-progress-wrap hidden><div class="firmware-progress-label"><strong data-phase></strong><span data-percent></span></div><progress data-progress max="100" value="0"></progress><p>Keep USB connected and the local firmware service running until completion.</p></div><div class="firmware-after" data-after hidden><p>Reconnect USB normally if needed, with no keys held. Verify the keyboard’s MIDI replies below.</p><button data-verify>Reconnect & verify</button><p data-verification role="status"></p></div><details data-log-wrap hidden><summary>Flash log</summary><pre data-log></pre></details></section>
    <p data-history class="hint"></p><p class="hint">Flash history records this computer’s actions. It is not an installed-version readout. Project backups can be restored with tools/seq_backup.py; restore is never automatic.</p>
  </div>`;
  const q=s=>dialog.querySelector(s);
  let status=null,releaseId='',deviceSerial='',timer=null,fetching=false,localBusy=false,flashPending=false,message='',verified='',verificationJob=null;
  const active=()=>status?.job?.state==='flashing'||flashPending;
  const release=()=>status?.releases.find(r=>r.id===releaseId);
  const displayError=e=>{message=e.message||String(e);q('[data-message]').textContent=message;};
  async function api(path,body){
    const response=await fetcher(`/api/firmware/${path}`,{cache:'no-store',signal:AbortSignal.timeout(12000),...(body?{method:'POST',headers:{'Content-Type':'application/json','X-SEQ-Token':status?.token||''},body:JSON.stringify(body)}:{})});
    if(!response.headers.get('content-type')?.includes('application/json'))throw Error('Local firmware service unavailable. Start it with the command below.');
    const value=await response.json();if(!response.ok)throw Error(value.error||'The firmware service rejected this request.');return value;
  }
  function options(select,items,value,empty){
    select.replaceChildren(...(items.length?items:[{id:'',label:empty}]).map(item=>{const option=document.createElement('option');option.value=item.id;option.textContent=item.label;return option;}));select.value=value;
  }
  function render(){
    const r=release(),job=status?.job,working=localBusy||active();
    q('[data-connection]').textContent=connected()?'USB MIDI connected':'MIDI offline';
    q('[data-check]').disabled=working||!r;q('[data-current-check]').textContent=verified;
    q('[data-message]').textContent=message||status?.discoveryError||'Select a release, prepare the keyboard, then install.';
    q('[data-service-help]').hidden=!!status;
    q('[data-release]').disabled=working||!status?.releases.length;
    q('[data-empty]').hidden=!!status?.releases.length;
    q('[data-release-identity]').textContent=r?.identity?`Selected release: ${r.identity.version} · build ${r.identity.buildId} · ${r.identity.target}`:r?'Selected release: legacy build without a reported identity.':'';
    q('[data-notes]').textContent=r?.notes||'';q('[data-size]').textContent=r?`${r.size.toLocaleString()} bytes`:'—';q('[data-hash]').textContent=r?.sha256||'—';
    q('[data-download]').hidden=!r;if(r)q('[data-download]').href=`/api/firmware/releases/${r.id}/bin`;
    q('[data-backup]').disabled=working||!connected();q('[data-prepared]').disabled=working;
    q('[data-device]').disabled=working;q('[data-refresh]').disabled=working||fetching;
    q('[data-flash]').disabled=working||!r||!deviceSerial||!status?.toolAvailable||!q('[data-prepared]').checked;
    q('[data-close]').disabled=localBusy||flashPending;
    q('[data-progress-wrap]').hidden=!job;
    if(job){q('[data-phase]').textContent=job.phase;q('[data-progress]').value=job.progress;q('[data-percent]').textContent=`${job.progress}%`;q('[data-log]').textContent=job.log||'';}
    q('[data-log-wrap]').hidden=!job;q('[data-after]').hidden=job?.state!=='completed';q('[data-verify]').disabled=working;
    q('[data-verification]').textContent=verified;
    const history=status?.lastFlash;
    q('[data-history]').textContent=history?`Last flash on this computer: ${history.release.label} · ${new Date(history.finishedAt*1000).toLocaleString()} · ${history.serial}`:'';
  }
  async function refresh(){
    if(fetching)return;if(localBusy||flashPending){schedule();return;}fetching=true;render();
    try{
      const next=await api('status');if(next.apiVersion!==1)throw Error('Update the local firmware service.');
      const previousJob=status?.job;status=next;
      if(!next.releases.some(r=>r.id===releaseId))releaseId=next.releases[0]?.id||'';
      if(!next.devices.some(d=>d.serial===deviceSerial))deviceSerial=next.devices.length===1?next.devices[0].serial:'';
      options(q('[data-release]'),next.releases,releaseId,'No registered releases');
      options(q('[data-device]'),[{id:'',label:'Choose detected bootloader'},...next.devices.map(d=>({id:d.serial,label:d.label}))],deviceSerial,'Not detected');
      const job=next.job;
      if(job?.id!==verificationJob){verified='';verificationJob=job?.id;}
      if(job?.state==='failed')message=job.error;
      else if(job?.state==='completed'&&(previousJob?.id!==job.id||previousJob?.state!=='completed'))message='Firmware downloaded successfully. Verify MIDI after the keyboard restarts.';
    }catch(error){status=null;deviceSerial='';displayError(error);}
    finally{fetching=false;render();schedule();}
  }
  function schedule(){clearTimeout(timer);if(dialog.open||active())timer=setTimeout(refresh,2500);}
  async function task(fn){if(localBusy||active())return;localBusy=true;message='';render();try{await fn();}catch(error){displayError(error);}finally{localBusy=false;render();}}
  button.onclick=()=>{dialog.showModal();render();refresh();};
  q('[data-close]').onclick=()=>dialog.close();
  dialog.addEventListener('cancel',event=>{if(localBusy||flashPending)event.preventDefault();});
  dialog.addEventListener('close',schedule);
  q('[data-release]').onchange=e=>{releaseId=e.target.value;verified='';render();};
  q('[data-device]').onchange=e=>{deviceSerial=e.target.value;render();};
  q('[data-prepared]').onchange=render;
  q('[data-refresh]').onclick=refresh;
  q('[data-backup]').onclick=()=>task(async()=>{q('[data-prepared]').checked=false;q('[data-backup-status]').textContent='Backing up all 12 tracks…';try{await backup();}catch(error){q('[data-backup-status]').textContent='Backup not completed. Reconnect normally and try again.';throw error;}q('[data-backup-status]').textContent='Backup downloaded; recovery copy retained in this browser. Keep the downloaded file.';q('[data-prepared]').checked=true;message='Backup complete. Enter bootloader mode when ready.';});
  q('[data-flash]').onclick=async()=>{
    if(q('[data-flash]').disabled)return;
    const selectedRelease=releaseId,serial=deviceSerial;flashPending=true;render();
    try{await disconnect();status.job=await api('flash',{release:selectedRelease,serial});verified='';message='Installing firmware…';}
    catch(error){displayError(error);}
    finally{flashPending=false;render();schedule();}
  };
  q('[data-check]').onclick=()=>task(async()=>{verified='Connecting and checking MIDI…';render();try{verified=await verify(release());}catch(error){verified='Verification not confirmed.';throw error;}});
  q('[data-verify]').onclick=()=>task(async()=>{verified='Connecting and checking MIDI…';render();try{verified=await verify(status.job.release);}catch(error){verified='Verification not confirmed.';throw error;}});
  return {refresh,render};
}
