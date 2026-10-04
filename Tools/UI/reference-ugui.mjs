import fs from 'node:fs';
import path from 'node:path';
import cp from 'node:child_process';
import crypto from 'node:crypto';
import {fileURLToPath} from 'node:url';
import {inventory, validateMatrix} from './runtime-matrix.mjs';
import {requireCompletionEvidence} from '../../Tests/UI/completion-evidence.mjs';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../..');
const reference = JSON.parse(fs.readFileSync(path.join(root, 'Tools/UI/ugui-reference.json'), 'utf8'));
const target = path.join(root, reference.directory);
const checkOnly = process.argv.includes('--check');
function git(args) {
    const result = cp.spawnSync('git', args, {cwd: root, encoding: 'utf8', windowsHide: true, timeout: 120000});
    if (result.error || result.status !== 0) throw new Error(result.error?.message || result.stderr);
    return result.stdout.trim();
}
if (!fs.existsSync(target)) {
    if (checkOnly) throw new Error(`Reference checkout missing: ${target}`);
    fs.mkdirSync(path.dirname(target), {recursive: true});
    git(['clone', '--depth', '1', '--branch', reference.branch, reference.repository, target]);
}
const prefix = ['-c', `safe.directory=${target.replaceAll('\\', '/')}`, '-C', target];
if (git([...prefix, 'remote', 'get-url', 'origin']) !== reference.repository)
    throw new Error('Reference origin does not match the pinned official repository.');
const commit = git([...prefix, 'rev-parse', 'HEAD']);
if (commit !== reference.commit)
    throw new Error(`Reference revision mismatch: expected ${reference.commit}, found ${commit}. No files changed.`);
if (git([...prefix, 'status', '--porcelain']))
    throw new Error('Reference checkout has local changes. Preserve them before updating the reference.');
const files = git([...prefix, 'ls-tree', '-r', '--name-only', 'HEAD']).split('\n');
const matrixPath = path.join(root, 'Tests/UI/ugui-runtime-matrix.json');
const summary = validateMatrix(JSON.parse(fs.readFileSync(matrixPath, 'utf8')), inventory(target, files), reference, root, {requireComplete: process.argv.includes('--complete')});
if(process.argv.includes('--complete'))requireCompletionEvidence(root);
const index = files.filter(p => p.startsWith('UnityEngine.UI/') && p.endsWith('.cs')).map(p => ({
    path: p, sha256: crypto.createHash('sha256').update(fs.readFileSync(path.join(target, p))).digest('hex')
}));
fs.writeFileSync(path.join(root, 'Build/Reference/ugui-files.json'), JSON.stringify({
    ...reference, files: index
}, null, 2) + '\n');
console.log(`Verified uGUI ${reference.branch} at ${commit}; ${summary.files} runtime files, ${summary.types} types (${summary.publicTypes} public), 100% classified.`);
console.log(JSON.stringify(summary));
