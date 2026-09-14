// S3 retains the full S1/S2 build/runtime regression and adds gameplay fixtures.
process.argv.push('--s3', '--s2', '--compat-build');
await import('./verify_s1.mjs');
