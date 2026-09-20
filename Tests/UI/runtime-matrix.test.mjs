import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {declarations, inventory, validateMatrix} from '../../Tools/UI/runtime-matrix.mjs';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../..');
const reference = JSON.parse(fs.readFileSync(path.join(root, 'Tools/UI/ugui-reference.json'), 'utf8'));
const matrix = JSON.parse(fs.readFileSync(path.join(root, 'Tests/UI/ugui-runtime-matrix.json'), 'utf8'));
function walk(directory, prefix = '') {
    return fs.readdirSync(directory, {withFileTypes: true}).flatMap(entry => {
        const name = prefix + entry.name;
        return entry.isDirectory() ? walk(path.join(directory, entry.name), name + '/') : [name];
    });
}
const source = path.join(root, reference.directory);
const files = ['UI', 'EventSystem'].flatMap(dir => walk(path.join(source, 'UnityEngine.UI', dir), 'UnityEngine.UI/' + dir + '/'));
const expected = inventory(source, files);
const check = candidate => validateMatrix(candidate, expected, reference, root);
const summary = check(matrix);
assert.equal(summary.files, 84);
assert.equal(summary.types, 178);
assert.equal(summary.publicTypes, 148);
let checks = 0;
function rejects(name, mutate, message) {
    const copy = structuredClone(matrix); mutate(copy);
    assert.throws(() => check(copy), message, name); ++checks;
}
rejects('missing file', m => m.files.pop(), /file count/);
rejects('duplicate file', m => m.files.push(m.files[0]), /duplicate file/);
rejects('unknown file', m => m.files[0].path = 'unknown.cs', /missing file/);
rejects('missing type', m => m.files[0].types.pop(), /type count/);
rejects('duplicate type', m => m.files[0].types.push(m.files[0].types[0]), /duplicate type/);
rejects('invented type', m => m.files[0].types[0].name = 'Unknown.Type', /missing type/);
rejects('unknown status', m => m.files[0].types[0].status = 'done', /unknown status/);
rejects('missing test ID', m => Object.assign(m.files[0].types[0], {status: 'implemented', tests: []}), /without tests/);
rejects('unregistered test', m => Object.assign(m.files[0].types[0], {status: 'adapted', tests: ['FAKE']}), /not accepted/);
rejects('false public declaration', m => m.files[0].types[0].public = false, /declaration mismatch/);
rejects('source changed', m => m.files[0].sha256 = 'bad', /hash mismatch/);
rejects('reference changed', m => m.reference.commit = 'bad', /reference mismatch/);
rejects('incomplete blocker', m => m.files[0].types[0].status = 'blocked', /incomplete blocker/);
rejects('unknown phase', m => m.files[0].types[0].stage = 'U99', /unknown stage/);
rejects('dependency completion without tests', m => Object.assign(m.dependencies[0], {status: 'implemented', tests: []}), /dependency without tests/);
rejects('duplicate dependency', m => m.dependencies.push(m.dependencies[0]), /duplicate dependency/);
const unfinished = structuredClone(matrix); unfinished.files[0].types[0].status = 'partial';
assert.throws(() => validateMatrix(unfinished, expected, reference, root, {requireComplete: true}), /incomplete types\/dependencies/);
const parsed = declarations(`namespace Example {
    // public class Fake {}
    internal class Hidden { public class Nested {} }
    public class Visible<T> where T : class {
        string ignored = "public class FakeAgain {}";
        public delegate void Handler<U>(U value);
        public enum Mode { One, Two }
        protected class Extension {}
    }
}`);
assert.deepEqual(parsed.filter(t => t.public).map(t => t.name), ['Example.Visible<T>', 'Example.Visible<T>.Handler<U>', 'Example.Visible<T>.Mode']);
assert.equal(parsed.length, 6);
console.log(`PASS U0-MATRIX: ${summary.files} files / ${summary.types} types / ${summary.publicTypes} public; ${checks} negative cases and scanner fixture.`);
