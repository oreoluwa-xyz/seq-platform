import assert from 'node:assert/strict';
import {verifyFirmware,decodeFirmwareIdentity} from './firmware.mjs?v=cdb10208e0';
function device({flags=63,version=3,short=false,fail=false,identity=null}={}){
  const calls=[];return {calls,async capabilities(){if(fail)throw Error('Offline');},async request(cmd,obj=0,offset=0){
    calls.push([cmd,obj,offset]);if(cmd===24){if(!identity)throw Error('Unsupported command');return identity.slice(offset,offset+64);}const bytes=new Uint8Array(short&&offset===128?12:64);
    if(cmd===17){bytes[62]=offset===576?flags:7;}
    if(cmd===22){bytes[0]=version;bytes[62]=9;}
    return bytes;
  }};
}
const good=device();assert.match(await verifyFirmware(good,{flags:63,snapshot:3}),/expected chord features verified/);
assert.deepEqual(good.calls.filter(c=>c[0]===22).map(c=>c[2]),[0,64,128,192,256,320,384]);
for(const options of [{flags:15},{version:2},{short:true},{fail:true}])await assert.rejects(()=>verifyFirmware(device(options),{flags:63,snapshot:3}));
assert.match(await verifyFirmware(device({flags:31,version:2}),{flags:31,snapshot:2}),/verified/);
console.log('Firmware verification: capabilities, full snapshot, old versions, short replies and offline failure passed.');

const target='keychron/v1_max/ansi_encoder:seq_test',version='2026.09.17.1',buildId='a'.repeat(32);
const identity=new Uint8Array(113),put=(at,text)=>identity.set(new TextEncoder().encode(text),at);
put(0,'SEQFWID1');put(8,version);put(32,target);put(80,buildId);
const release={flags:63,snapshot:3,target,identity:{target,version,buildId}};
assert.deepEqual(decodeFirmwareIdentity(identity),release.identity);
assert.match(await verifyFirmware(device({identity}),release),/Verified installed firmware/);
await assert.rejects(()=>verifyFirmware(device(),release),/Cannot verify the installed build/);
for(const change of [{buildId:'b'.repeat(32)},{version:'2026.09.18.1'},{target:'other-board'}])
  await assert.rejects(()=>verifyFirmware(device({identity}),{...release,identity:{...release.identity,...change}}),/mismatch|differs/);
for(const broken of [identity.slice(0,112),new Uint8Array(113),new Uint8Array([...identity.slice(0,80),...new TextEncoder().encode('0'.repeat(32)),0])])
  assert.throws(()=>decodeFirmwareIdentity(broken));
assert.match(await verifyFirmware(device(),{flags:63,snapshot:3}),/legacy release/);
console.log('Firmware identity: exact match, wrong build/version/board, malformed replies and legacy fallback passed.');

assert.match(await verifyFirmware(device({flags:127,version:4,identity}),{...release,flags:127,snapshot:4}),/Verified installed firmware/);
