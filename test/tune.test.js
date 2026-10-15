import { test, describe } from 'node:test';
import assert from 'node:assert/strict';
import { writeFileSync, mkdtempSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { Position } from '../src/index.js';
import { evaluate } from '../src/engine/index.js';
import { WEIGHTS } from '../src/engine/weights.js';
import { layout, trace, linearEval, unflatten } from '../scripts/eval-trace.js';
import { formatWeights, loadPositions, error, gradient } from '../scripts/tune.js';

// Positions from seeded random games, so every evaluation term turns up:
// openings, middlegames, endgames, promotions, bare kings.
function randomPositions(count) {
  let seed = 7;
  const random = () => ((seed = (seed * 1103515245 + 12345) >>> 0) / 2 ** 32);
  const positions = [];
  while (positions.length < count) {
    const pos = new Position();
    for (let ply = 0; ply < 300 && pos.hasLegalMove(); ply++) {
      const moves = pos.legalMoves();
      pos.makeMove(moves[Math.floor(random() * moves.length)]);
      if (ply % 7 === 0) positions.push(new Position(pos.fen()));
    }
  }
  return positions;
}

describe('tuning', () => {
  const lay = layout(WEIGHTS);

  test("the tuner's linear model matches evaluate()", () => {
    for (const pos of randomPositions(2000)) {
      const t = trace(pos, lay);
      const score = evaluate(pos);
      if (t === null) {
        assert.equal(score, 0, pos.fen());
        continue;
      }
      // evaluate() is from the side to move and rounds; the model is from
      // White's side and doesn't.
      const model = linearEval(t, lay.values, lay.kinds);
      assert.ok(Math.abs(t.stm * score - model) <= 1, `${pos.fen()}: ${t.stm * score} vs ${model}`);
    }
  });

  test('weights.js survives a round trip through the tuner', () => {
    assert.deepEqual(unflatten(WEIGHTS, lay.values), WEIGHTS);
    const text = formatWeights(WEIGHTS);
    assert.ok(text.includes('export const WEIGHTS'));
    assert.equal(text, formatWeights(unflatten(WEIGHTS, lay.values)));
  });

  test("the tuner's gradient matches the error's slope, king danger included", async () => {
    // Middlegame positions with made-up results, so king danger is in play.
    const dir = mkdtempSync(join(tmpdir(), 'tune-test-'));
    const file = join(dir, 'positions.epd');
    const results = ['1-0', '0-1', '1/2-1/2'];
    const lines = randomPositions(600).filter((pos, i) => i % 3 === 0)
      .map((pos, i) => `${pos.fen().split(' ').slice(0, 4).join(' ')} c9 "${results[i % 3]}";`);
    writeFileSync(file, lines.join('\n'));
    const { train } = await loadPositions(file, lay, Infinity, 0);
    rmSync(dir, { recursive: true });

    const weights = Float64Array.from(lay.values);
    const grad = new Float64Array(weights.length);
    gradient(train, weights, 1, grad);
    const check = (i) => {
      const h = 0.01;
      weights[i] += h;
      const up = error(train, weights, 1);
      weights[i] -= 2 * h;
      const down = error(train, weights, 1);
      weights[i] += h;
      const slope = (up - down) / (2 * h);
      assert.ok(Math.abs(slope - grad[i]) <= 1e-3 * Math.abs(slope) + 1e-9, `${lay.names[i]}: ${grad[i]} vs ${slope}`);
    };
    const danger = lay.offset.kingDanger;
    assert.ok(grad[danger + WEIGHTS.kingDanger.length - 1] !== 0, 'no king danger in the sample');
    for (let i = danger; i < danger + WEIGHTS.kingDanger.length; i++) check(i);
    check(lay.offset['material.mg'] + 5);
    check(lay.offset.tempo);
  });
});
