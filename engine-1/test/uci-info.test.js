import test from 'node:test';
import assert from 'node:assert/strict';
import { parseInfo } from '../app/uci-info.js';

test('parses a centipawn info line', () => {
  const info = parseInfo('info depth 14 score cp 16 nodes 1065639 nps 1473912 time 723 pv d2d4 e5d4 f3d4');
  assert.deepEqual(info, {
    bestMove: 'd2d4', score: 16, mate: null, pv: ['d2d4', 'e5d4', 'f3d4'],
    depth: 14, nodes: 1065639, timeMs: 723, nps: 1473912,
  });
});

test('parses mate scores for either side', () => {
  const win = parseInfo('info depth 5 score mate 2 nodes 10 nps 10 time 1 pv h5f7');
  assert.equal(win.mate, 2);
  assert.ok(win.score > 0);
  const loss = parseInfo('info depth 5 score mate -3 nodes 10 nps 10 time 1 pv e8d8');
  assert.equal(loss.mate, -3);
  assert.ok(loss.score < 0);
});

test('ignores lines that are not info', () => {
  assert.equal(parseInfo('bestmove e2e4'), null);
});

test('an info line without a pv has no best move', () => {
  assert.equal(parseInfo('info depth 0').bestMove, null);
});
