import path from 'node:path';
import { verifyApps } from './verify_s3_apps.mjs';
export function verifyFields(options) {
  const { root, output, run, compiler, nativeOptions, libraries, gen, dotnetRoot, production, project } = options;
  const object=path.join(output,'s4.obj'),executable=path.join(output,'s4.exe');
  run('s4-compile',path.join(compiler,'cl.exe'),['/nologo','/std:c++20','/EHsc','/MDd','/W4','/WX','/c',
    '/I'+gen,'/I'+path.join(root,'Dependencies/glfw/include'),'/I'+path.join(root,'Dependencies/bullet3/src'),
    ...nativeOptions,path.join(root,'Tests/Scripting/Integration/native_s4_validation.cpp'),'/Fo'+object]);
  run('s4-link',path.join(compiler,'link.exe'),['/nologo','/OPT:REF','/OPT:ICF','/OUT:'+executable,object,...libraries,
    ...['opengl32','dbghelp','ws2_32','winmm','imm32','version','user32','gdi32','shell32','ole32','oleaut32','advapi32','comdlg32','uuid'].map(s=>s+'.lib')]);
  for(const mode of ['default','render-thread']) run('s4-'+mode,executable,[dotnetRoot,production,project,mode],0,120000);
  verifyApps({...options,stage:'s4'});
}
