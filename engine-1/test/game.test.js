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

test('resigning ends the game for the other side; take back resumes it', () => {
  const game = new Game();
  playMoves(game, 'e2-e4 e7-e5');
  assert.ok(game.resign('w'));
  assert.ok(game.isOver);
  assert.deepEqual(game.status, { isOver: true, result: '0-1', reason: 'resignation' });
  assert.deepEqual(game.describe(), { headline: 'White resigns', detail: 'Black wins · 0–1', side: 'b', tone: 'over' });
  assert.ok(!game.canMoveFrom(sq('g1')));
  assert.ok(!game.resign('b'), 'no resigning a finished game');
  assert.ok(game.takeBack());
  assert.ok(!game.isOver);
  assert.equal(game.ply, 2, 'the moves are still there');
});

test('PGN export', () => {
  const game = new Game();
  playMoves(game, 'f2-f3 e7-e5 g2-g4 d8-h4');
  const date = new Date(2026, 9, 14);
  assert.equal(game.pgn({ white: 'You', black: 'Engine', date }), [
    '[Event "Casual game"]',
    '[Site "Chess Engine"]',
    '[Date "2026.10.14"]',
    '[Round "-"]',
    '[White "You"]',
    '[Black "Engine"]',
    '[Result "0-1"]',
    '[Termination "Checkmate"]',
    '',
    '1. f3 e5 2. g4 Qh4# 0-1',
    '',
  ].join('\n'));

  const unfinished = new Game();
  playMoves(unfinished, 'e2-e4');
  assert.match(unfinished.pgn({ date }), /\[Result "\*"\]\n\n1\. e4 \*\n$/);

  // Long games wrap at 80 characters, never splitting a move from its number.
  const long = Game.fromUci(('e2e4 e7e5 g1f3 b8c6 f1b5 a7a6 b5a4 g8f6 e1g1 f8e7 f1e1 b7b5 a4b3 d7d6 '
    + 'c2c3 e8g8 h2h3 c6a5 b3c2 c7c5 d2d4 d8c7').split(' '));
  assert.equal(long.ply, 22);
  const movetext = long.pgn({ date }).split('\n\n')[1].trim().split('\n');
  assert.ok(movetext.length > 1 && movetext.every((l) => l.length <= 80 && !/\d\.$/.test(l)));
});

test('a game can start from a FEN, Black to move', () => {
  const fen = 'r1bqkbnr/pppp1ppp/2n5/4p3/4P3/5N2/PPPP1PPP/RNBQKB1R b KQkq - 3 3';
  const game = new Game(fen);
  assert.equal(game.startFen, fen);
  assert.equal(game.turn, 'b');
  playMoves(game, 'g8-f6 f3-e5');
  assert.deepEqual(game.scoresheet(), [
    { number: 3, white: '…', black: 'Nf6' },
    { number: 4, white: 'Nxe5', black: '' },
  ]);
  const date = new Date(2026, 9, 20);
  const pgn = game.pgn({ date });
  assert.match(pgn, /\[SetUp "1"\]\n\[FEN "r1bqkbnr\/pppp1ppp\/2n5\/4p3\/4P3\/5N2\/PPPP1PPP\/RNBQKB1R b KQkq - 3 3"\]/);
  assert.match(pgn, /\n\n3\.\.\. Nf6 4\. Nxe5 \*\n$/);
  // The exported PGN imports back to the same game.
  const again = Game.fromText(pgn);
  assert.equal(again.pos.fen(), game.pos.fen());
  assert.deepEqual(again.uciHistory(), game.uciHistory());
  // A saved game from a FEN restores; a bad saved FEN falls back to the start.
  assert.equal(Game.fromUci(game.uciHistory(), fen).pos.fen(), game.pos.fen());
  assert.equal(Game.fromUci([], 'nonsense').startFen, new Game().startFen);
  game.reset();
  assert.equal(game.pos.fen(), fen);
  assert.equal(new Game().startFen, 'rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1');
});

test('fromText reads a FEN', () => {
  const game = Game.fromText('  8/8/8/4k3/8/8/4P3/4K3 w - - 0 50\n');
  assert.equal(game.pos.fen(), '8/8/8/4k3/8/8/4P3/4K3 w - - 0 50');
  assert.equal(Game.fromText('8/8/8/4k3/8/8/4P3/4K3 w - -').pos.fen(), '8/8/8/4k3/8/8/4P3/4K3 w - - 0 1');
  assert.throws(() => Game.fromText('8/8/8/8/8/8/8/8 w - - 0 1'), /That FEN can't be read: each side must have exactly one king/);
  assert.throws(() => Game.fromText('   '), /Paste a FEN or a PGN first/);
});

test('fromText reads a PGN, skipping comments, variations and annotations', () => {
  const pgn = `[Event "Casual game"]
[White "Someone"]
[Result "1-0"]

1. e4 {best by test} e5 2.Nf3 (2. f4 exf4 (2... d5) 3. Nf3) Nc6 $1 3. Bb5 a6!? ; the Morphy
4. Ba4 Nf6 5. O-O Be7 6. Re1 b5 7. Bb3 d6 8. c3 O-O 1-0`;
  const game = Game.fromText(pgn);
  assert.equal(game.ply, 16);
  assert.equal(game.sanHistory.slice(0, 6).join(' '), 'e4 e5 Nf3 Nc6 Bb5 a6');
  assert.equal(game.sanHistory.at(-1), 'O-O');
  // Bare movetext works too, and a wrong move says which one.
  assert.equal(Game.fromText('1. d4 d5 2. c4').ply, 3);
  assert.throws(() => Game.fromText('1. e4 e5 2. Ke3'), /Move 2\. Ke3 isn't legal in that position/);
  assert.throws(() => Game.fromText('1. e4 Nf3'), /Move 1… Nf3 isn't legal/);
  assert.throws(() => Game.fromText('[Event "x"]\n\n*'), /No moves found/);
});

test('stepping back and forward through a line, and branching off it', () => {
  const game = Game.fromText('1. e4 e5 2. Nf3 Nc6 3. Bb5 a6');
  const end = game.pos.fen();
  assert.equal(game.lineLength, 6);
  assert.ok(game.back() && game.back());
  assert.equal(game.ply, 4);
  assert.equal(game.lineLength, 6);
  assert.equal(game.scoresheet().at(-1).white, 'Bb5'); // the scoresheet still shows the whole line
  assert.deepEqual(game.lineUci(), ['e2e4', 'e7e5', 'g1f3', 'b8c6', 'f1b5', 'a7a6']);
  game.goTo(0);
  assert.equal(game.ply, 0);
  assert.equal(game.back(), false);
  game.goTo(99);
  assert.equal(game.pos.fen(), end);
  assert.equal(game.forward(), false);
  // Playing the line's next move keeps the rest of it...
  game.goTo(2);
  playMoves(game, 'g1-f3');
  assert.equal(game.lineLength, 6);
  // ...and another move starts a new line.
  playMoves(game, 'd7-d6');
  assert.equal(game.lineLength, 4);
  assert.equal(game.forward(), false);
  assert.deepEqual(game.lineUci(), ['e2e4', 'e7e5', 'g1f3', 'd7d6']);
  // A saved line restores at the same move.
  game.goTo(1);
  const again = Game.fromUci(game.lineUci());
  again.goTo(1);
  assert.equal(again.pos.fen(), game.pos.fen());
  assert.equal(again.lineLength, 4);
});
