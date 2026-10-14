# Chess engine

A chess engine in plain JavaScript (ES modules, no dependencies), so it can run
directly in a phone's browser. The plan:

1. **Rules of chess** ✅ (this step)
2. Engine: evaluation and alpha-beta search
3. Mobile web app: a touch board, with the engine in a Web Worker, installable
   to the home screen and playable offline

## Usage

```js
import { Position } from './src/index.js';

const pos = new Position();          // or new Position(fen)
pos.play('e4');                      // SAN or UCI ("e2e4") both work
pos.play('e7e5');

const moves = pos.legalMoves();      // moves are integers
moves.map((m) => pos.toSan(m));      // ['Na3', 'Nc3', ..., 'Qh5', ...]

pos.fen();                           // 'rnbqkbnr/pppp1ppp/8/4p3/4P3/8/PPPP1PPP/RNBQKBNR w KQkq - 0 2'
pos.unmakeMove();                    // take back
pos.status();                        // { isOver: false, result: '*', reason: null }
pos.pieceAt('e1');                   // { color: 'w', type: 'k' }
```

`status()` reports checkmate, stalemate, insufficient material, the fifty-move
rule and threefold repetition. The last two end the game automatically, as in
most chess apps.

## What's implemented

- All piece moves, captures, check, pins
- Castling, with every condition (rights, empty squares, not out of, through
  or into check)
- En passant, including the rare case where it's illegal because it exposes
  the king along a rank
- Promotion to queen, rook, bishop or knight
- Checkmate, stalemate, insufficient material, fifty-move rule, threefold
  repetition
- FEN import/export with validation; SAN and UCI move notation
- Zobrist hashing (used for repetition now, and the engine's transposition
  table later)

## Layout

| File | Contents |
| --- | --- |
| `src/board.js` | Piece/square constants, 0x88 board helpers, move encoding |
| `src/position.js` | `Position`: board state, FEN, move generation, make/unmake, game end |
| `src/notation.js` | SAN and UCI conversion |
| `src/zobrist.js` | Hash keys |

## Testing

```sh
npm test                    # ~5 seconds
npm run perft -- 5          # perft breakdown and speed from the start position
npm run perft -- 4 <fen>    # ... or from any position
```

The tests include **perft**: counting every position reachable to a fixed depth
and matching published counts exactly. It covers the 6 standard reference
positions and 14 edge-case positions (illegal en passant, castling through check,
promotions with check, self-stalemate and more), about 30 million positions in all.
Unit tests cover each rule directly, along with a fuzz test of make/unmake
across random games.
