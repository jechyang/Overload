// One fixed-source receipt for S5 and all S1-S4/Lua regressions.
process.argv.push('--s5','--s4','--s3','--s2','--compat-build');
await import('./verify_s1.mjs');
