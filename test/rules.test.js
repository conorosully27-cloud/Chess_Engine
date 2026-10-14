import { test, describe } from 'node:test';
import assert from 'node:assert/strict';
import { Position, START_FEN } from '../src/index.js';

// Plays a space-separated list of moves (SAN or UCI) and returns the position.
function playAll(moves, fen = START_FEN) {
  const pos = new Position(fen);
  for (const move of moves.split(/\s+/).filter(Boolean)) pos.play(move);
  return pos;
}

const sanMoves = (pos) => pos.legalMoves().map((m) => pos.toSan(m)).sort();
const uciMoves = (pos) => pos.legalMoves().map((m) => pos.toUci(m)).sort();

describe('FEN', () => {
  test('round-trips', () => {
    for (const fen of [
      START_FEN,
      'r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1',
      'rnbqkbnr/ppp1p1pp/8/3pPp2/8/8/PPPP1PPP/RNBQKBNR w KQkq f6 0 3',
      '8/8/8/4k3/8/8/8/4K3 b - - 37 99',
    ]) {
      assert.equal(new Position(fen).fen(), fen);
    }
  });

  test('missing move counters default to 0 and 1', () => {
    assert.equal(new Position('8/8/8/4k3/8/8/8/4K3 w - -').fen(), '8/8/8/4k3/8/8/8/4K3 w - - 0 1');
  });

  test('rejects malformed or impossible positions', () => {
    const bad = [
      'rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP w KQkq - 0 1', // 7 ranks
      'rnbqkbnr/pppppppp/9/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1', // 9 squares
      'rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBN w KQkq - 0 1', // 7 squares
      'rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNX w KQkq - 0 1', // bad piece
      'rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR x KQkq - 0 1', // bad side
      'rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkz - 0 1', // bad castling
      'rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq e4 0 1', // bad en passant square
      'rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq e6 0 1', // no pawn just moved
      'rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - x 1', // bad counter
      '8/8/8/8/8/8/8/4K3 w - - 0 1', // no black king
      '4k3/8/8/8/8/8/8/3KK3 w - - 0 1', // two white kings
      'P3k3/8/8/8/8/8/8/4K3 w - - 0 1', // pawn on the last rank
      '4k3/8/8/8/8/8/8/4K2r b - - 0 1', // side not to move is in check
    ];
    for (const fen of bad) assert.throws(() => new Position(fen), /Invalid FEN/, fen);
  });

  test('drops castling rights that do not match the board', () => {
    assert.equal(new Position('4k3/8/8/8/8/8/8/4K3 w KQkq - 0 1').fen(), '4k3/8/8/8/8/8/8/4K3 w - - 0 1');
    assert.equal(new Position('r3k3/8/8/8/8/8/8/4K2R w KQkq - 0 1').fen(), 'r3k3/8/8/8/8/8/8/4K2R w Kq - 0 1');
  });
});

describe('basic movement', () => {
  test('20 moves from the start', () => {
    assert.deepEqual(sanMoves(new Position()), [
      'Na3', 'Nc3', 'Nf3', 'Nh3',
      'a3', 'a4', 'b3', 'b4', 'c3', 'c4', 'd3', 'd4', 'e3', 'e4', 'f3', 'f4', 'g3', 'g4', 'h3', 'h4',
    ]);
  });

  test('pinned pieces cannot move off the pin line', () => {
    // The knight on e2 is pinned by the rook on e8; the bishop on d2 is pinned by the bishop on a5.
    const pos = new Position('4r1k1/8/8/b7/8/8/3BN3/4K3 w - - 0 1');
    const moves = uciMoves(pos);
    assert.ok(!moves.some((m) => m.startsWith('e2')), 'pinned knight moved');
    assert.deepEqual(moves.filter((m) => m.startsWith('d2')), ['d2a5', 'd2b4', 'd2c3']);
  });

  test('in check, only moves that resolve the check are legal', () => {
    // Rook on e8 checks the king on e1: move the king, block on e2, or capture is impossible.
    const pos = new Position('4r1k1/8/8/8/8/8/8/2B1K3 w - - 0 1');
    assert.ok(pos.inCheck());
    assert.deepEqual(uciMoves(pos), ['c1e3', 'e1d1', 'e1d2', 'e1f1', 'e1f2']);
  });

  test('kings cannot move next to each other', () => {
    const pos = new Position('8/8/8/3k4/8/3K4/8/8 w - - 0 1');
    assert.deepEqual(uciMoves(pos), ['d3c2', 'd3c3', 'd3d2', 'd3e2', 'd3e3']);
  });
});

