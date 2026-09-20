import fs from 'node:fs';
import path from 'node:path';
import {spawnSync} from 'node:child_process';

// Probe in the build worker, never on the editor Main thread. No shell or PATH changes.
export function resolvePython(root, explicit, env = process.env) {
  const candidates = [];
  const add = (exe, args = []) => { if (exe) candidates.push({exe,args}); };
  if (explicit) {
    if (!path.isAbsolute(explicit)) throw Error('csharp_python/--python must be an absolute Python 3.10+ executable path');
    add(explicit);
  } else {
    add(env.OV_REFLECTION_PYTHON);
    add(path.join(root,'Build/Toolchains/python/python.exe'));
    // Reuse the tool selected for the native reflection build, when available.
    const project = path.join(root,'Tools/OvReflectGen/Projects/OvReflectionGen.vcxproj');
    if (fs.existsSync(project)) {
      const text = fs.readFileSync(project,'utf8');
      const command = text.match(/<NMakeBuildCommandLine>([^<]+)<\/NMakeBuildCommandLine>/)?.[1]
        .replaceAll('&quot;','"').replaceAll('&amp;','&');
      const exe = command?.match(/^\s*"([^"]+)"\s+"[^"]*reflection\.py"/)?.[1];
      if (exe && path.isAbsolute(exe)) add(exe);
    }
    add('py.exe',['-3']); add('python3.exe'); add('python.exe');
  }
  const attempted = [], seen = new Set();
  for (const {exe,args} of candidates) {
    const key = JSON.stringify([exe,args]); if (seen.has(key)) continue; seen.add(key);
    const result = spawnSync(exe,[...args,'-c','import sys,json; print(json.dumps([sys.executable,list(sys.version_info[:3])]))'],
      {env, windowsHide:true, encoding:'utf8', timeout:5000, maxBuffer:65536});
    try {
      if (result.error || result.status !== 0) throw Error(result.error?.code || 'exit '+result.status);
      const [resolved,version] = JSON.parse(result.stdout.trim());
      if (!path.isAbsolute(resolved) || !fs.statSync(resolved).isFile()) throw Error('invalid executable');
      if (version[0] !== 3 || version[1] < 10) throw Error('Python '+version.join('.')+'; requires 3.10+');
      console.log('Reflection Python: '+resolved+' ('+version.join('.')+')');
      return resolved;
    } catch (error) { attempted.push(exe+': '+error.message); }
  }
  throw Error('No usable Python 3.10+ for C# reflection. Set csharp_python in the .ovproject file to an absolute Python 3 executable, then retry Build.\n'+attempted.join('\n'));
}
