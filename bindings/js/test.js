'use strict';
// The binding, against the real library.
//
// No test framework: node:test and plain assertions are enough, and the thing
// under test needs a real native library rather than a mock.
const test = require('node:test');
const assert = require('node:assert');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const d = require('./index.js');

test('the library loads and agrees on the ABI', () => {
  assert.ok(d.version().length > 0, 'the library reports no version');
  assert.strictEqual(d.abiVersion(), d.REQUIRED_ABI);
});

test('defaults come from the library', () => {
  const o = d.defaultOptions();
  // Not a fixed table: the point is they were ASKED FOR. All-false would mean
  // the call never reached the library, which is the mistake this catches.
  assert.strictEqual(o.carrySidecar, true, 'a round trip is lossless by default');
  assert.strictEqual(o.deriveGeometry, true);
  assert.strictEqual(o.translateScripts, true);
  assert.strictEqual(o.strict, false);
  assert.strictEqual(o.synthesiseOcean, true);
});

test('something that is not a map is unknown', () => {
  const p = path.join(os.tmpdir(), 'dragoman-js-not-a-map.txt');
  fs.writeFileSync(p, 'hello');
  try {
    assert.strictEqual(d.detect(p), d.Format.UNKNOWN);
  } finally {
    fs.rmSync(p, { force: true });
  }
});

// The polarity test: if zero were read as failure, ok would come back true.
test('a failed conversion says why', () => {
  const out = path.join(os.tmpdir(), 'dragoman-js-should-not-appear');
  fs.rmSync(out, { recursive: true, force: true });
  const r = d.convert('/does/not/exist/anywhere.odmap', out, d.Format.GD5);
  assert.strictEqual(r.ok, false, 'converting a path that does not exist should fail');
  assert.ok(r.notes.length > 0, 'it failed silently');
  assert.strictEqual(r.worst, d.Severity.ERROR);
  assert.strictEqual(fs.existsSync(out), false, 'it wrote something anyway');
});

test('a real map converts and returns', { skip: !process.env.DRAGOMAN_TEST_MAP && 'set DRAGOMAN_TEST_MAP to run' }, () => {
  const map = process.env.DRAGOMAN_TEST_MAP;
  const from = d.detect(map);
  assert.notStrictEqual(from, d.Format.UNKNOWN, `${map} is not a map`);

  const to = from === d.Format.ODMAP ? d.Format.GD5 : d.Format.ODMAP;
  const out = path.join(os.tmpdir(), `dragoman-js-${process.pid}`);
  fs.rmSync(out, { recursive: true, force: true });

  const r = d.convert(map, out, to);
  assert.ok(r.ok, 'conversion failed: ' + JSON.stringify(r.notes.slice(0, 2)));
  assert.ok(fs.existsSync(out), 'it reported success and wrote nothing');
  for (const n of r.notes) {
    assert.ok(n.code.length > 0, 'a note with no code');
    assert.ok(n.message.length > 0, 'a note with no message');
  }

  // NOT read the way convert() is read: 1 is identical, 0 is differed.
  const rt = d.roundTripCheck(map, to);
  assert.strictEqual(rt.outcome, d.RoundTrip.IDENTICAL,
                     'the map did not come back unchanged (and 1, not 0, means identical)');

  fs.rmSync(out, { recursive: true, force: true });
});
