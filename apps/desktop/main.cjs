const {app, BrowserWindow, Menu, ipcMain, session, screen, shell, dialog} = require('electron');
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
const publicState = () => ({pinned: state.pinned, compact: state.compact});
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
        window.setMinimumSize(Math.min(value ? 520 : 840, area.width), Math.min(400, area.height));
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
        ...bounds, minWidth: Math.min(bounds.width, state.compact ? 520 : 840), minHeight: Math.min(bounds.height, 400),
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
    check(await window.webContents.executeJavaScript("document.body.classList.contains('desktop-performance') && getComputedStyle(document.querySelector('.sidebar')).display === 'none'"), 'Compact layout');
    window.hide();
    check(await window.webContents.executeJavaScript('!document.hidden'), 'Live view remains active in the background');
    window.show();
    await window.webContents.executeJavaScript('window.seqDesktop.setCompact(false)');
    await new Promise(resolve => setTimeout(resolve, 100));
    check(!state.compact && fs.existsSync(statePath), 'Restore workshop and persist state');
    if (process.platform === 'darwin') check(app.commandLine.getSwitchValue('disable-features').split(',').includes('MidiMacUmp'), 'macOS CoreMIDI backend selected');
    const midi = await window.webContents.executeJavaScript("navigator.requestMIDIAccess({sysex:true}).then(a=>({sysex:a.sysexEnabled,inputs:[...a.inputs.values()].map(p=>p.name),outputs:[...a.outputs.values()].map(p=>p.name)}))", true);
    check(midi.sysex, 'MIDI SysEx permission');
    await window.webContents.executeJavaScript('window.seqDesktop.setCompact(true)');
    await window.webContents.executeJavaScript(fs.readFileSync(path.join(__dirname, 'smoke-preview.js'), 'utf8'));
    check(await window.webContents.executeJavaScript("document.querySelector('#live-temp').dataset.temp === 'queued' && document.querySelector('#live-temp').textContent.includes('Shift + Esc')"), 'TEMP queue status and controls in compact view');
    window.setSize(520, 720);
    await new Promise(resolve => setTimeout(resolve, 150));
    check(await window.webContents.executeJavaScript('document.documentElement.scrollWidth <= innerWidth'), 'No horizontal overflow at the minimum width');
    window.setSize(780, 720);
    await new Promise(resolve => setTimeout(resolve, 150));
    fs.writeFileSync(path.join(app.getPath('userData'), 'performance.png'), (await window.webContents.capturePage()).toPNG());
    console.log(JSON.stringify({result: 'PASS', ownedService: service.owned, midi, statePath, screenshot: path.join(app.getPath('userData'), 'performance.png')}));
    app.quit();
}
if (!app.requestSingleInstanceLock()) app.quit();
else {
    app.on('second-instance', () => { if (window) { if (window.isMinimized()) window.restore(); window.show(); window.focus(); } });
    app.on('activate', () => { if (window) window.show(); });
    app.on('window-all-closed', () => { if (process.platform !== 'darwin') app.quit(); });
    app.on('before-quit', () => { quitting = true; if (statePath) save(); });
    app.on('will-quit', () => { clearTimeout(saveTimer); service?.stop(); });
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
        await createWindow();
        if (smoke) await smokeTest();
    }).catch(error => {
        console.error(error);
        if (!smoke) dialog.showErrorBox('SEQ could not start', error.message);
        service?.stop(); app.exit(1);
    });
}
