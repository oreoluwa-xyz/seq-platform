const {app, BrowserWindow, Menu, globalShortcut, ipcMain, session, screen, shell, dialog} = require('electron');
// Chromium's macOS UMP backend drops the keyboard's fragmented SysEx replies.
// Select CoreMIDI's legacy backend before Electron initializes MIDI services.
if (process.platform === 'darwin') {
    const disabled = new Set(app.commandLine.getSwitchValue('disable-features').split(',').filter(Boolean));
    disabled.add('MidiMacUmp');
    app.commandLine.appendSwitch('disable-features', [...disabled].join(','));
}
const fs = require('node:fs');
const path = require('node:path');
const os = require('node:os');
const {ensureService, status} = require('./service.cjs');
const {fitBounds, normalizeState, isAppURL} = require('./window-state.cjs');
const root = app.isPackaged ? path.join(process.resourcesPath, 'site') : path.resolve(__dirname, '..');
const smoke = process.argv.includes('--smoke-test');
app.setName('SEQ');
if (smoke) app.setPath('userData', fs.mkdtempSync(path.join(os.tmpdir(), 'seq-desktop-smoke-')));
let window, service, state, statePath, quitting = false, saveTimer, menu;
let shortcutAvailable = false;
const publicState = () => ({pinned: state.pinned, compact: state.compact, shortcutAvailable});
function toggleVisibility() {
    if (!window || window.isDestroyed()) return;
    if (window.isVisible() && !window.isMinimized()) window.hide();
    else { if (window.isMinimized()) window.restore(); window.show(); window.focus(); }
}
function save() {
    clearTimeout(saveTimer);
    if (window && !window.isDestroyed() && !window.isMaximized() && !window.isFullScreen()) state[state.compact ? 'performance' : 'workshop'] = window.getBounds();
    try { fs.writeFileSync(`${statePath}.tmp`, JSON.stringify(state)); fs.renameSync(`${statePath}.tmp`, statePath); }
    catch (error) { console.error('Could not save window placement:', error.message); }
}
function publish() {
    window.webContents.send('seq:window-changed', publicState());
    if (menu) { menu.getMenuItemById('pin').checked = state.pinned; menu.getMenuItemById('compact').checked = state.compact; }
    save();
}
function setPinned(value) {
    state.pinned = value;
    window.setAlwaysOnTop(value, 'floating');
    if (process.platform === 'darwin') window.setVisibleOnAllWorkspaces(value, {visibleOnFullScreen: value});
    publish(); return publicState();
}
function setCompact(value) {
    if (state.compact !== value) {
        save(); state.compact = value;
        if (window.isMaximized()) window.unmaximize();
        const area = screen.getDisplayMatching(window.getBounds()).workArea;
        window.setMinimumSize(Math.min(value ? 520 : 840, area.width), Math.min(value ? 250 : 400, area.height));
        window.setBounds(fitBounds(state[value ? 'performance' : 'workshop'], screen.getAllDisplays(), value));
    }
    publish(); return publicState();
}
function trusted(event) {
    if (!window || event.sender !== window.webContents || event.senderFrame !== window.webContents.mainFrame || !isAppURL(event.senderFrame.url, service.origin)) throw Error('Untrusted window request.');
}
function openLink(url) {
    try {
        const parsed = new URL(url);
        const localDocument = parsed.origin === service.origin && (parsed.pathname === '/MANUAL.md' || parsed.pathname.startsWith('/modules/'));
        if (!parsed.username && !parsed.password && (parsed.protocol === 'https:' || localDocument)) shell.openExternal(parsed.href).catch(console.error);
    } catch {}
}
function installMenu() {
    menu = Menu.buildFromTemplate([
        ...(process.platform === 'darwin' ? [{label: 'SEQ', submenu: [{role: 'about'}, {type: 'separator'}, {role: 'hide'}, {role: 'hideOthers'}, {role: 'unhide'}, {type: 'separator'}, {role: 'quit'}]}] : []),
        {label: 'Edit', submenu: [{role: 'undo'}, {role: 'redo'}, {type: 'separator'}, {role: 'cut'}, {role: 'copy'}, {role: 'paste'}, {role: 'selectAll'}]},
        {label: 'View', submenu: [
            {label: 'Hide / show SEQ (Ctrl+Backslash)', click: toggleVisibility},
            {id: 'pin', label: 'Pin above other apps', type: 'checkbox', checked: state.pinned, accelerator: 'CommandOrControl+Shift+P', click: item => setPinned(item.checked)},
            {id: 'compact', label: 'Performance view', type: 'checkbox', checked: state.compact, accelerator: 'CommandOrControl+Shift+M', click: item => setCompact(item.checked)},
            {type: 'separator'}, {role: 'resetZoom'}, {role: 'zoomIn'}, {role: 'zoomOut'}, {type: 'separator'}, {role: 'toggleDevTools'}
        ]},
        {role: 'windowMenu'}
    ]);
    Menu.setApplicationMenu(menu);
}
async function createWindow() {
    const bounds = fitBounds(state[state.compact ? 'performance' : 'workshop'], screen.getAllDisplays(), state.compact);
    window = new BrowserWindow({
        ...bounds, minWidth: Math.min(bounds.width, state.compact ? 520 : 840), minHeight: Math.min(bounds.height, state.compact ? 250 : 400),
        title: 'SEQ', show: false, backgroundColor: '#f4f3ed', autoHideMenuBar: true,
        webPreferences: {preload: path.join(__dirname, 'preload.cjs'), sandbox: true, contextIsolation: true, nodeIntegration: false, webSecurity: true, backgroundThrottling: false}
    });
    window.on('page-title-updated', event => { event.preventDefault(); window.setTitle('SEQ · Instrument workshop'); });
    window.webContents.setWindowOpenHandler(({url}) => { openLink(url); return {action: 'deny'}; });
    window.webContents.on('will-navigate', (event, url) => { if (!isAppURL(url, service.origin)) { event.preventDefault(); openLink(url); } });
    window.webContents.on('will-redirect', (event, url) => { if (!isAppURL(url, service.origin)) event.preventDefault(); });
    window.webContents.on('will-attach-webview', event => event.preventDefault());
    for (const event of ['move', 'resize']) window.on(event, () => { clearTimeout(saveTimer); saveTimer = setTimeout(save, 300); });
    window.on('close', event => {
        save();
        // Keep one live MIDI connection when closing the macOS window. Cmd+Q quits.
        if (process.platform === 'darwin' && !quitting) { event.preventDefault(); window.hide(); }
    });
    window.on('closed', () => { window = null; });
    installMenu();
    await window.loadURL(`${service.origin}/ui/`);
    setPinned(state.pinned);
    window.show();
}
async function smokeTest() {
    const check = (condition, message) => { if (!condition) throw Error(message); };
    const capture = async target => {
        await target.webContents.executeJavaScript("Promise.all(document.getAnimations().filter(a=>Number.isFinite(a.effect.getComputedTiming().iterations)).map(a=>a.finished.catch(()=>{}))).then(()=>new Promise(resolve=>requestAnimationFrame(()=>requestAnimationFrame(resolve))))");
        return target.webContents.capturePage();
    };
    if (app.isPackaged) {
        const firmware = await status(service.origin);
        check(firmware.toolAvailable && !firmware.discoveryError, 'Bundled DFU tool runs');
        check(firmware.releases.length > 0, 'Bundled firmware seeds writable storage');
        check(await window.webContents.executeJavaScript("fetch('/MANUAL.md').then(r => r.ok)"), 'Bundled manual');
    }
    check(await window.webContents.executeJavaScript("!!document.querySelector('#desktop-pin') && typeof navigator.requestMIDIAccess === 'function' && typeof window.require === 'undefined'"), 'UI, MIDI API and renderer isolation');
    await window.webContents.executeJavaScript('window.seqDesktop.setPinned(false)'); check(!window.isAlwaysOnTop(), 'Unpin');
    await window.webContents.executeJavaScript('window.seqDesktop.setPinned(true)'); check(window.isAlwaysOnTop(), 'Pin');
    await window.webContents.executeJavaScript('window.seqDesktop.setCompact(true)');
    await new Promise(resolve => setTimeout(resolve, 200));
    check(await window.webContents.executeJavaScript("document.body.classList.contains('desktop-performance') && getComputedStyle(document.querySelector('.sidebar')).display !== 'none'"), 'Compact layout');
    check(shortcutAvailable && globalShortcut.isRegistered('Control+\\'), 'Global hide shortcut registered');
    toggleVisibility();
    check(!window.isVisible(), 'Shortcut hides the window');
    check(await window.webContents.executeJavaScript('!document.hidden'), 'Live view remains active in the background');
    toggleVisibility();
    check(window.isVisible(), 'Shortcut restores the window');
    await window.webContents.executeJavaScript('window.seqDesktop.setCompact(false)');
    await new Promise(resolve => setTimeout(resolve, 100));
    check(!state.compact && fs.existsSync(statePath), 'Restore workshop and persist state');
    if (process.platform === 'darwin') check(app.commandLine.getSwitchValue('disable-features').split(',').includes('MidiMacUmp'), 'macOS CoreMIDI backend selected');
    const midi = await window.webContents.executeJavaScript("navigator.requestMIDIAccess({sysex:true}).then(a=>({sysex:a.sysexEnabled,inputs:[...a.inputs.values()].map(p=>p.name),outputs:[...a.outputs.values()].map(p=>p.name)}))", true);
    check(midi.sysex, 'MIDI SysEx permission');
    await window.webContents.executeJavaScript('window.seqDesktop.setCompact(true)');
    check(await window.webContents.executeJavaScript("document.querySelector('#desktop-pin').textContent==='Pinned' && document.querySelector('#desktop-compact').textContent==='Workshop ↗'"),'Desktop toolbar initializes before live MIDI state');
    await window.webContents.executeJavaScript(fs.readFileSync(path.join(__dirname, 'smoke-preview.js'), 'utf8'));
    check(await window.webContents.executeJavaScript("document.querySelector('#live-temp').dataset.temp === 'queued' && document.querySelector('#live-temp').textContent.includes('Shift + Esc')"), 'TEMP queue status and controls in compact view');
    check(await window.webContents.executeJavaScript("document.querySelectorAll('#tracks .track').length === 12 && getComputedStyle(document.querySelector('.sidebar')).display !== 'none' && !!document.querySelector('#hardware-orbit svg')"), 'Track rail and circular rhythm remain visible');
    window.setSize(520, 720);
    await new Promise(resolve => setTimeout(resolve, 150));
    check(await window.webContents.executeJavaScript('document.documentElement.scrollWidth <= innerWidth'), 'No horizontal overflow at the minimum width');
    window.setSize(1440, 270);
    await new Promise(resolve => setTimeout(resolve, 150));
    check(await window.webContents.executeJavaScript("document.querySelector('#hardware-orbit').getBoundingClientRect().bottom <= innerHeight"), 'Circular rhythm fits the default strip height');
    check(await window.webContents.executeJavaScript(`(()=>{
        const steps=document.querySelector('#live-steps').getBoundingClientRect();
        const orbit=document.querySelector('#hardware-orbit').getBoundingClientRect();
        const rhythm=document.querySelector('#live-euclid-settings').getBoundingClientRect();
        const tabs=document.querySelector('#performance-tabs').getBoundingClientRect();
        return steps.left>=orbit.right && steps.top>=rhythm.bottom && rhythm.top>=tabs.bottom && steps.width>500 && steps.bottom<=innerHeight && getComputedStyle(document.querySelector('#live-palette')).display==='none';
    })()`), 'Rhythm controls and sequence have separate regions with palette available in its tab');
    fs.writeFileSync(path.join(app.getPath('userData'), 'performance.png'), (await capture(window)).toPNG());
    await window.webContents.executeJavaScript("document.querySelector('[data-pane=palette]').click()");
    check(await window.webContents.executeJavaScript("[...document.querySelectorAll('#live-palette .palette-slot')].every(n=>n.getBoundingClientRect().bottom<=innerHeight)"),'All saved palette notes fit the default performance window');
    fs.writeFileSync(path.join(app.getPath('userData'),'performance-palette-default.png'),(await capture(window)).toPNG());
    check(await window.webContents.executeJavaScript(`(async()=>{
        const {renderPalette}=await import('/ui/palette.mjs'),{renderLiveSteps}=await import('/ui/steps.mjs');
        const root=document.querySelector('#live-palette'),steps=document.querySelector('#live-steps'),state=window.performanceFixtureState;
        const render=()=>{renderPalette(root,state,true,()=>{},()=>{},()=>{},()=>{});renderLiveSteps(steps,state,true);};
        const button=root.querySelector('[data-palette-select="0"]');button.focus();button.click();
        if(steps.querySelectorAll('.palette-match').length!==2||root.querySelector('[data-palette-select="0"]').getAttribute('aria-pressed')!=='true')return false;
        const mounted=root.querySelector('[data-palette-select="0"]');
        state.step=5;render();
        if(root.querySelector('[data-palette-select="0"]')!==mounted)return false;
        if(!steps.children[5].matches('.palette-match.playhead')||!document.activeElement.matches('[data-palette-select="0"]'))return false;
        if(steps.children[2].hasAttribute('data-palette-slot'))return false;
        state.palette.pin=3;render();
        if(steps.querySelectorAll('.palette-match').length!==1||!steps.children[8].matches('.palette-match'))return false;
        state.palette.pin=null;state.track=1;render();
        if(steps.querySelector('.palette-match'))return false;
        state.track=0;render();root.querySelector('[data-palette-select="0"]').click();
        return document.querySelector('#palette-selection-status').textContent.includes('2 hits');
    })()`),'Palette inspection links the correct hits, preserves focus/playhead on polls and clears on track changes');
    fs.writeFileSync(path.join(app.getPath('userData'),'performance-palette-linked.png'),(await capture(window)).toPNG());

    check(await window.webContents.executeJavaScript(`(()=>{
        const palette=document.querySelector('#live-palette'),preview=document.querySelector('.hardware-rhythm>div:nth-child(2)'),steps=document.querySelector('#live-steps');
        const before=steps.getBoundingClientRect();palette.scrollTop=palette.scrollHeight;
        const after=steps.getBoundingClientRect(),r=palette.getBoundingClientRect();
        palette.scrollTop=0;
        return getComputedStyle(preview).display!=='none'&&before.width>350&&before.right<=r.left&&after.top===before.top&&after.bottom<=innerHeight&&r.bottom<=innerHeight;
    })()`),'Palette scrolls independently beside the visible Euclidean hits in the short wide strip');

    window.setSize(1440,270);await new Promise(resolve=>setTimeout(resolve,100));
    for(const pane of ['palette','variation','scale']){
        await window.webContents.executeJavaScript(`document.querySelector('[data-pane="${pane}"]').click()`);
        check(await window.webContents.executeJavaScript(`(()=>{
            const panel=document.querySelector('#live-${pane}'),tabs=document.querySelector('#performance-tabs').getBoundingClientRect(),r=panel.getBoundingClientRect();
            return getComputedStyle(panel).display!=='none' && r.top>=tabs.bottom && r.left>=0 && r.right<=innerWidth && document.documentElement.scrollWidth<=innerWidth && getComputedStyle(document.querySelector('#live-euclid-settings')).display==='none';
        })()`), `${pane} editor has its own scrollable region in the short window`);
        fs.writeFileSync(path.join(app.getPath('userData'),`performance-${pane}.png`),(await capture(window)).toPNG());
    }
    await window.webContents.executeJavaScript(`(async()=>{
        document.querySelector('[data-pane="palette"]').click();
        const {renderPalette}=await import('/ui/palette.mjs');
        const state=window.performanceFixtureState;state.palette.mode='chords';state.palette.qualities=[0,1,8,7,6,0,0,0];
        renderPalette(document.querySelector('#live-palette'),state,true,()=>{},()=>{},()=>{},()=>{});
    })()`);
    check(await window.webContents.executeJavaScript("document.querySelectorAll('.palette-quality-row button').length===13 && document.querySelector('#live-palette').getBoundingClientRect().top>=document.querySelector('#performance-tabs').getBoundingClientRect().bottom"), 'Chord palette remains contained');
    window.setSize(1440,270);await new Promise(resolve=>setTimeout(resolve,100));
    fs.writeFileSync(path.join(app.getPath('userData'),'performance-chord-palette.png'),(await capture(window)).toPNG());
    check(await window.webContents.executeJavaScript("getComputedStyle(document.querySelector('.hardware-rhythm>div:nth-child(2)')).display!=='none' && document.querySelector('#live-palette').scrollHeight>document.querySelector('#live-palette').clientHeight"),'Chord quality editing retains the rhythm beside its scrollable palette');
    check(await window.webContents.executeJavaScript(`(()=>{
        const root=document.querySelector('#live-palette'),steps=document.querySelector('#live-steps'),before=steps.getBoundingClientRect().top;
        root.querySelectorAll('.palette-edit-chord')[2].click();
        const title=root.querySelector('.palette-quality-title').getBoundingClientRect();
        return root.scrollTop>0&&title.top>=root.getBoundingClientRect().top-1&&title.bottom<=innerHeight&&steps.getBoundingClientRect().top===before;
    })()`),'Editing a chord reveals its qualities without moving the visible rhythm');
    fs.writeFileSync(path.join(app.getPath('userData'),'performance-chord-quality.png'),(await capture(window)).toPNG());
    check(await window.webContents.executeJavaScript(`(async()=>{
        const {renderPalette}=await import('/ui/palette.mjs');
        const root=document.querySelector('#live-palette'),state=window.performanceFixtureState;
        root.querySelector('[data-palette-select="3"]').click();
        if(!root.querySelector('.palette-quality-title').textContent.startsWith('Palette 4')||!document.querySelector('#live-steps').children[8].matches('.palette-match'))return false;
        [...root.querySelectorAll('button')].find(b=>b.textContent==='Edit all slots').click();
        if(document.querySelectorAll('#live-steps .palette-match').length!==4)return false;
        state.palette.editDigit=5;renderPalette(root,state,true,()=>{},()=>{},()=>{},()=>{});
        if(!root.querySelector('[data-palette-select="4"]').matches('[aria-pressed="true"]'))return false;
        state.palette.editDigit=null;renderPalette(root,state,true,()=>{},()=>{},()=>{},()=>{});
        return root.querySelector('.palette-quality-title').textContent.startsWith('Palette 5');
    })()`),'Chord selection, all-slot editing and hardware chord keys share the hit highlight');
    await window.webContents.executeJavaScript("document.querySelector('#live-palette').scrollTop=0");
    window.setSize(520,720);await new Promise(resolve=>setTimeout(resolve,100));
    check(await window.webContents.executeJavaScript('document.documentElement.scrollWidth<=innerWidth'),'Full chord palette fits narrow window');
    fs.writeFileSync(path.join(app.getPath('userData'),'performance-palette-narrow.png'),(await capture(window)).toPNG());
    await window.webContents.executeJavaScript("document.querySelector('[data-pane=variation]').click();document.querySelector('[data-mutation]').focus();document.querySelector('[data-mutation]').value='35';document.querySelector('[data-mutation]').dispatchEvent(new Event('input'))");
    await window.webContents.executeJavaScript("Promise.all([import('/ui/variation.mjs'),import('/ui/performance.mjs')]).then(([v,p])=>{v.renderVariation(document.querySelector('#live-variation'),window.performanceFixtureState,true,()=>{});p.renderPerformance(window.performanceFixtureState);})");
    check(await window.webContents.executeJavaScript("document.activeElement.matches('[data-mutation]') && document.querySelector('[data-mutation]').value==='35' && document.body.dataset.performancePane==='variation'"),'Polling preserves variation focus and selected pane');
    check(await window.webContents.executeJavaScript(`(async()=>{
        const {renderPerformance}=await import('/ui/performance.mjs');
        const state=window.performanceFixtureState,content=document.querySelector('#live-content');
        content.scrollTop=content.scrollHeight;
        const tabs=document.querySelector('#performance-tabs').getBoundingClientRect(),rail=document.querySelector('#tracks').getBoundingClientRect();
        if(tabs.top<rail.bottom||tabs.bottom>innerHeight)return false;
        document.querySelector('[data-pane=rhythm]').click();
        state.palette.held=true;state.palette.shift=true;renderPerformance(state);
        if(document.body.dataset.performancePane!=='palette')return false;
        document.querySelector('[data-pane=rhythm]').click();renderPerformance(state);
        if(document.body.dataset.performancePane!=='rhythm')return false;
        state.palette.capturing=true;renderPerformance(state);
        if(document.body.dataset.performancePane!=='palette'||content.scrollTop!==0)return false;
        state.palette.capturing=false;state.palette.held=false;state.palette.shift=false;renderPerformance(state);
        document.querySelector('[data-pane=rhythm]').click();state.palette.replace=true;renderPerformance(state);
        if(document.body.dataset.performancePane!=='palette')return false;
        state.scale.editing=true;renderPerformance(state);
        if(document.body.dataset.performancePane!=='scale')return false;
        state.scale.editing=false;renderPerformance(state);
        if(document.body.dataset.performancePane!=='palette')return false;
        const tab=document.querySelector('[data-pane=palette]');tab.focus();tab.dispatchEvent(new KeyboardEvent('keydown',{key:'ArrowRight',bubbles:true}));
        if(!document.activeElement.matches('[data-pane=variation]'))return false;
        document.querySelector('#live-palette').hidden=true;document.querySelector('[data-pane=palette]').click();
        if(document.body.dataset.performancePane!=='rhythm')return false;
        document.querySelector('#live-palette').hidden=false;renderPerformance(state);
        return true;
    })()`),'Editor navigation stays visible, follows keyboard gestures and handles unavailable palettes');
    await window.webContents.executeJavaScript("document.querySelector('[data-pane=rhythm]').click();document.querySelector('#live-content').scrollTop=0");
    window.setSize(1080,270);await new Promise(resolve=>setTimeout(resolve,100));
    check(await window.webContents.executeJavaScript("document.querySelector('#project-health').dataset.tone === 'temp' && document.querySelector('#project-health summary').textContent.includes('TEMP edits')"), 'Capacity status in performance toolbar');
    await window.webContents.executeJavaScript("document.querySelector('#project-health').open=true");
    check(await window.webContents.executeJavaScript("(()=>{const r=document.querySelector('.project-health-body').getBoundingClientRect();return r.left>=0&&r.right<=innerWidth&&r.bottom<=innerHeight})()"), 'Capacity disclosure fits the short strip');
    fs.writeFileSync(path.join(app.getPath('userData'), 'project-capacity.png'), (await capture(window)).toPNG());
    await window.webContents.executeJavaScript("document.querySelector('#project-health').open=false");
    await window.webContents.executeJavaScript(`(async()=>{
        const {renderTemp}=await import('/ui/temp.mjs');
        const {renderLiveSteps}=await import('/ui/steps.mjs');
        const {renderHardwareOrbit}=await import('/ui/orbit.mjs');
        const state={active:true,running:true,track:0,type:1,bar:0,step:4,length:16,loopSteps:5,pattern:31,sounding:[67],temp:{active:false,queued:false},projectedNotes:Array.from({length:16},(_,i)=>i<5?[[60,63,67,70,67][i]]:[])};
        renderTemp(document.querySelector('#live-temp'),state,true);
        renderLiveSteps(document.querySelector('#live-steps'),state,true);
        renderHardwareOrbit(document.querySelector('#hardware-orbit'),state,true);
        document.querySelector('#live-position').textContent='5 / 5';
    })()`);
    check(await window.webContents.executeJavaScript("document.querySelector('#live-temp').dataset.temp === 'loop' && document.querySelector('#live-temp').textContent.includes('Global loop · 5 steps') && document.querySelector('#live-temp').textContent.includes('Release Enter')"), 'Momentary global loop feedback');
    fs.writeFileSync(path.join(app.getPath('userData'), 'global-loop.png'), (await capture(window)).toPNG());
    await window.webContents.executeJavaScript(`(async()=>{
        const {renderCC}=await import('/ui/cc.mjs');
        document.body.dataset.liveMode='cc';
        renderCC(document.querySelector('#cc-dock'),{cc:{active:true,latched:false,manual:true,track:0,selected:0,slots:Array.from({length:16},(_,i)=>({keycode:20+i,controller:70+i,channel:0,value:8*i,held:i===0}))}},true);
    })()`);
    await new Promise(resolve => setTimeout(resolve, 150));
    check(await window.webContents.executeJavaScript("!document.querySelector('#cc-dock').hidden && getComputedStyle(document.querySelector('#cc-dock')).position === 'static' && getComputedStyle(document.querySelector('.hardware-rhythm')).display === 'none' && document.querySelector('#cc-dock').getBoundingClientRect().top >= document.querySelector('#tracks').getBoundingClientRect().bottom"), 'Held CC mode replaces rhythm without covering tracks');
    const allCCFit = `(()=>{
        const cells=[...document.querySelectorAll('.cc-slot')];
        return cells.length===16 && cells.every(cell=>{const r=cell.getBoundingClientRect();return r.top>=document.querySelector('#tracks').getBoundingClientRect().bottom&&r.bottom<=innerHeight&&r.right<=innerWidth;});
    })()`;
    check(await window.webContents.executeJavaScript(allCCFit), 'All 16 CC controls fit the short strip');
    await window.webContents.executeJavaScript(`(async()=>{
        const {renderTrackMute}=await import('/ui/track-status.mjs');
        document.querySelectorAll('#tracks .track').forEach((node,i)=>renderTrackMute(node,{mutes:{muted:2,partial:4}},true,i));
    })()`);
    check(await window.webContents.executeJavaScript("document.querySelectorAll('#tracks [data-mute=muted]').length===1 && document.querySelectorAll('#tracks [data-mute=partial]').length===1"), 'Distinct whole-track and partial mute indicators');
    await new Promise(resolve=>setTimeout(resolve,100));
    fs.writeFileSync(path.join(app.getPath('userData'), 'performance-cc.png'), (await capture(window)).toPNG());
    window.setSize(520, 250);
    await new Promise(resolve=>setTimeout(resolve,150));
    fs.writeFileSync(path.join(app.getPath('userData'), 'performance-cc-minimum.png'), (await capture(window)).toPNG());
    check(await window.webContents.executeJavaScript(allCCFit), 'All CC controls fit at the minimum window size');
    const web = new BrowserWindow({width:1280,height:800,show:false,webPreferences:{sandbox:true,contextIsolation:true,nodeIntegration:false}});
    await web.loadURL(`${service.origin}/ui/`);
    await web.webContents.executeJavaScript(`(async()=>{
        const {renderTrackMute}=await import('/ui/track-status.mjs');
        const {renderCC}=await import('/ui/cc.mjs');
        document.querySelectorAll('#tracks .track').forEach((node,i)=>renderTrackMute(node,{mutes:{muted:2,partial:4}},true,i));
        renderCC(document.querySelector('#cc-dock'),{cc:{active:true,latched:true,track:0,selected:null,slots:Array.from({length:16},(_,i)=>({keycode:20+i,controller:16+i,channel:0,value:64,held:false}))}},true);
    })()`);
    web.show();
    await new Promise(resolve=>setTimeout(resolve,150));
    check(await web.webContents.executeJavaScript(`(()=>{
        const muted=document.querySelector('#tracks [data-mute=muted] .track-mute-full');
        const cells=[...document.querySelectorAll('.cc-slot')];
        return !window.seqDesktop && !document.body.classList.contains('desktop-performance') && muted.textContent==='Muted' && getComputedStyle(muted).display!=='none' && cells.length===16 && cells.every(c=>{const r=c.getBoundingClientRect();return r.top>=0&&r.bottom<=innerHeight;});
    })()`), 'Browser UI shows explicit mute labels and all sixteen CC controls');
    fs.writeFileSync(path.join(app.getPath('userData'),'web-cc-mutes.png'),(await capture(web)).toPNG());
    if (!app.isPackaged) {
        await web.loadURL(`${service.origin}/ui/project-health-fixture.html`);
        check(await web.webContents.executeJavaScript("document.title.startsWith('PASS')"), 'Rendered capacity fixture: '+await web.webContents.executeJavaScript("document.querySelector('#result')?.textContent || document.title"));
    } else {
        await web.loadURL(`${service.origin}/ui/`);
        await web.webContents.executeJavaScript(fs.readFileSync(path.join(__dirname, 'smoke-preview.js'), 'utf8'));
        await web.webContents.executeJavaScript("document.querySelector('#project-health').open=true");
        check(await web.webContents.executeJavaScript("document.querySelector('#project-health').textContent.includes('TEMP edits')"), 'Bundled browser capacity status');
    }
    web.setSize(390, 844);
    await new Promise(resolve=>setTimeout(resolve,150));
    check(await web.webContents.executeJavaScript('document.documentElement.scrollWidth <= innerWidth'), 'Capacity fits phone width');
    fs.writeFileSync(path.join(app.getPath('userData'),'project-capacity-phone.png'),(await capture(web)).toPNG());
    if (!app.isPackaged) {
        web.setSize(1080, 900);
        await web.loadURL(`${service.origin}/ui/palette-chords-fixture.html`);
        check(await web.webContents.executeJavaScript("document.title.startsWith('PASS')"), 'Rendered chord palette: '+await web.webContents.executeJavaScript("document.querySelector('#result').textContent"));
        fs.writeFileSync(path.join(app.getPath('userData'),'chord-palette.png'),(await capture(web)).toPNG());
        web.setSize(390, 844);
        await new Promise(resolve=>setTimeout(resolve,150));
        check(await web.webContents.executeJavaScript('document.documentElement.scrollWidth <= innerWidth'), 'Chord picker fits phone width');
        fs.writeFileSync(path.join(app.getPath('userData'),'chord-palette-phone.png'),(await capture(web)).toPNG());
        web.setSize(1080, 900);
        await web.loadURL(`${service.origin}/ui/variation-fixture.html`);
        check(await web.webContents.executeJavaScript("document.title.startsWith('PASS')"), 'Rendered variation controls: '+await web.webContents.executeJavaScript("document.querySelector('#result').textContent"));
        fs.writeFileSync(path.join(app.getPath('userData'),'variation.png'),(await capture(web)).toPNG());
        web.setSize(390, 844);
        await new Promise(resolve=>setTimeout(resolve,150));
        check(await web.webContents.executeJavaScript('document.documentElement.scrollWidth <= innerWidth'), 'Variation controls fit phone width');
        fs.writeFileSync(path.join(app.getPath('userData'),'variation-phone.png'),(await capture(web)).toPNG());
    }
    web.destroy();
    console.log(JSON.stringify({result: 'PASS'  , ownedService: service.owned, midi, statePath, screenshot: path.join(app.getPath('userData'), 'performance.png')}));
    app.quit();
}
if (!app.requestSingleInstanceLock()) app.quit();
else {
    app.on('second-instance', () => { if (window) { if (window.isMinimized()) window.restore(); window.show(); window.focus(); } });
    app.on('activate', () => { if (window) window.show(); });
    app.on('window-all-closed', () => { if (process.platform !== 'darwin') app.quit(); });
    app.on('before-quit', () => { quitting = true; if (statePath) save(); });
    app.on('will-quit', () => { clearTimeout(saveTimer); globalShortcut.unregisterAll(); service?.stop(); });
    app.whenReady().then(async () => {
        fs.mkdirSync(app.getPath('userData'), {recursive: true});
        statePath = path.join(app.getPath('userData'), 'window-state.json');
        try { state = normalizeState(JSON.parse(fs.readFileSync(statePath, 'utf8'))); } catch { state = normalizeState(); }
        service = await ensureService({root, logPath: path.join(app.getPath('userData'), 'service.log'),
            port: Number(process.env.SEQ_DESKTOP_PORT || (app.isPackaged ? 8766 : 8765)),
            ...(app.isPackaged ? {runtime: path.join(process.resourcesPath, 'runtime'), store: path.join(app.getPath('userData'), 'firmware')} : {})});
        const allowed = (contents, permission, origin) => !!window && contents === window.webContents && isAppURL(contents.getURL(), service.origin) && origin === service.origin && ['midi', 'midiSysex'].includes(permission);
        session.defaultSession.setPermissionCheckHandler((contents, permission, origin) => allowed(contents, permission, origin));
        session.defaultSession.setPermissionRequestHandler((contents, permission, callback, details) => {
            let origin; try { origin = new URL(details.requestingUrl).origin; } catch {}
            callback(allowed(contents, permission, origin));
        });
        session.defaultSession.webRequest.onHeadersReceived({urls: [`${service.origin}/*`]}, (details, callback) => {
            callback({responseHeaders: {...details.responseHeaders, 'Content-Security-Policy': ["default-src 'self'; script-src 'self'; style-src 'self' 'unsafe-inline'; img-src 'self' data: blob:; font-src 'self'; connect-src 'self'; media-src 'self' blob:; object-src 'none'; frame-src 'none'; base-uri 'self'; frame-ancestors 'none'"]}});
        });
        for (const [channel, action] of [['seq:window-state', () => publicState()], ['seq:pin', setPinned], ['seq:compact', setCompact]]) {
            ipcMain.handle(channel, (event, value) => { trusted(event); if (channel !== 'seq:window-state' && typeof value !== 'boolean') throw Error('Expected a boolean.'); return action(value); });
        }
        ipcMain.handle('seq:hide', event => { trusted(event); window.hide(); });
        shortcutAvailable = globalShortcut.register('Control+\\', toggleVisibility);
        if (!shortcutAvailable) console.error('Ctrl+Backslash is unavailable: another app may own it.');
        await createWindow();
        if (smoke) await smokeTest();
    }).catch(error => {
        console.error(error);
        if (!smoke) dialog.showErrorBox('SEQ could not start', error.message);
        service?.stop(); app.exit(1);
    });
}
