// Run only after compiling Release --game-benchmark / --editor-benchmark.
// Keep this process exclusive of other engine tests / Tracy clients.
import fs from 'node:fs';
import path from 'node:path';
import cp from 'node:child_process';
import {fileURLToPath} from 'node:url';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../..');
const product = process.argv[2];
if (!['game', 'editor'].includes(product)) throw Error('Usage: node capture-product-benchmark.mjs <game|editor>');
const output = path.join(root, 'Build/JobsValidation');
const prefix = 'tracy-' + product + '-benchmark-release';
const dir = path.join(output, 'Skinning', 'Release-' + product + '-benchmark' + (product === 'editor' ? '-command-list-editor-ui' : ''), 'current');
const captureExe = process.argv[3] || path.join(root, 'Build/Tools/Tracy-0.11.1/tracy-capture.exe');
const captureSeconds = product === 'game' ? 30 : 180;
const result = {startedAt: new Date().toISOString(), product, dir, captureExe, captureSeconds};
const save = () => fs.writeFileSync(path.join(output, prefix + '-result.json'), JSON.stringify(result, null, 2));
const log = fs.openSync(path.join(output, prefix + '-harness.log'), 'w');
const harness = cp.spawn(path.join(dir, 'skinning-validation.exe'), [dir, '--capture'], {
  cwd: path.join(root, 'Build/Release'), windowsHide: true, stdio: ['ignore', 'pipe', 'pipe']});
result.harnessPid = harness.pid;
let capture, pending = '';
const timer = setTimeout(() => {result.timeout = true; harness.kill(); capture?.kill(); save();}, 240000);
timer.unref();
harness.stdout.on('data', data => {
  fs.writeSync(log, data);
  pending = (pending + data.toString()).slice(-16384);
  if (pending.includes('TRACE READY') && !capture) {
    const captureLog = fs.openSync(path.join(output, prefix + '-capture.log'), 'w');
    capture = cp.spawn(captureExe, ['-a', '127.0.0.1', '-o', path.join(output, prefix + '.tracy'), '-f', '-s', String(captureSeconds)], {
      cwd: output, windowsHide: true, stdio: ['ignore', captureLog, captureLog]});
    result.capturePid = capture.pid;
    save();
    fs.closeSync(captureLog);
    capture.on('error', error => {result.captureError = String(error); harness.kill(); save();});
    capture.on('exit', code => {result.captureExit = code; save(); console.log('capture exit', code); if (code !== 0) harness.kill();});
  }
});
harness.stderr.on('data', data => fs.writeSync(log, data));
harness.on('error', error => {result.harnessError = String(error); save();});
harness.on('exit', code => {fs.closeSync(log); result.harnessExit = code; save(); console.log('harness exit', code);});
process.on('exit', () => {
  save();
  if (result.harnessExit !== 0 || result.captureExit !== 0) process.exitCode = 1;
});
save();
console.log('harness started', harness.pid);
