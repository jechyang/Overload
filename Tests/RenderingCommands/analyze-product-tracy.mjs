// tracy-csvexport -u -f Benchmark <capture.tracy> > events.csv
import fs from 'node:fs';
import path from 'node:path';
import assert from 'node:assert/strict';

const file = path.resolve(process.argv[2] || '');
if (!file.endsWith('.csv')) throw Error('Usage: node analyze-product-tracy.mjs <events.csv>');
const lines = fs.readFileSync(file, 'utf8').trim().split(/\r?\n/);
lines.shift();
const events = lines.map(line => {
  const cells = line.split(',');
  assert.equal(cells.length, 6, 'Unexpected Tracy CSV format');
  return {name: cells[0], source: cells[1], start: Number(cells[3]),
    end: Number(cells[3]) + Number(cells[4]), thread: cells[5]};
});
const cases = events.filter(e => /^Benchmark (Immediate|Commands|Threaded) Case$/.test(e.name)).sort((a, b) => a.start - b.start);
assert(cases.length === 9 || cases.length === 15, 'Incomplete product benchmark capture');
assert.equal(cases.filter(e => e.name === 'Benchmark Immediate Case').length, 3);
assert.equal(cases.filter(e => e.name === 'Benchmark Commands Case').length, cases.length === 9 ? 3 : 6);
assert.equal(cases.filter(e => e.name === 'Benchmark Threaded Case').length, cases.length === 9 ? 3 : 6);
const details = cases.map((test, index) => {
  const inside = name => events.filter(e => e.name === name && e.start >= test.start && e.end <= test.end);
  const main = inside('Benchmark Main Frame');
  const updates = inside('Benchmark Main Update');
  const render = inside('Benchmark Render Frame');
  assert(main.length === 384 || main.length === 224);
  assert.equal(updates.length, main.length);
  assert(main.every(e => e.thread === test.thread) && updates.every(e => e.thread === test.thread));
  const threaded = test.name.includes('Threaded');
  if (test.name.includes('Immediate')) assert.equal(render.length, 0);
  else {
    assert.equal(render.length, main.length);
    assert.equal(new Set(render.map(e => e.thread)).size, 1);
    assert(render.every(e => threaded ? e.thread !== test.thread : e.thread === test.thread));
    assert(render.every(e => /Release-(game|editor)-benchmark/.test(e.source)), 'Wrong test executable/source');
  }
  let overlappingUpdates = 0, overlapNs = 0;
  for (const update of updates) {
    let span = 0;
    for (const frame of render) span += Math.max(0, Math.min(update.end, frame.end) - Math.max(update.start, frame.start));
    if (span > 0) ++overlappingUpdates;
    overlapNs += span;
  }
  if (threaded) assert(overlappingUpdates > 0, 'No actual Update / Render overlap');
  else assert.equal(overlappingUpdates, 0);
  return {index, mode: test.name, frames: main.length, mainThread: test.thread,
    renderThread: render[0]?.thread ?? null, overlappingUpdates, overlapWallMs: overlapNs / 1e6};
});
const output = file.replace(/\.csv$/, '-analysis.json');
fs.writeFileSync(output, JSON.stringify({source: file, cases: details}, null, 2) + '\n');
console.log(JSON.stringify(details, null, 2));
console.log('PASS: complete Tracy cases, thread identity and actual Update / Render overlap; ' + output);
