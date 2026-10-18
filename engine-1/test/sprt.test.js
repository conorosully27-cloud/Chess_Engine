import { test } from 'node:test';
import assert from 'node:assert/strict';
import { scoreToElo, eloEstimate, parseSprt, sprtLlr } from '../scripts/sprt.js';

test('score to Elo', () => {
  assert.equal(scoreToElo(0.5), 0);
  assert.ok(Math.abs(scoreToElo(0.75) - 190.85) < 0.01);
  assert.ok(Math.abs(scoreToElo(0.25) + 190.85) < 0.01);
  assert.equal(scoreToElo(1), Infinity);
});

test('Elo estimate brackets the result', () => {
  const { elo, low, high } = eloEstimate({ win: 60, draw: 20, loss: 20 });
  assert.ok(low < elo && elo < high);
  assert.ok(Math.abs(elo - scoreToElo(0.7)) < 1e-9);
});

test('LLR bounds and direction', () => {
  const sprt = parseSprt('0,10', 0.05, 0.05);
  assert.ok(Math.abs(sprt.upper - Math.log(19)) < 1e-9);
  assert.ok(Math.abs(sprt.lower + Math.log(19)) < 1e-9);
  assert.ok(sprtLlr({ win: 60, draw: 40, loss: 40 }, sprt) > 0);
  assert.ok(sprtLlr({ win: 40, draw: 40, loss: 60 }, sprt) < 0);
  assert.throws(() => parseSprt('10,0', 0.05, 0.05));
});

// Simulated matches between engines with a known Elo gap (40% draws): the
// test should accept the right hypothesis about as reliably as alpha/beta say.
test('SPRT reaches the right verdict in simulated matches', () => {
  let seed = 12345;
  const random = () => (seed = (seed * 1103515245 + 12345) >>> 0) / 2 ** 32;
  const sprt = parseSprt('0,10', 0.05, 0.05);
  const run = (trueElo) => {
    const expected = 1 / (1 + 10 ** (-trueElo / 400));
    const drawRate = 0.4;
    const win = expected - drawRate / 2;
    const tally = { win: 0, draw: 0, loss: 0 };
    for (let games = 0; games < 100000; games++) {
      const r = random();
      tally[r < win ? 'win' : r < win + drawRate ? 'draw' : 'loss']++;
      const llr = sprtLlr(tally, sprt);
      if (llr >= sprt.upper) return 'H1';
      if (llr <= sprt.lower) return 'H0';
    }
    return 'none';
  };
  const runs = 200;
  const count = (elo, verdict) => Array.from({ length: runs }, () => run(elo)).filter((v) => v === verdict).length;
  const falsePositives = count(0, 'H1') / runs; // no real gain, but "accepted"
  const falseNegatives = count(10, 'H0') / runs; // a real 10 Elo gain, but rejected
  const clearGain = count(40, 'H1') / runs;
  assert.ok(falsePositives <= 0.1, `false positives ${falsePositives}`);
  assert.ok(falseNegatives <= 0.1, `false negatives ${falseNegatives}`);
  assert.ok(clearGain >= 0.98, `40 Elo gains accepted ${clearGain}`);
});
