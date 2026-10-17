const {test} = require('node:test');
const assert = require('node:assert/strict');
const http = require('node:http');
const net = require('node:net');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const {fitBounds, normalizeState, isAppURL} = require('./window-state.cjs');
const {ensureService, status} = require('./service.cjs');
const displays = [{workArea: {x: 0, y: 25, width: 1440, height: 875}}];
test('Restore onto a connected display and clamp corrupt/oversize geometry', () => {
    const offscreen = fitBounds({x: 3000, y: -500, width: 900, height: 700}, displays);
    assert.deepEqual(offscreen, {x: 540, y: 25, width: 900, height: 700});
    assert.deepEqual(fitBounds({x: 0, y: 0, width: 10000, height: 10000}, displays), {x: 0, y: 25, width: 1440, height: 875});
    assert.equal(fitBounds({width: 'bad'}, displays, true).width, 1440);
    const external = [...displays, {workArea: {x: -1920, y: 0, width: 1920, height: 1080}}];
    assert.equal(fitBounds({x: -1500, y: 50, width: 800, height: 600}, external, true).x, -1500);
    assert.equal(normalizeState({pinned: 'yes'}).pinned, true);
});
test('Only the local app document may invoke desktop controls', () => {
    const origin = 'http://127.0.0.1:8765';
    assert(isAppURL(`${origin}/ui/`, origin));
    for (const url of [`${origin}.evil/ui/`, `${origin}/ui/fake.html`, 'file:///ui/', 'https://example.com/ui/', 'http://user@127.0.0.1:8765/ui/']) assert(!isAppURL(url, origin));
});
const listen = server => new Promise(resolve => server.listen(0, '127.0.0.1', () => resolve(server.address().port)));
const close = server => new Promise(resolve => server.close(resolve));
test('Reuse an existing service without taking ownership; reject unrelated servers', async () => {
    let valid = true;
    const server = http.createServer((_request, response) => { response.setHeader('content-type', 'application/json'); response.end(JSON.stringify(valid ? {apiVersion: 1, target: 'keychron/v1_max/ansi_encoder:seq_test', releases: []} : {app: 'unrelated'})); });
    const port = await listen(server);
    try {
        const helper = await ensureService({port, root: __dirname, logPath: '/tmp/unused-seq-service.log'});
        assert.equal(helper.owned, false); helper.stop(); assert(server.listening);
        await assert.rejects(ensureService({port, root: __dirname, runtime: '/unused', store: '/unused', logPath: '/tmp/unused-seq-service.log'}), /still in use/);
        valid = false;
        await assert.rejects(ensureService({port, root: __dirname, logPath: '/tmp/unused-seq-service.log'}), /different service/);
    } finally { await close(server); }
});
test('Start and gracefully stop the actual helper on an unused port', async () => {
    const probe = net.createServer(), port = await listen(probe); await close(probe);
    const folder = fs.mkdtempSync(path.join(os.tmpdir(), 'seq-desktop-service-test-'));
    let helper;
    try { helper = await ensureService({port, root: path.resolve(__dirname, '..'), logPath: path.join(folder, 'service.log')}); }
    catch (error) {
        const log = path.join(folder, 'service.log');
        error.message += `\nHelper log (${log}):\n${fs.existsSync(log) ? fs.readFileSync(log, 'utf8') : 'No log created: initial port probe failed before helper launch.'}`;
        throw error;
    }
    try { assert.equal(helper.owned, true); assert.equal((await status(helper.origin)).target, 'keychron/v1_max/ansi_encoder:seq_test'); }
    finally { helper.stop(); }
    for (let i = 0; i < 30; i++) {
        try { await status(helper.origin); } catch (error) { if (error.code === 'ECONNREFUSED') return; if (error.code !== 'ECONNRESET') throw error; }
        await new Promise(resolve => setTimeout(resolve, 100));
    }
    assert.fail('Owned service did not exit after graceful shutdown');
});

test('Performance strip defaults to bottom center and preserves saved bounds', () => {
    assert.equal(normalizeState().compact, true);
    assert.deepEqual(fitBounds(null, displays, true), {x:0,y:630,width:1440,height:270});
    const saved={x:40,y:50,width:1000,height:380};
    assert.deepEqual(fitBounds(saved, displays, true),saved);
    const small=[{workArea:{x:-640,y:38,width:640,height:270}}];
    assert.deepEqual(fitBounds(null,small,true),{x:-640,y:38,width:640,height:270});
});

 test('Restore a wide low DAW footprint once and preserve later manual placement', () => {
    const old={performance:{x:30,y:40,width:1080,height:270}};
    assert.equal(normalizeState(old).performance,undefined);
    assert.equal(old.performance.height,270);
    assert.equal(normalizeState({...old,layoutVersion:3}).performance.height,270);
});

test('Low performance strip preserves placement across the Dock reserved area',()=>{
    const screen=[{workArea:{x:0,y:25,width:1512,height:902},bounds:{x:0,y:0,width:1512,height:982}}];
    assert.deepEqual(fitBounds(null,screen,true),{x:36,y:712,width:1440,height:270});
    const saved={x:35,y:712,width:1440,height:270};
    assert.deepEqual(fitBounds(saved,screen,true),saved);
    assert.equal(fitBounds({...saved,y:900},screen,true).y,712);
    assert.equal(fitBounds(saved,screen,false).y,527);
});


