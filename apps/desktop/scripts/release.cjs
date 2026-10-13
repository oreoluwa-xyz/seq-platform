const fs = require('node:fs');
const path = require('node:path');
const {execFileSync} = require('node:child_process');
const {build, Platform, Arch} = require('electron-builder');
const desktop = path.resolve(__dirname, '..');
const local = process.argv.includes('--local');
const identity = process.env.SEQ_SIGN_IDENTITY;
const profile = process.env.SEQ_NOTARY_PROFILE;
const run = (file, args) => execFileSync(file, args, {cwd: desktop, stdio: 'inherit'});
const notarize = file => {
    const result = JSON.parse(execFileSync('xcrun', ['notarytool', 'submit', file, '--keychain-profile', profile, '--wait', '--output-format', 'json'], {encoding: 'utf8'}));
    if (result.status !== 'Accepted') throw Error(`Apple did not accept ${path.basename(file)}: ${result.status}. Submission ${result.id}; inspect with notarytool log.`);
    console.log(`Apple accepted ${path.basename(file)} (${result.id}).`);
};
async function main() {
    if (process.platform !== 'darwin') throw Error('Build this release on macOS.');
    if (!local) {
        if (!identity?.startsWith('Developer ID Application:') || !profile) throw Error('Set SEQ_SIGN_IDENTITY to your Developer ID Application identity and SEQ_NOTARY_PROFILE to a notarytool Keychain profile. Use dist:local only for an explicitly unnotarized preview.');
        const identities = execFileSync('security', ['find-identity', '-v', '-p', 'codesigning'], {encoding: 'utf8'});
        if (!identities.includes(`"${identity}"`)) throw Error('The specified signing identity is not valid in Keychain.');
        run('xcrun', ['notarytool', 'history', '--keychain-profile', profile]);
    }
    run(process.env.SEQ_BUILD_PYTHON || path.join(desktop, 'build', 'venv', 'bin', 'python'), [path.join(__dirname, 'prepare.py')]);
    run('swift', ['-module-cache-path', path.join(desktop, 'build', 'swift-cache'), path.join(__dirname, 'icon.swift')]);
    const stage = path.join(desktop, 'build', 'stage');
    const info = JSON.parse(fs.readFileSync(path.join(stage, 'build-info.json'), 'utf8'));
    if (info.arch !== process.arch) throw Error(`Packaging Python architecture ${info.arch} does not match Node/Electron ${process.arch}. Use a native Python interpreter.`);
    const nativeBinaries = [];
    function scan(folder) {
        for (const entry of fs.readdirSync(folder, {withFileTypes: true})) {
            const file = path.join(folder, entry.name);
            if (entry.isDirectory()) scan(file);
            else if (entry.isFile()) {
                const fd = fs.openSync(file, 'r'), header = Buffer.alloc(4);
                fs.readSync(fd, header, 0, 4, 0); fs.closeSync(fd);
                if (['cffaedfe', 'cefaedfe', 'cafebabe', 'bebafeca'].includes(header.toString('hex'))) nativeBinaries.push(path.join('Contents', 'Resources', path.relative(stage, file)));
            }
        }
    }
    scan(path.join(stage, 'runtime'));
    const artifacts = await build({
        projectDir: desktop, targets: Platform.MAC.createTarget('dmg', Arch[info.arch]),
        config: {
            appId: 'com.midiheaven.seq', productName: 'SEQ',
            directories: {output: 'dist', buildResources: 'assets'},
            electronDist: 'node_modules/electron/dist',
            files: ['main.cjs', 'preload.cjs', 'service.cjs', 'window-state.cjs', 'smoke-preview.js', 'package.json'],
            extraResources: [{from: 'build/stage', to: '.', filter: ['**/*']}],
            artifactName: `SEQ-\${version}-\${arch}${local ? '-local-preview' : ''}.\${ext}`,
            asar: true, forceCodeSigning: !local,
            mac: {category: 'public.app-category.music', target: 'dmg', minimumSystemVersion: '13.0',
                icon: 'build/SEQ.icns', identity: local ? '-' : identity.replace('Developer ID Application: ', ''), type: 'distribution',
                hardenedRuntime: !local, notarize: false,
                entitlements: 'assets/entitlements.plist', entitlementsInherit: 'assets/entitlements.plist',
                binaries: nativeBinaries},
            dmg: {sign: false, title: local ? 'SEQ · Local preview' : 'SEQ', backgroundColor: '#f4f3ed',
                window: {width: 540, height: 360}, iconSize: 100,
                contents: [{x: 140, y: 160, type: 'file'}, {x: 400, y: 160, type: 'link', path: '/Applications'}]},
            afterSign: async context => {
                const appPath = path.join(context.appOutDir, 'SEQ.app');
                run('codesign', ['--verify', '--deep', '--strict', appPath]);
                if (local) return;
                const zip = path.join(desktop, 'build', 'SEQ-notarize.zip');
                fs.rmSync(zip, {force: true});
                run('ditto', ['-c', '-k', '--keepParent', appPath, zip]);
                notarize(zip);
                run('xcrun', ['stapler', 'staple', appPath]);
                run('spctl', ['--assess', '--type', 'execute', '--verbose=2', appPath]);
            }
        }
    });
    for (const file of artifacts.filter(file => file.endsWith('.dmg'))) {
        if (!local) {
            run('codesign', ['--sign', identity, '--timestamp', file]);
            notarize(file);
            run('xcrun', ['stapler', 'staple', file]);
            run('xcrun', ['stapler', 'validate', file]);
            run('codesign', ['--verify', '--verbose=2', file]);
            run('spctl', ['--assess', '--type', 'open', '--context', 'context:primary-signature', '--verbose=2', file]);
        }
        const sha = require('node:crypto').createHash('sha256').update(fs.readFileSync(file)).digest('hex');
        fs.writeFileSync(`${file}.sha256`, `${sha}  ${path.basename(file)}\n`);
        fs.writeFileSync(`${file}.json`, JSON.stringify({...info, notarized: !local, signing: local ? 'ad-hoc preview only' : identity, sha256: sha}, null, 2));
        console.log(`${local ? 'LOCAL PREVIEW — not Developer ID signed or notarized' : 'SIGNED AND NOTARIZED'}: ${file}`);
    }
}
main().catch(error => { console.error(error.message); process.exitCode = 1; });
