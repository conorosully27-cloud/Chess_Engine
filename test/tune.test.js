import { test, describe } from 'node:test';
import assert from 'node:assert/strict';
import { Position } from '../src/index.js';
import { evaluate } from '../src/engine/index.js';
import { WEIGHTS } from '../src/engine/weights.js';
import { layout, trace, linearEval, unflatten } from '../scripts/eval-trace.js';
import { formatWeights } from '../scripts/tune.js';

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
});
