import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';

export const statuses = ['pending', 'partial', 'implemented', 'adapted', 'blocked'];
export const runtimePath = p => /^UnityEngine\.UI\/(UI|EventSystem)\/.*\.cs$/.test(p);

// Inventory declarations, including nested and internal helpers. Strip comments
// and literals first so documentation examples cannot invent public API entries.
// This scanner targets the pinned C# source, not arbitrary future C# grammars.
export function declarations(source) {
    const clean = source.replace(/\/\*[\s\S]*?\*\/|\/\/[^\r\n]*|@"(?:""|[^"])*"|"(?:\\.|[^"\\])*"|'(?:\\.|[^'\\])*'/g, ' ');
    const tokens = clean.match(/[A-Za-z_][A-Za-z_0-9]*|[^\s]/g) || [];
    const scopes = [], result = [];
    let pending = null;
    for (let i = 0; i < tokens.length; ++i) {
        const token = tokens[i];
        if (token === 'namespace') {
            let name = '';
            while (tokens[++i] !== '{' && i < tokens.length) name += tokens[i];
            scopes.push({kind: 'namespace', name});
        } else if (['class', 'struct', 'interface', 'enum', 'delegate'].includes(token)) {
            // Generic constraints (where T : class/struct) are not declarations.
            if (!/^[A-Za-z_]\w*$/.test(tokens[i + 1] || '') || tokens[i - 1] === ':') continue;
            let j = i + 1;
            if (token === 'delegate') {
                while (j < tokens.length && tokens[j] !== '(') ++j;
                j--;
                if (tokens[j] === '>') { while (tokens[j] !== '<') --j; --j; }
            }
            let name = tokens[j];
            if (tokens[j + 1] === '<') {
                let k = j + 1;
                do { name += tokens[k++]; } while (tokens[k - 1] !== '>' && k < tokens.length);
            }
            let k = i - 1;
            while (k >= 0 && !['{', '}', ';', ']'].includes(tokens[k])) --k;
            const modifiers = tokens.slice(k + 1, i);
            const parents = scopes.filter(s => s.kind === 'type');
            const visibility = modifiers.includes('public') ? 'public' : modifiers.includes('protected') ? 'protected' : modifiers.includes('private') ? 'private' : modifiers.includes('internal') ? 'internal' : parents.length ? 'private' : 'internal';
            const entry = {
                name: [...scopes.filter(s => s.kind === 'namespace').map(s => s.name), ...parents.map(s => s.name), name].join('.'),
                kind: token, visibility,
                public: visibility === 'public' && parents.every(s => s.public)
            };
            result.push(entry);
            if (token !== 'delegate') pending = {kind: 'type', name, public: entry.public};
            i = j;
        } else if (token === '{') { scopes.push(pending || {kind: 'block'}); pending = null; }
        else if (token === '}') scopes.pop();
    }
    return result;
}

export function inventory(referenceDirectory, files) {
    return files.filter(runtimePath).sort().map(file => {
        const bytes = fs.readFileSync(path.join(referenceDirectory, file));
        return {path: file, sha256: crypto.createHash('sha256').update(bytes).digest('hex'), types: declarations(bytes.toString('utf8'))};
    });
}

export function validateMatrix(matrix, expected, reference, root, {requireComplete = false} = {}) {
    function require(condition, message) { if (!condition) throw Error('uGUI matrix: ' + message); }
    require(matrix.schemaVersion === 1, 'unsupported schema version');
    require(matrix.reference?.commit === reference.commit && matrix.reference?.repository === reference.repository, 'reference mismatch');
    require(Array.isArray(matrix.files), 'files must be an array');
    const actual = new Map();
    const ids = new Set();
    for (const file of matrix.files) {
        require(!actual.has(file.path), 'duplicate file ' + file.path);
        actual.set(file.path, file);
        require(Array.isArray(file.types), 'types missing for ' + file.path);
        for (const type of file.types) {
            require(!ids.has(type.name), 'duplicate type ' + type.name); ids.add(type.name);
            require(statuses.includes(type.status), 'unknown status for ' + type.name);
            require(/^U(?:[0-9]|1[0-3])$/.test(type.stage), 'unknown stage for ' + type.name);
            require(typeof type.overload === 'string' && type.overload.length > 0, 'missing mapping for ' + type.name);
            require(typeof type.differences === 'string' && type.differences.length > 0, 'missing differences for ' + type.name);
            require(Array.isArray(type.tests), 'missing test list for ' + type.name);
            if (['implemented', 'adapted'].includes(type.status)) {
                require(type.tests.length > 0, 'completed type without tests: ' + type.name);
                for (const id of type.tests) {
                    const test = matrix.tests?.[id];
                    require(test && test.status === 'passed' && test.command && test.source && test.evidence, 'test not accepted: ' + id);
                    require(fs.existsSync(path.join(root, test.source)), 'test source missing: ' + id);
                }
            }
            if (type.status === 'blocked') require(type.blocker?.dependency && type.blocker?.minimumImplementation && type.blocker?.unblockWhen, 'incomplete blocker: ' + type.name);
        }
    }
    require(actual.size === expected.length, 'runtime file count mismatch');
    for (const file of expected) {
        const match = actual.get(file.path);
        require(match, 'missing file ' + file.path);
        require(match.sha256 === file.sha256, 'source hash mismatch: ' + file.path);
        require(match.types.length === file.types.length, 'type count mismatch: ' + file.path);
        for (const type of file.types) {
            const mapped = match.types.find(t => t.name === type.name);
            require(mapped, 'missing type ' + type.name);
            for (const field of ['kind', 'visibility', 'public']) require(mapped[field] === type[field], 'declaration mismatch: ' + type.name);
        }
    }
    require(Array.isArray(matrix.dependencies), 'missing engine dependency inventory');
    const dependencies = new Set();
    for (const dependency of matrix.dependencies) {
        require(!dependencies.has(dependency.name), 'duplicate dependency ' + dependency.name);
        dependencies.add(dependency.name);
        require(statuses.includes(dependency.status), 'unknown dependency status: ' + dependency.name);
        require(dependency.overload && dependency.differences && /^U(?:[0-9]|1[0-3])$/.test(dependency.stage), 'incomplete dependency: ' + dependency.name);
        require(Array.isArray(dependency.tests), 'missing dependency tests: ' + dependency.name);
        if (['implemented', 'adapted'].includes(dependency.status)) {
            require(dependency.tests.length > 0, 'completed dependency without tests: ' + dependency.name);
            for (const id of dependency.tests) {
                const test = matrix.tests?.[id];
                require(test?.status === 'passed' && test.command && test.source && test.evidence && fs.existsSync(path.join(root, test.source)), 'dependency test not accepted: ' + id);
            }
        }
        if (dependency.status === 'blocked') require(dependency.blocker?.dependency && dependency.blocker?.minimumImplementation && dependency.blocker?.unblockWhen, 'incomplete dependency blocker: ' + dependency.name);
    }
    if (requireComplete) {
        const unfinished = [...matrix.files.flatMap(f => f.types), ...matrix.dependencies].filter(t => !['implemented', 'adapted'].includes(t.status));
        require(unfinished.length === 0, `${unfinished.length} incomplete types/dependencies; full compatibility cannot be claimed`);
    }
    return {files: expected.length, types: ids.size, publicTypes: matrix.files.flatMap(f => f.types).filter(t => t.public).length,
        statuses: Object.fromEntries(statuses.map(status => [status, matrix.files.flatMap(f => f.types).filter(t => t.status === status).length]))};
}
