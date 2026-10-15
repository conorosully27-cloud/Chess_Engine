import { test, describe } from 'node:test';
import assert from 'node:assert/strict';
import { Position, START_FEN, FLAG_CAPTURE, movePromotion, QUEEN } from '../src/index.js';
import {
  Engine, MATE, evaluate, BOOK_LINES, bookMove, chooseMove, positionFromHistory,
} from '../src/engine/index.js';

const engine = new Engine({ ttBits: 16 });

// The same position with colors swapped and the board flipped top to bottom.
function mirrorFen(fen) {
  const [placement, side, castling, ep, half, full] = fen.split(' ');
  const swapCase = (ch) => (ch === ch.toUpperCase() ? ch.toLowerCase() : ch.toUpperCase());
  const board = placement.split('/').reverse().map((rank) => [...rank].map(swapCase).join('')).join('/');
  const rights = castling === '-' ? '-' : [...castling].map(swapCase).sort((a, b) => 'KQkq'.indexOf(a) - 'KQkq'.indexOf(b)).join('');
  const passant = ep === '-' ? '-' : ep[0] + (9 - Number(ep[1]));
  return [board, side === 'w' ? 'b' : 'w', rights, passant, half, full].join(' ');
}

// Brute force, independent of the engine: can the side to move force mate
// within `plies` plies?
function canForceMate(pos, plies) {
  for (const move of pos.legalMoves()) {
    pos.makeMove(move);
    const mates = defenderIsMated(pos, plies - 1);
    pos.unmakeMove();
    if (mates) return true;
  }
  return false;
}

function defenderIsMated(pos, plies) {
  const moves = pos.legalMoves();
  if (moves.length === 0) return pos.inCheck();
  if (plies <= 0) return false;
  for (const move of moves) {
    pos.makeMove(move);
    const mated = canForceMate(pos, plies - 1);
    pos.unmakeMove();
    if (!mated) return false;
  }
  return true;
}

const POSITIONS = [
  START_FEN,
  'r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1',
  'rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8',
  'r4rk1/1pp1qppp/p1np1n2/2b1p1B1/2B1P1b1/P1NP1N2/1PP1QPPP/R4RK1 w - - 0 10',
  '8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1',
  '4k3/8/8/3r4/8/8/3Q4/4K3 w - - 0 1',
  'r1bq1b1r/ppp3pp/2n1k3/3np3/2B5/5Q2/PPPP1PPP/RNB1K2R w KQ - 2 7',
];

describe('evaluation', () => {
  test('is the same for both colors in mirrored positions', () => {
    for (const fen of POSITIONS) {
      const mirrored = mirrorFen(fen);
      assert.equal(evaluate(new Position(fen)), evaluate(new Position(mirrored)), `${fen} vs ${mirrored}`);
    }
  });

  test('the start position is roughly level', () => {
    assert.ok(Math.abs(evaluate(new Position())) <= 30);
  });

  test('an extra rook is a big advantage for the side that has it', () => {
    const pos = new Position('4k3/8/8/8/8/8/8/R3K3 w - - 0 1');
    assert.ok(evaluate(pos) > 400);
    assert.ok(evaluate(new Position('4k3/8/8/8/8/8/8/R3K3 b - - 0 1')) < -400);
  });

  test('pushing a central pawn does not weaken an uncastled king', () => {
    // King shelter only counts for a castled king. Engine 1.0 took 24 off
    // 1.e4's gain for the hole in front of the king on e1.
    const before = new Position();
    const after = new Position('rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR w KQkq - 0 1');
    assert.ok(evaluate(after) - evaluate(before) > 60);
  });

  test('dead draws score zero', () => {
    assert.equal(evaluate(new Position('8/8/8/4k3/8/8/8/2B1K3 w - - 0 1')), 0);
  });
});

