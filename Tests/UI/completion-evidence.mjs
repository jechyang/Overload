import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';

export const completionCommands = [
  ['Tools/UI/reference-ugui.mjs', '--check'],
  ['Tests/UI/runtime-matrix.test.mjs'],
  ...['field-codec','coroutine','tween','graphics','layout','font','input','image','controls','input-controls','gallery']
    .map(name => [`Tests/UI/run-${name}-validation.mjs`]),
  ['Tests/UI/run-flappy-validation.mjs','--apps'],
  ...['Debug','Release'].map(config => ['Tests/UI/run-validation.mjs',config,'--managed','--foundations','--scripting-regression']),
  ['Tests/Skinning/run-validation.mjs','Debug','--editor-camera'],
];

export function completionFingerprint(root) {
  const hash=crypto.createHash('sha256');
  function walk(relative) {
    if(!fs.existsSync(path.join(root,relative)))return;
    for(const entry of fs.readdirSync(path.join(root,relative),{withFileTypes:true}).sort((a,b)=>a.name.localeCompare(b.name))) {
      if(['Build','bin','obj','.overload','.git'].includes(entry.name))continue;
      const file=relative+'/'+entry.name;
      if(entry.isDirectory())walk(file);
      else if(!file.endsWith('.local.ovproject') && /\.(cs|cpp|h|mjs|json|ovscene|ovproject|vcxproj|csproj)$/.test(file)) {
        hash.update(file);hash.update(fs.readFileSync(path.join(root,file)));
      }
    }
  }
  for(const directory of ['Sources/OvManaged','Sources/OvCore','Sources/OvRendering','Sources/OvEditor','Sources/OvGame',
    'Tests/UI','Tests/Scripting','Tests/Skinning','Tests/RenderingCommands','Tools/UI',
    'Examples/FlappyBird','Examples/UIGallery','Examples/ManagedUI'])walk(directory);
  return hash.digest('hex');
}

export function requireCompletionEvidence(root) {
  const file=path.join(root,'Build/UIValidation/completion.json');
  if(!fs.existsSync(file))throw Error('Runtime completion evidence missing. Run node Tests/UI/run-completion-validation.mjs after building Debug and Release libraries.');
  const receipt=JSON.parse(fs.readFileSync(file,'utf8'));
  if(!receipt.success || receipt.fingerprint!==completionFingerprint(root) ||
    JSON.stringify(receipt.checks?.map(c=>c.args))!==JSON.stringify(completionCommands) || receipt.checks.some(c=>c.exit!==0))
    throw Error('Runtime completion evidence is incomplete or stale; classification alone is not acceptance.');
  return receipt;
}