describe('castling', () => {
  const OPEN = 'r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1';

  test('both sides available and the rook moves too', () => {
    const pos = new Position(OPEN);
    assert.ok(sanMoves(pos).includes('O-O'));
    assert.ok(sanMoves(pos).includes('O-O-O'));
    pos.play('O-O');
    assert.equal(pos.fen(), 'r3k2r/8/8/8/8/8/8/R4RK1 b kq - 1 1');
    pos.play('O-O-O');
    assert.equal(pos.fen(), '2kr3r/8/8/8/8/8/8/R4RK1 w - - 2 2');
  });

  test('UCI castling notation', () => {
    const pos = new Position(OPEN);
    pos.play('e1c1');
    assert.equal(pos.fen(), 'r3k2r/8/8/8/8/8/8/2KR3R b kq - 1 1');
  });

  test('moving the king or a rook loses the rights', () => {
    assert.equal(playAll('Kf1', OPEN).fen(), 'r3k2r/8/8/8/8/8/8/R4K1R b kq - 1 1');
    assert.equal(playAll('Rb1', OPEN).fen(), 'r3k2r/8/8/8/8/8/8/1R2K2R b Kkq - 1 1');
    // The king returning home doesn't bring the rights back.
    const pos = playAll('Kf1 Kf8 Ke1 Ke8', OPEN);
    assert.equal(pos.fen(), 'r3k2r/8/8/8/8/8/8/R3K2R w - - 4 3');
    assert.ok(!sanMoves(pos).includes('O-O'));
  });

  test('a rook captured on its home square loses the rights', () => {
    assert.equal(playAll('Rxa8+', OPEN).fen(), 'R3k2r/8/8/8/8/8/8/4K2R b Kk - 0 1');
  });

  test('not out of, through, or into check', () => {
    const castles = (fen) => sanMoves(new Position(fen)).filter((m) => m.startsWith('O-O'));
    assert.deepEqual(castles('4k3/4r3/8/8/8/8/8/R3K2R w KQ - 0 1'), []); // in check
    assert.deepEqual(castles('4kr2/8/8/8/8/8/8/R3K2R w KQ - 0 1'), ['O-O-O']); // f1 attacked
    assert.deepEqual(castles('4k1r1/8/8/8/8/8/8/R3K2R w KQ - 0 1'), ['O-O-O']); // g1 attacked
    assert.deepEqual(castles('3rk3/8/8/8/8/8/8/R3K2R w KQ - 0 1'), ['O-O']); // d1 attacked
    assert.deepEqual(castles('2r1k3/8/8/8/8/8/8/R3K2R w KQ - 0 1'), ['O-O']); // c1 attacked
    // b1 being attacked is fine: the king never crosses it.
    assert.deepEqual(castles('1r2k3/8/8/8/8/8/8/R3K2R w KQ - 0 1').sort(), ['O-O', 'O-O-O']);
  });

  test('not through pieces', () => {
    const castles = (fen) => sanMoves(new Position(fen)).filter((m) => m.startsWith('O-O'));
    assert.deepEqual(castles('4k3/8/8/8/8/8/8/RN2K1NR w KQ - 0 1'), []);
    assert.deepEqual(castles('4k3/8/8/8/8/8/8/R2QK2R w KQ - 0 1'), ['O-O']);
  });
});

