// Analyze a product-frame-metrics*.csv produced by --{game,editor}-benchmark.
// CPU and GPU timestamps have different clock origins: never subtract across them.
import fs from 'node:fs';
import path from 'node:path';
import assert from 'node:assert/strict';

const file = path.resolve(process.argv[2] || '');
if (!file.endsWith('.csv')) throw Error('Usage: node analyze-product-benchmark.mjs <metrics.csv>');
const lines = fs.readFileSync(file, 'utf8').trim().split(/\r?\n/);
const header = lines.shift().split(',');
const rows = lines.map(line => {
  const cells = line.split(',');
  assert.equal(cells.length, header.length);
  return Object.fromEntries(cells.map((value, i) => [header[i], i < 2 ? value :
    header[i].startsWith('gpu_') ? BigInt(value) : Number(value)]));
});
assert(rows.length > 0, 'Empty benchmark');
const products = [...new Set(rows.map(row => row.product))];
assert.deepEqual(products, products[0] === 'game' ? ['game'] : products[0] === 'editor-recording' ? ['editor-recording'] : ['editor', 'editor-picking']);
function stats(values) {
  if (!values.length) return null;
  assert(values.every(Number.isFinite));
  const sorted = [...values].sort((a, b) => a - b);
  const q = p => sorted[Math.min(sorted.length - 1, Math.floor(sorted.length * p))];
  return {mean: sorted.reduce((a, b) => a + b, 0) / sorted.length,
    min: sorted[0], p50: q(.5), p95: q(.95), p99: q(.99), max: sorted.at(-1)};
}
function overlap(producer, consumer) {
  let total = 0, frames = 0;
  for (const [start, end] of producer) {
    let duration = 0;
    for (const [begin, finish] of consumer) duration += Math.max(0, Math.min(end, finish) - Math.max(start, begin));
    total += duration;
    if (duration > 0) ++frames;
  }
  return {frames, wallMs: total / 1e6};
}
const groups = [];
for (const product of new Set(rows.map(row => row.product))) {
  const modes = [...new Set(rows.filter(row => row.product === product).map(row => row.mode))];
  assert.deepEqual(modes, product === 'editor-recording' ? ['legacy-recording', 'serial-encoding', 'parallel-encoding'] : product === 'editor-picking' ? ['commands', 'threaded'] : ['immediate', 'commands', 'threaded']);
  for (const mode of modes) {
    const samples = rows.filter(row => row.product === product && row.mode === mode);
    const expectedPerRepeat = product === 'editor-recording' ? 160 : product === 'editor-picking' ? 192 : 320;
    const threaded = mode === 'threaded' || product === 'editor-recording';
    assert.equal(samples.length, 3 * expectedPerRepeat, 'Incomplete measured samples');
    assert.deepEqual([...new Set(samples.map(r => r.repeat))], [0, 1, 2]);
    const delta = (end, start, filter = () => true) => stats(samples.filter(filter).map(r => Number(r[end] - r[start]) / 1e6));
    for (const r of samples) {
      assert(Object.entries(r).every(([k, v]) => ['product', 'mode'].includes(k) ||
        (k.startsWith('gpu_') ? typeof v === 'bigint' : Number.isSafeInteger(v))));
      assert(r.main_start_ns <= r.main_updated_ns && r.main_updated_ns <= r.main_end_ns);
      assert(r.main_start_ns <= r.render_begin_ns && r.render_begin_ns <= r.pre_present_ns && r.pre_present_ns <= r.present_return_ns);
      assert(r.gpu_end_ns >= r.gpu_begin_ns && r.gpu_begin_ns > 0);
      if (mode !== 'immediate') assert(r.present_return_ns <= r.retired_ns && r.retired_ns <= r.completion_observed_ns);
      if (threaded) {
        assert(r.main_updated_ns <= r.wait_start_ns && r.wait_start_ns <= r.admitted_ns && r.admitted_ns <= r.render_begin_ns);
        assert(r.queued_at_admission >= 1 && r.queued_at_admission <= r.inflight_at_admission && r.inflight_at_admission <= 2);
      }
      if (product.startsWith('editor') && mode !== 'immediate') assert(r.interaction_observed_ns >= r.retired_ns);
    }
    const repeats = [...new Set(samples.map(r => r.repeat))].map(repeat => {
      const batch = samples.filter(r => r.repeat === repeat);
      assert.equal(batch.length, expectedPerRepeat);
      for (let i = 1; i < batch.length; ++i) {
        assert.equal(batch[i].frame, batch[i - 1].frame + 1);
        assert(batch[i].main_start_ns >= batch[i - 1].main_end_ns);
        assert(batch[i].render_begin_ns >= batch[i - 1].present_return_ns);
      }
      const tail = batch.at(-1).run_tail_ns;
      assert(batch.every(r => r.run_tail_ns === tail && tail >= r.present_return_ns));
      const concurrent = overlap(batch.map(r => [r.main_start_ns, r.main_updated_ns]), batch.map(r => [r.render_begin_ns, r.pre_present_ns]));
      if (mode === 'commands') assert.equal(concurrent.frames, 0);
      if (threaded) assert(concurrent.frames > 0);
      return {repeat, frames: batch.length, framesPerSecond: batch.length * 1e9 / (tail - batch[0].main_start_ns),
        producerConsumerOverlap: threaded ? concurrent : null};
    });
    groups.push({product, mode, samples: samples.length, repeats,
      mainFrameMs: delta('main_end_ns', 'main_start_ns'),
      mainPreUpdateAndUpdateMs: delta('main_updated_ns', 'main_start_ns'),
      mainPostUpdateMs: delta('main_end_ns', 'main_updated_ns'),
      renderBeforePresentMs: delta('pre_present_ns', 'render_begin_ns'),
      presentCallMs: delta('present_return_ns', 'pre_present_ns'),
      gpuRenderSpanMs: delta('gpu_end_ns', 'gpu_begin_ns'),
      capacityWaitAndAdmissionMs: delta('admitted_ns', 'wait_start_ns', r => r.admitted_ns > 0),
      admissionToRenderMs: delta('render_begin_ns', 'admitted_ns', r => r.admitted_ns > 0),
      frameStartToPresentReturnMs: delta('present_return_ns', 'main_start_ns'),
      frameStartToRetireMs: delta('retired_ns', 'main_start_ns', r => r.retired_ns > 0),
      frameStartToCompletionObservedMs: mode === 'immediate' ? null : delta('completion_observed_ns', 'main_start_ns'),
      frameStartToInteractionObservedMs: mode === 'immediate' ? null : delta('interaction_observed_ns', 'main_start_ns', r => r.interaction_observed_ns > 0),
      queuedAtAdmission: threaded ? stats(samples.map(r => r.queued_at_admission)) : null,
      inFlightAtAdmission: threaded ? stats(samples.map(r => r.inflight_at_admission)) : null});
  }
}
const output = file.replace(/\.csv$/, '-analysis.json');
fs.writeFileSync(output, JSON.stringify({source: file, connected: path.basename(file).includes('connected'), groups}, null, 2) + '\n');
console.log(JSON.stringify(groups.map(g => ({product: g.product, mode: g.mode, samples: g.samples,
  fps: g.repeats.map(r => r.framesPerSecond), mainP50Ms: g.mainFrameMs.p50, gpuP50Ms: g.gpuRenderSpanMs.p50,
  presentLatencyP95Ms: g.frameStartToPresentReturnMs.p95, interactionLatencyP95Ms: g.frameStartToInteractionObservedMs?.p95})), null, 2));
console.log('PASS: timing order, monotonic frame IDs, two-frame capacity and measured overlap; ' + output);
