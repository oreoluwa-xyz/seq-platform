const http = require('node:http');
const {spawn} = require('node:child_process');
const fs = require('node:fs');
const path = require('node:path');
const TARGET = 'keychron/v1_max/ansi_encoder:seq_test';
function status(origin) {
    return new Promise((resolve, reject) => {
        const request = http.get(`${origin}/api/firmware/status`, {timeout: 12000}, response => {
            let body = '';
            response.setEncoding('utf8');
            response.on('data', chunk => { body += chunk; if (body.length > 2 * 1024 * 1024) request.destroy(new Error('Unexpected service response.')); });
            response.on('end', () => {
                try {
                    const data = JSON.parse(body);
                    if (response.statusCode !== 200 || data.apiVersion !== 1 || data.target !== TARGET || !Array.isArray(data.releases)) throw Error('This port is occupied by a different service.');
                    resolve(data);
                } catch (error) { reject(error); }
            });
            response.on('error', reject);
        });
        request.on('timeout', () => request.destroy(new Error('Local firmware service did not respond.')));
        request.on('error', reject);
    });
}
async function ensureService({root, logPath, port = 8765, python = process.env.SEQ_PYTHON || 'python3', runtime, store}) {
    if (!Number.isInteger(port) || port < 1024 || port > 65535) throw Error('SEQ_DESKTOP_PORT must be between 1024 and 65535.');
    const origin = `http://127.0.0.1:${port}`;
    try {
        await status(origin);
        if (runtime) throw Error('SEQ’s desktop service port is still in use. Wait for any firmware installation to finish, then reopen SEQ.');
        return {origin, owned: false, stop() {}};
    }
    catch (error) { if (error.code !== 'ECONNREFUSED') throw error; }
    const log = fs.openSync(logPath, 'a');
    // The helper can finish an in-flight flash after the window closes. SIGINT
    // uses seq_server's existing graceful shutdown and worker.join(), never SIGKILL.
    const executable = runtime ? path.join(runtime, 'seq-service', 'seq-service') : python;
    const args = runtime
        ? ['serve', '--port', String(port), '--root', root, '--store', store, '--seed-store', path.join(root, 'firmware'), '--dfu', path.join(runtime, 'dfu', 'dfu-util')]
        : [path.join(root, 'tools', 'seq_server.py'), 'serve', '--port', String(port)];
    const child = spawn(executable, args, {
        cwd: root, detached: true, stdio: ['ignore', log, log],
        env: {...process.env, PYTHONDONTWRITEBYTECODE: '1', PYTHONUNBUFFERED: '1', PATH: `${process.env.PATH || ''}:/opt/homebrew/bin:/usr/local/bin:/usr/bin:/bin`}
    });
    fs.closeSync(log);
    let failure;
    child.on('error', error => { failure = error; });
    child.on('exit', code => { failure ||= Error(`Local service exited (${code}). See ${logPath}.`); });
    child.unref();
    const stop = () => { if (child.exitCode === null && !failure) child.kill('SIGINT'); };
    for (let attempt = 0; attempt < 40; attempt++) {
        await new Promise(resolve => setTimeout(resolve, 250));
        if (failure) throw Error(`Cannot start the local service: ${failure.message}\n${runtime ? 'Reinstall SEQ from the disk image.' : 'Install Python 3 or set SEQ_PYTHON to its executable.'}`);
        try { await status(origin); return {origin, owned: true, stop}; }
        catch (error) { if (error.code !== 'ECONNREFUSED') { stop(); throw error; } }
    }
    stop(); throw Error(`Local service did not start. See ${logPath}.`);
}
module.exports = {ensureService, status};
