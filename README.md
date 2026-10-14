# Chess engine

A chess engine in plain JavaScript (ES modules, no dependencies), so it can run
directly in a phone's browser. The plan:

1. **Rules of chess** ✅
2. **Playable board** ✅ for two players on one device
3. Engine: evaluation and alpha-beta search, running in a Web Worker
4. Installable to the home screen and playable offline

## Play

```sh
npm start
```

Then open http://localhost:8000. To play on your phone, open the
"On your phone" address it prints while your phone is on the same Wi-Fi.
(Browsers won't load the app straight from the file system, so it needs this
small server.)

- **Drag** a piece onto a square, or **tap** a piece and then tap a square.
  Legal squares are marked with a small blue square (corner brackets for
  captures), and only the player whose turn it is can pick pieces up.
- Pawns reaching the last rank ask which piece to promote to.
- **Take back** undoes a move, **Flip board** turns the board around, and
  **New game** starts over (mid-game it asks you to tap twice).
- The game is saved in the browser, so it survives a reload.

### Board and pieces

Tap **Style** to choose a piece set and a board; your choice is remembered.

| Pieces | |
| --- | --- |
| Ink | Printed-book style: White in outline, Black in solid ink |
| Soft | Rounded, turned-looking pieces with soft shading |
| Tokens | Board-game counters stamped with the piece |
| Bauhaus | Geometric blocks after Josef Hartwig's 1924 Bauhaus set, where each shape shows how the piece moves |
| Pixel | 8-bit sprites |

| Boards | |
| --- | --- |
| Walnut | Maple and walnut, with wood grain |
| Stone | Warm greys |
| Lagoon | Teal and sea-glass |
| Coral | Coral and shell |
| Midnight | Navy and periwinkle |

All the piece sets are original artwork, drawn by `scripts/build-pieces.js`
from one file per set in `scripts/pieces/`; after editing one, run
`npm run pieces`. Board colors live in `app/style.css` under
`[data-board="..."]`. To add either, add it there and to the lists at the top
of `app/ui.js`.

## Using the rules library

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
| `index.html`, `app/style.css` | The page |
| `app/ui.js` | Board rendering and drag/tap input |
| `app/game.js` | Game session behind the board (no DOM code, so it's unit-tested) |
| `app/pieces/` | Piece images, one folder per set, generated from `scripts/pieces/` |

## Testing

```sh
npm test                    # ~6 seconds
npm run perft -- 5          # perft breakdown and speed from the start position
npm run perft -- 4 <fen>    # ... or from any position
```

The tests include **perft**: counting every position reachable to a fixed depth
and matching published counts exactly. It covers the 6 standard reference
positions and 14 edge-case positions (illegal en passant, castling through check,
promotions with check, self-stalemate and more), about 30 million positions in all.
Unit tests cover each rule directly, along with a fuzz test of make/unmake
across random games.