describe('en passant', () => {
  test('captures the pawn that just moved two squares', () => {
    const pos = playAll('e4 d5 e5 f5');
    assert.equal(pos.fen(), 'rnbqkbnr/ppp1p1pp/8/3pPp2/8/8/PPPP1PPP/RNBQKBNR w KQkq f6 0 3');
    assert.ok(sanMoves(pos).includes('exf6'));
    assert.ok(!sanMoves(pos).includes('exd6'), 'd5 pawn moved one step at a time');
    pos.play('exf6');
    assert.equal(pos.fen(), 'rnbqkbnr/ppp1p1pp/5P2/3p4/8/8/PPPP1PPP/RNBQKBNR b KQkq - 0 3');
    pos.unmakeMove();
    assert.equal(pos.fen(), 'rnbqkbnr/ppp1p1pp/8/3pPp2/8/8/PPPP1PPP/RNBQKBNR w KQkq f6 0 3');
  });

  test('only on the very next move', () => {
    const pos = playAll('e4 d5 e5 f5 Nf3 Nc6');
    assert.ok(!sanMoves(pos).includes('exf6'));
  });

  test('no en passant square in the FEN when no capture is possible', () => {
    assert.equal(playAll('e4').fen(), 'rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq - 0 1');
  });

  test('not allowed when it exposes the king along the rank', () => {
    // After ...c5, bxc6 would remove both pawns from the 5th rank and expose
    // the white king on a5 to the rook on h5.
    const pinned = playAll('c5', '4k3/2p5/8/KP5r/8/8/8/8 b - - 0 1');
    assert.equal(pinned.fen(), '4k3/8/8/KPp4r/8/8/8/8 w - - 0 2');
    assert.ok(!uciMoves(pinned).includes('b5c6'));

    const free = playAll('c5', '4k3/2p5/8/1P5r/8/8/8/K7 b - - 0 1');
    assert.equal(free.fen(), '4k3/8/8/1Pp4r/8/8/8/K7 w - c6 0 2');
    assert.ok(uciMoves(free).includes('b5c6'));
  });

  test('can capture a pawn that gives check', () => {
    // d2-d4 checks the king on e5; ...cxd3 removes the checking pawn.
    const pos = playAll('d4', '8/8/8/4k3/2p5/8/3P4/4K3 w - - 0 1');
    assert.ok(pos.inCheck());
    assert.ok(sanMoves(pos).includes('cxd3'));
  });
});

describe('promotion', () => {
  test('to any of four pieces, with or without capture', () => {
    const pos = new Position('1r2k3/P7/8/8/8/8/8/4K3 w - - 0 1');
    const promotions = sanMoves(pos).filter((m) => m.includes('='));
    assert.deepEqual(promotions, [
      'a8=B', 'a8=N', 'a8=Q', 'a8=R', 'axb8=B', 'axb8=N', 'axb8=Q+', 'axb8=R+',
    ]);
  });

  test('places the chosen piece and takes back cleanly', () => {
    const fen = '1r2k3/P7/8/8/8/8/8/4K3 w - - 0 1';
    const pos = new Position(fen);
    pos.play('axb8=N');
    assert.deepEqual(pos.pieceAt('b8'), { color: 'w', type: 'n' });
    assert.equal(pos.pieceAt('a7'), null);
    pos.unmakeMove();
    assert.equal(pos.fen(), fen);
  });

  test('black promotes on the first rank', () => {
    const pos = playAll('e1=Q', '4k3/8/8/8/8/8/4p3/K7 b - - 0 1');
    assert.deepEqual(pos.pieceAt('e1'), { color: 'b', type: 'q' });
    assert.ok(pos.inCheck());
  });
});

