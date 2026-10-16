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
