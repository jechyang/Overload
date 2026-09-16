// Fixed-source complete S6 receipt including S1-S5 regression.
process.argv.push('--s6','--s5','--s4','--s3','--s2','--compat-build');
await import('./verify_s1.mjs');
