import assert from 'node:assert/strict';
import {test} from 'node:test';
import {boxToRender, headingToDegrees, headingToYaw, toRender} from '../src/units.js';

test('engine units map to render space as the glTF writer does', () => {
  // Project 0's spawn, header +0xB4: the old pre-converted JSON held (-3.19, 6.25, -31.87).
  const [x, y, z] = toRender([-319, -625, -3187]);
  assert.ok(Math.abs(x + 3.19) < 1e-9 && Math.abs(y - 6.25) < 1e-9 && Math.abs(z + 31.87) < 1e-9);
});

test('a LINK box keeps min below max after Y is negated', () => {
  // Project 0 LINK0, the statue's mouth.
  const {min, max} = boxToRender([-1227, -1796, 237], [-1152, -1640, 339]);
  assert.deepEqual(min.map(v => +v.toFixed(2)), [-12.27, 16.4, 2.37]);
  assert.deepEqual(max.map(v => +v.toFixed(2)), [-11.52, 17.96, 3.39]);
});

test('12-bit headings: 4096 is a full turn', () => {
  assert.equal(headingToDegrees(1024), 90);
  assert.ok(Math.abs(headingToYaw(3046) - 4.6725) < 1e-4);
});
