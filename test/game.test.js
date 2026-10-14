import { test } from 'node:test';
import assert from 'node:assert/strict';
import { Game } from '../app/game.js';
import { parseSquare, KNIGHT } from '../src/index.js';

const sq = parseSquare;
const names = (set) => [...set].map((s) => 'abcdefgh'[s & 7] + ((s >> 4) + 1)).sort();

// Plays moves given as "e2-e4" pairs through the same from/to API the board uses.
function playMoves(game, moves) {
  for (const move of moves.split(' ')) {
    const [from, to] = move.split('-');
    assert.equal(game.play(sq(from), sq(to)), 'moved', move);
  }
}

test('only the player to move can pick up pieces', () => {
  const game = new Game();
  assert.ok(game.canMoveFrom(sq('e2')));
  assert.ok(!game.canMoveFrom(sq('e7')), 'black piece on white\'s turn');
  assert.ok(!game.canMoveFrom(sq('e4')), 'empty square');
  game.play(sq('e2'), sq('e4'));
  assert.ok(game.canMoveFrom(sq('e7')));
  assert.ok(!game.canMoveFrom(sq('e4')));
});

test('targets are the legal destination squares', () => {
  const game = new Game();
  assert.deepEqual(names(game.targets(sq('e2'))), ['e3', 'e4']);
  assert.deepEqual(names(game.targets(sq('g1'))), ['f3', 'h3']);
  assert.deepEqual(names(game.targets(sq('a1'))), []);
});

test('illegal moves are refused and change nothing', () => {
  const game = new Game();
  assert.equal(game.play(sq('e2'), sq('e5')), 'illegal');
  assert.equal(game.play(sq('e7'), sq('e5')), 'illegal', 'black cannot move first');
  assert.equal(game.ply, 0);
  assert.equal(game.turn, 'w');
});

test('promotion asks for a piece, then promotes to it', () => {
  const game = new Game();
  playMoves(game, 'h2-h4 g7-g5 h4-g5 g8-f6 g5-g6 f6-e4 g6-g7 e4-f6');
  assert.equal(game.play(sq('g7'), sq('h8')), 'promotion');
  assert.equal(game.ply, 8, 'nothing played until a piece is chosen');
  assert.equal(game.play(sq('g7'), sq('h8'), KNIGHT), 'moved');
  assert.deepEqual(game.pos.pieceAt('h8'), { color: 'w', type: 'n' });
  assert.equal(game.sanHistory.at(-1), 'gxh8=N');
});

test('take back undoes the last move', () => {
  const game = new Game();
  playMoves(game, 'e2-e4 e7-e5');
  assert.ok(game.takeBack());
  assert.equal(game.turn, 'b');
  assert.deepEqual(game.sanHistory, ['e4']);
  assert.ok(game.takeBack());
  assert.ok(!game.takeBack(), 'nothing left to take back');
});

test('scoresheet pairs moves by number', () => {
  const game = new Game();
  playMoves(game, 'e2-e4 e7-e5 g1-f3');
  assert.deepEqual(game.scoresheet(), [
    { number: 1, white: 'e4', black: 'e5' },
    { number: 2, white: 'Nf3', black: '' },
  ]);
});

test('status line: to move, check, checkmate', () => {
  const game = new Game();
  assert.deepEqual(game.describe(), { headline: 'White to move', detail: '', side: 'w', tone: 'normal' });

  playMoves(game, 'e2-e4 f7-f6 d1-h5');
  assert.deepEqual(game.describe(), { headline: 'Black to move', detail: 'Check', side: 'b', tone: 'check' });
  assert.equal(game.checkedKingSquare(), sq('e8'));

  game.reset();
  playMoves(game, 'f2-f3 e7-e5 g2-g4 d8-h4');
  assert.deepEqual(game.describe(), { headline: 'Checkmate', detail: 'Black wins · 0–1', side: 'b', tone: 'over' });
  assert.ok(game.isOver);
  assert.ok(!game.canMoveFrom(sq('a2')), 'no moves after the game ends');
  assert.equal(game.play(sq('a2'), sq('a3')), 'illegal');
});

test('status line: draws', () => {
  const game = new Game();
  playMoves(game, 'g1-f3 g8-f6 f3-g1 f6-g8 g1-f3 g8-f6 f3-g1 f6-g8');
  assert.deepEqual(game.describe(), { headline: 'Draw', detail: 'Threefold repetition · ½–½', side: 'draw', tone: 'over' });
});

test('a saved game restores from its UCI moves', () => {
  const game = new Game();
  playMoves(game, 'e2-e4 e7-e5 g1-f3');
  const restored = Game.fromUci(game.uciHistory());
  assert.equal(restored.pos.fen(), game.pos.fen());
  assert.deepEqual(restored.sanHistory, ['e4', 'e5', 'Nf3']);
  // A corrupted save keeps the legal prefix.
  assert.equal(Game.fromUci(['e2e4', 'e2e4', 'd2d4']).ply, 1);
  assert.equal(Game.fromUci(['e7e8q']).ply, 0);
});
