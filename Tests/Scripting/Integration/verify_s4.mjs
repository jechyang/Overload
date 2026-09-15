// S4 includes unchanged S1/S2/S3 acceptance plus field persistence and real Editor checks.
process.argv.push('--s4', '--s3', '--s2', '--compat-build');
await import('./verify_s1.mjs');
