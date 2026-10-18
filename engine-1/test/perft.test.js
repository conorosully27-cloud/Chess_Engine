// Perft counts every leaf of the legal move tree to a fixed depth. Matching
// published counts exactly is strong evidence that every rule (castling, en
// passant, promotion, pins, checks) is implemented correctly.

import { test } from 'node:test';
import assert from 'node:assert/strict';
import { Position, START_FEN } from '../src/index.js';

// https://www.chessprogramming.org/Perft_Results
const STANDARD = [
  { name: 'start position', fen: START_FEN, counts: [20, 400, 8902, 197281, 4865609] },
  {
    name: 'Kiwipete',
    fen: 'r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1',
    counts: [48, 2039, 97862, 4085603],
  },
  {
    name: 'position 3 (en passant, discovered checks)',
    fen: '8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1',
    counts: [14, 191, 2812, 43238, 674624],
  },
  {
    name: 'position 4 (promotions, castling)',
    fen: 'r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1',
    counts: [6, 264, 9467, 422333],
  },
  {
    name: 'position 4 mirrored',
    fen: 'r2q1rk1/pP1p2pp/Q4n2/bbp1p3/Np6/1B3NBn/pPPP1PPP/R3K2R b KQ - 0 1',
    counts: [6, 264, 9467, 422333],
  },
  {
    name: 'position 5',
    fen: 'rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8',
    counts: [44, 1486, 62379, 2103487],
  },
  {
    name: 'position 6',
    fen: 'r4rk1/1pp1qppp/p1np1n2/2b1p1B1/2B1P1b1/P1NP1N2/1PP1QPPP/R4RK1 w - - 0 10',
    counts: [46, 2079, 89890, 3894594],
  },
];

// Edge cases collected by Martin Sedlak (talkchess.com), each aimed at one rule.
const EDGE_CASES = [
  { name: 'illegal en passant #1', fen: '3k4/3p4/8/K1P4r/8/8/8/8 b - - 0 1', depth: 6, nodes: 1134888 },
  { name: 'illegal en passant #2', fen: '8/8/4k3/8/2p5/8/B2P2K1/8 w - - 0 1', depth: 6, nodes: 1015133 },
  { name: 'en passant capture checks opponent', fen: '8/8/1k6/2b5/2pP4/8/5K2/8 b - d3 0 1', depth: 6, nodes: 1440467 },
  { name: 'short castling gives check', fen: '5k2/8/8/8/8/8/8/4K2R w K - 0 1', depth: 6, nodes: 661072 },
  { name: 'long castling gives check', fen: '3k4/8/8/8/8/8/8/R3K3 w Q - 0 1', depth: 6, nodes: 803711 },
  { name: 'castling rights', fen: 'r3k2r/1b4bq/8/8/8/8/7B/R3K2R w KQkq - 0 1', depth: 4, nodes: 1274206 },
  { name: 'castling prevented', fen: 'r3k2r/8/3Q4/8/8/5q2/8/R3K2R b KQkq - 0 1', depth: 4, nodes: 1720476 },
  { name: 'promote out of check', fen: '2K2r2/4P3/8/8/8/8/8/3k4 w - - 0 1', depth: 6, nodes: 3821001 },
  { name: 'discovered check', fen: '8/8/1P2K3/8/2n5/1q6/8/5k2 b - - 0 1', depth: 5, nodes: 1004658 },
  { name: 'promote to give check', fen: '4k3/1P6/8/8/8/8/K7/8 w - - 0 1', depth: 6, nodes: 217342 },
  { name: 'underpromote to give check', fen: '8/P1k5/K7/8/8/8/8/8 w - - 0 1', depth: 6, nodes: 92683 },
  { name: 'self stalemate', fen: 'K1k5/8/P7/8/8/8/8/8 w - - 0 1', depth: 6, nodes: 2217 },
  { name: 'stalemate and checkmate #1', fen: '8/k1P5/8/1K6/8/8/8/8 w - - 0 1', depth: 7, nodes: 567584 },
  { name: 'stalemate and checkmate #2', fen: '8/8/2k5/5q2/5n2/8/5K2/8 b - - 0 1', depth: 4, nodes: 23527 },
];

for (const { name, fen, counts } of STANDARD) {
  test(`perft: ${name}`, () => {
    const pos = new Position(fen);
    counts.forEach((expected, i) => assert.equal(pos.perft(i + 1), expected, `depth ${i + 1}`));
    assert.equal(pos.fen(), new Position(fen).fen(), 'perft must leave the position unchanged');
  });
}

for (const { name, fen, depth, nodes } of EDGE_CASES) {
  test(`perft: ${name}`, () => {
    assert.equal(new Position(fen).perft(depth), nodes);
  });
}