describe('game end', () => {
  test('checkmate: fool\'s mate', () => {
    const pos = playAll('f3 e5 g4');
    assert.equal(pos.toSan(pos.parseMove('Qh4')), 'Qh4#');
    pos.play('Qh4');
    assert.ok(pos.isCheckmate());
    assert.deepEqual(pos.status(), { isOver: true, result: '0-1', reason: 'checkmate' });
  });

  test('checkmate: scholar\'s mate', () => {
    const pos = playAll('e4 e5 Bc4 Nc6 Qh5 Nf6 Qxf7');
    assert.deepEqual(pos.status(), { isOver: true, result: '1-0', reason: 'checkmate' });
  });

  test('check is not checkmate', () => {
    const pos = playAll('e4 f6 Qh5');
    assert.ok(pos.inCheck());
    assert.ok(!pos.isCheckmate());
    assert.equal(pos.status().isOver, false);
  });

  test('stalemate', () => {
    for (const fen of ['7k/5Q2/6K1/8/8/8/8/8 b - - 0 1', 'k7/8/1Q6/8/8/8/8/7K b - - 0 1']) {
      const pos = new Position(fen);
      assert.ok(pos.isStalemate());
      assert.ok(!pos.isCheckmate());
      assert.deepEqual(pos.status(), { isOver: true, result: '1/2-1/2', reason: 'stalemate' });
    }
  });

  test('insufficient material', () => {
    const cases = {
      '8/8/8/4k3/8/8/8/4K3 w - - 0 1': true, // K v K
      '8/8/8/4k3/8/8/8/2B1K3 w - - 0 1': true, // KB v K
      '8/8/8/4k3/8/8/8/1N2K3 w - - 0 1': true, // KN v K
      '5b2/8/8/4k3/8/8/8/2B1K3 w - - 0 1': true, // KB v KB, same-colored bishops
      '2b5/8/8/4k3/8/8/8/2B1K3 w - - 0 1': false, // opposite-colored bishops
      '8/8/8/4k3/8/8/8/1N2K1N1 w - - 0 1': false, // KNN v K (mate is possible)
      '1n6/8/8/4k3/8/8/8/1N2K3 w - - 0 1': false, // KN v KN
      '1n6/8/8/4k3/8/8/8/2B1K3 w - - 0 1': false, // KB v KN
      '8/8/8/4k3/8/8/4P3/4K3 w - - 0 1': false, // a pawn
      '8/8/8/4k3/8/8/8/R3K3 w - - 0 1': false, // a rook
    };
    for (const [fen, expected] of Object.entries(cases)) {
      assert.equal(new Position(fen).isInsufficientMaterial(), expected, fen);
    }
    assert.deepEqual(new Position('8/8/8/4k3/8/8/8/2B1K3 w - - 0 1').status(), {
      isOver: true, result: '1/2-1/2', reason: 'insufficient material',
    });
  });

  test('a capture leading to bare kings ends the game', () => {
    const pos = playAll('Kxd4', '8/8/8/8/3n4/4K3/8/7k w - - 0 1');
    assert.equal(pos.status().reason, 'insufficient material');
  });

  test('fifty-move rule', () => {
    const pos = new Position('8/8/8/4k3/8/8/8/R3K3 w - - 99 60');
    assert.equal(pos.status().isOver, false);
    pos.play('Ra2');
    assert.ok(pos.isFiftyMoveRule());
    assert.deepEqual(pos.status(), { isOver: true, result: '1/2-1/2', reason: 'fifty-move rule' });
  });

  test('a pawn move or capture resets the fifty-move count', () => {
    assert.equal(playAll('e3', '4k3/8/8/8/8/8/4P3/4K3 w - - 98 60').halfmoveClock, 0);
    assert.equal(playAll('Rxa7', 'n3k3/r7/8/8/8/8/8/R3K3 w - - 98 60').halfmoveClock, 0);
    assert.equal(playAll('Ra2', 'n3k3/r7/8/8/8/8/8/R3K3 w - - 98 60').halfmoveClock, 99);
  });

  test('checkmate on the 100th half-move beats the fifty-move rule', () => {
    const pos = playAll('Ra8', '7k/8/6K1/8/8/8/8/R7 w - - 99 80');
    assert.equal(pos.halfmoveClock, 100);
    assert.deepEqual(pos.status(), { isOver: true, result: '1-0', reason: 'checkmate' });
  });

  test('threefold repetition', () => {
    const pos = playAll('Nf3 Nf6 Ng1 Ng8');
    assert.equal(pos.repetitionCount(), 2);
    assert.equal(pos.status().isOver, false);
    pos.play('Nf3');
    pos.play('Nf6');
    pos.play('Ng1');
    pos.play('Ng8');
    assert.equal(pos.repetitionCount(), 3);
    assert.deepEqual(pos.status(), { isOver: true, result: '1/2-1/2', reason: 'threefold repetition' });
    pos.unmakeMove();
    assert.equal(pos.status().isOver, false);
  });

  test('positions with different castling rights are not repetitions', () => {
    // The starting setup recurs on the board, but the castling rights have gone.
    const pos = playAll('Kf1 Kf8 Ke1 Ke8 Kf1 Kf8 Ke1 Ke8', 'r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1');
    assert.equal(pos.repetitionCount(), 2);
    assert.equal(pos.status().isOver, false);
  });

  test('positions with different side to move are not repetitions', () => {
    // A rook triangle returns the pieces to the same squares with the other side to move.
    const pos = playAll('Ra2 Kd7 Rb2 Ke8 Rb1 Kd7 Ra1 Ke8', '4k3/8/8/8/8/8/8/R3K3 w - - 0 1');
    assert.equal(pos.fen(), '4k3/8/8/8/8/8/8/R3K3 w - - 8 5');
    assert.equal(pos.repetitionCount(), 2);
  });
});