test('Compact placement survives Dock changes, negative monitors and topology recovery', () => {
    const primary = {bounds:{x:0,y:0,width:1512,height:982},workArea:{x:0,y:25,width:1512,height:902}};
    const saved = {x:35,y:712,width:1440,height:270};
    for (const workArea of [primary.workArea, {x:80,y:25,width:1432,height:957}, {x:0,y:25,width:1432,height:957}, {x:0,y:25,width:1512,height:957}]) {
        assert.deepEqual(fitBounds(saved,[{...primary,workArea}],true),saved);
    }
    const left = {bounds:{x:-1920,y:-300,width:1920,height:1080},workArea:{x:-1920,y:-275,width:1920,height:1000}};
    const edge = {x:-1100,y:510,width:1200,height:270};
    assert.deepEqual(fitBounds(edge,[primary,left],true),{...edge,x:-1200});
    assert.deepEqual(fitBounds(edge,[left,primary],true),{...edge,x:-1200});
    const negative = {...edge,x:-1500,width:1000};
    assert.deepEqual(fitBounds(negative,[primary,left],true),negative);
    const recovered = fitBounds(negative,[primary],true);
    assert.deepEqual(recovered,{...negative,x:0});
    assert.deepEqual(fitBounds(recovered,[primary],true),recovered);
    const small = [{bounds:{x:0,y:0,width:480,height:240},workArea:{x:0,y:25,width:480,height:180}}];
    assert.deepEqual(fitBounds(saved,small,true),{x:0,y:25,width:480,height:215});
    assert.deepEqual(fitBounds({...saved,width:-1},[primary],true),fitBounds(null,[primary],true));
});

test('Startup re-applies saved geometry after native show and does not persist transient placement', async () => {
    const vm = require('node:vm');
    const {EventEmitter} = require('node:events');
    const saved = {x:35,y:712,width:1440,height:270};
    const display = {bounds:{x:0,y:0,width:1512,height:982},workArea:{x:0,y:25,width:1512,height:902}};
    const writes = [];
    class FakeWindow extends EventEmitter {
        constructor(options) {
            super(); this.bounds = {...saved,y:657}; // Native creation clamp.
            this.webContents = new EventEmitter();
            Object.assign(this.webContents,{setWindowOpenHandler(){},send(){}});
        }
        isDestroyed(){return false;} isMinimized(){return this.minimized || false;}
        isMaximized(){return false;} isFullScreen(){return false;}
        getBounds(){return {...this.bounds};} setMinimumSize(){}
        setBounds(bounds){this.bounds={...bounds};this.emit('move');this.emit('resize');}
        setAlwaysOnTop(){} setVisibleOnAllWorkspaces(){}
        async loadURL(){this.emit('move');}
        show(){this.bounds.y=657;this.emit('move');}
    }
    const electron = {app:{isPackaged:false,setName(){}},BrowserWindow:FakeWindow,
        Menu:{buildFromTemplate:()=>({getMenuItemById:()=>({})}),setApplicationMenu(){}},
        screen:{getAllDisplays:()=>[display]}};
    const context = vm.createContext({require: name => name==='electron' ? electron : name==='node:fs' ? {
        writeFileSync:(_path,data)=>writes.push(JSON.parse(data)),renameSync(){}
    } : name.startsWith('./') ? require(name) : require(name),
    process:{platform:'linux',argv:[]},__dirname,console,setTimeout,clearTimeout});
    const source = fs.readFileSync(path.join(__dirname,'main.cjs'),'utf8').split('if (!app.requestSingleInstanceLock())')[0];
    vm.runInContext(source + `\nstate=normalizeState(${JSON.stringify({layoutVersion:3,compact:true,performance:saved})});statePath='/unused';service={origin:'http://localhost'};`,context);
    await vm.runInContext('createWindow()',context);
    assert(writes.length>0);
    assert(writes.every(state=>JSON.stringify(state.performance)===JSON.stringify(saved)));
    vm.runInContext('window.setBounds({x:50,y:700,width:1400,height:270});save()',context);
    assert.deepEqual(writes.at(-1).performance,{x:50,y:700,width:1400,height:270});
    vm.runInContext('window.minimized=true;window.bounds.y=-10000;save()',context);
    assert.equal(writes.at(-1).performance.y,700);
    // Disk roundtrip through the same creation path, including native clamping.
    vm.runInContext(`state=normalizeState(${JSON.stringify(writes.at(-1))})`,context);
    await vm.runInContext('createWindow()',context);
    assert.equal(writes.at(-1).performance.y,700);
    vm.runInContext('setCompact(false);setCompact(true)',context);
    assert.deepEqual(writes.at(-1).performance,{x:50,y:700,width:1400,height:270});
    display.bounds = {x:0,y:0,width:640,height:480};
    display.workArea = {x:0,y:25,width:640,height:400};
    vm.runInContext('recoverPlacement()',context);
    assert.deepEqual(writes.at(-1).performance,{x:0,y:210,width:640,height:270});
    vm.runInContext('clearTimeout(saveTimer)',context);
});
