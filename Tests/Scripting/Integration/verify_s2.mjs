// Share the native-host/toolchain harness; S2 adds real project/Actor/Lua tests.
process.argv.push('--s2');
await import('./verify_s1.mjs');