describe('SAN', () => {
  test('disambiguates by file, rank, or both', () => {
    const knights = new Position('4k3/8/8/8/8/8/8/1N2KN2 w - - 0 1');
    assert.ok(sanMoves(knights).includes('Nbd2'));
    assert.ok(sanMoves(knights).includes('Nfd2'));

    const rooks = new Position('4k3/8/8/R7/8/8/8/R3K3 w - - 0 1');
    assert.ok(sanMoves(rooks).includes('R1a3'));
    assert.ok(sanMoves(rooks).includes('R5a3'));

    const queens = new Position('4k3/8/8/8/8/Q7/8/Q1Q1K3 w - - 0 1');
    const toB2 = sanMoves(queens).filter((m) => m.endsWith('b2'));
    assert.deepEqual(toB2, ['Q3b2', 'Qa1b2', 'Qcb2']);
  });

  test('does not disambiguate when the other piece is pinned', () => {
    // Both knights attack c3, but the e2 knight is pinned by the rook on e8.
    const pos = new Position('4r1k1/8/8/8/8/8/4N3/1N2K3 w - - 0 1');
    assert.ok(sanMoves(pos).includes('Nc3'));
    assert.ok(!sanMoves(pos).includes('Nbc3'));
  });

  test('marks check and mate', () => {
    const pos = playAll('e4 f6');
    assert.equal(pos.toSan(pos.parseMove('Qh5')), 'Qh5+');
    assert.ok(sanMoves(playAll('f3 e5 g4')).includes('Qh4#'));
  });

  test('parses common variations', () => {
    const open = 'r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1';
    for (const text of ['O-O', '0-0', 'O-O+', 'e1g1']) {
      assert.equal(new Position(open).toSan(new Position(open).parseMove(text)), 'O-O', text);
    }
    const promo = '4k3/1P6/8/8/8/8/8/4K3 w - - 0 1';
    for (const text of ['b8=Q', 'b8Q', 'b8=Q+', 'b7b8q', 'b8=q']) {
      const pos = new Position(promo);
      assert.equal(pos.toSan(pos.parseMove(text)), 'b8=Q+', text);
    }
    const start = new Position();
    for (const text of ['Nf3', 'Ngf3', 'Ng1f3', 'g1f3', 'Nf3!?']) {
      assert.equal(start.toSan(start.parseMove(text)), 'Nf3', text);
    }
  });

  test('rejects illegal and ambiguous moves', () => {
    const start = new Position();
    for (const text of ['e5', 'Nf4', 'Ke2', 'O-O', 'e2e5', 'nonsense', '']) {
      assert.equal(start.parseMove(text), null, text);
    }
    assert.equal(new Position('4k3/8/8/8/8/8/8/1N2KN2 w - - 0 1').parseMove('Nd2'), null);
    assert.throws(() => start.play('e5'), /Illegal/);
  });
});

describe('make / unmake', () => {
  // Deterministic PRNG so failures are reproducible.
  function rng(seed) {
    return () => {
      seed = (seed * 1103515245 + 12345) >>> 0;
      return seed / 2 ** 32;
    };
  }

  test('random games keep the hash consistent and unmake restores everything', () => {
    const random = rng(42);
    const starts = [
      START_FEN,
      'r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1',
      'rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8',
    ];
    for (let game = 0; game < 300; game++) {
      const startFen = starts[game % starts.length];
      const pos = new Position(startFen);
      const fens = [pos.fen()];
      const keys = [pos.hashKey()];
      while (!pos.status().isOver && pos.ply < 300) {
        const moves = pos.legalMoves();
        pos.makeMove(moves[Math.floor(random() * moves.length)]);
        // The incrementally updated hash must equal one computed from scratch.
        assert.equal(pos.hashKey(), new Position(pos.fen()).hashKey(), pos.fen());
        fens.push(pos.fen());
        keys.push(pos.hashKey());
      }
      while (pos.ply > 0) {
        pos.unmakeMove();
        assert.equal(pos.fen(), fens[pos.ply]);
        assert.equal(pos.hashKey(), keys[pos.ply]);
      }
    }
  });

  test('history lists the moves played', () => {
    const pos = playAll('e4 e5 Nf3');
    assert.deepEqual(pos.history().map((m) => pos.toUci(m)), ['e2e4', 'e7e5', 'g1f3']);
  });

  test('cannot take back past the loaded position', () => {
    assert.throws(() => new Position().unmakeMove(), /No move/);
  });
});