describe('search', () => {
  test('leaves the position as it found it', () => {
    for (const fen of POSITIONS) {
      const pos = new Position(fen);
      const before = [pos.fen(), pos.hashKey()];
      engine.search(pos, { depth: 4 });
      assert.deepEqual([pos.fen(), pos.hashKey()], before);
    }
  });

  test('finds mate in one', () => {
    const backRank = engine.search(new Position('6k1/5ppp/8/8/8/8/8/R5K1 w - - 0 1'), { depth: 3 });
    assert.equal(backRank.bestMove, 'a1a8');
    assert.equal(backRank.mate, 1);

    const foolsMate = engine.search(positionFromHistory(['f2f3', 'e7e5', 'g2g4']), { depth: 3 });
    assert.equal(foolsMate.bestMove, 'd8h4');
    assert.equal(foolsMate.mate, 1);
  });

  test('finds mate in two, checked by brute force', () => {
    const pos = new Position('k7/8/2K5/8/8/8/8/7R w - - 0 1');
    const result = engine.search(pos, { depth: 6 });
    assert.equal(result.mate, 2);
    pos.play(result.bestMove);
    assert.ok(defenderIsMated(pos, 2), `${result.bestMove} does not force mate in 2`);
  });

  test('mates rather than stalemates', () => {
    // Qf7 would be stalemate; Qf8 is mate.
    const result = engine.search(new Position('7k/8/6K1/8/8/8/8/5Q2 w - - 0 1'), { depth: 4 });
    assert.equal(result.bestMove, 'f1f8');
  });

  test('sees it is being mated', () => {
    // Black's only move is Kb8, then Rh8 is mate.
    const result = engine.search(new Position('k7/8/1K6/8/8/8/8/7R b - - 0 1'), { depth: 4 });
    assert.equal(result.bestMove, 'a8b8');
    assert.equal(result.mate, -1);
  });

  test('takes free material and avoids traps', () => {
    assert.equal(engine.search(new Position('4k3/8/8/3r4/8/8/3Q4/4K3 w - - 0 1'), { depth: 5 }).bestMove, 'd2d5');
    // Qxd5 wins a pawn but cxd5 wins the queen.
    assert.notEqual(engine.search(new Position('4k3/8/2p5/3p4/8/8/8/3QK3 w - - 0 1'), { depth: 5 }).bestMove, 'd1d5');
  });

  test('a position with no moves has no best move', () => {
    const mated = engine.search(positionFromHistory(['f2f3', 'e7e5', 'g2g4', 'd8h4']), { depth: 3 });
    assert.equal(mated.bestMove, null);
    assert.equal(mated.score, -MATE);
    const stalemate = engine.search(new Position('k7/8/1Q6/8/8/8/8/7K b - - 0 1'), { depth: 3 });
    assert.equal(stalemate.bestMove, null);
    assert.equal(stalemate.score, 0);
  });

  test('reports progress after each depth', () => {
    const infos = [];
    const result = engine.search(new Position(), { depth: 5, onInfo: (info) => infos.push(info) });
    assert.deepEqual(infos.map((i) => i.depth), [1, 2, 3, 4, 5]);
    assert.equal(result.depth, 5);
    assert.ok(result.pv.length >= 1 && result.pv[0] === result.bestMove);
    // The principal variation is a sequence of legal moves.
    const pos = new Position();
    for (const uci of result.pv) pos.play(uci);
  });

  test('stops on time', () => {
    const start = performance.now();
    const result = engine.search(new Position(POSITIONS[1]), { timeMs: 200 });
    const elapsed = performance.now() - start;
    assert.ok(elapsed < 600, `took ${elapsed}ms`);
    assert.ok(result.bestMove);
  });
});

describe('null moves and tactical move generation', () => {
  test('a null move passes the turn and can be undone', () => {
    const pos = positionFromHistory(['e2e4', 'd7d5', 'e4e5', 'f7f5']); // en passant is available
    const before = [pos.fen(), pos.hashKey()];
    pos.makeNullMove();
    const passed = new Position(pos.fen());
    assert.equal(pos.hashKey(), passed.hashKey(), 'hash matches the same position with the other side to move');
    assert.equal(pos.epSquare, -1);
    pos.unmakeNullMove();
    assert.deepEqual([pos.fen(), pos.hashKey()], before);
  });

  test('tactical generation is exactly the captures and queen promotions', () => {
    for (const fen of [...POSITIONS, '1r2k3/P7/8/8/8/8/8/4K3 w - - 0 1']) {
      const pos = new Position(fen);
      const tactical = pos.generatePseudoLegalMoves([], true).sort();
      const expected = pos.generatePseudoLegalMoves().filter(
        (m) => (m & FLAG_CAPTURE && (!movePromotion(m) || movePromotion(m) === QUEEN)) || movePromotion(m) === QUEEN,
      ).sort();
      assert.deepEqual(tactical, expected, fen);
    }
  });
});

describe('playing a game', () => {
  // A seeded random source so the tests are repeatable.
  function seeded(seed) {
    return () => (seed = (seed * 1103515245 + 12345) >>> 0) / 2 ** 32;
  }

  test('every book line is legal', () => {
    for (const line of BOOK_LINES) positionFromHistory(line);
  });

  test('opens from the book, then thinks for itself', () => {
    const fromBook = chooseMove(engine, new Position(), [], { random: seeded(1) });
    assert.ok(fromBook.book);
    assert.ok(['e2e4', 'd2d4', 'c2c4', 'g1f3'].includes(fromBook.bestMove));
    assert.equal(bookMove(['a2a3']), null);
    const history = ['a2a3', 'h7h6'];
    const own = chooseMove(engine, positionFromHistory(history), history, { timeMs: 200 });
    assert.ok(!own.book && own.bestMove);
  });

  test('without the book it searches even in the opening', () => {
    const result = chooseMove(engine, new Position(), [], { timeMs: 100, useBook: false });
    assert.ok(!result.book && result.depth >= 1);
  });
});
