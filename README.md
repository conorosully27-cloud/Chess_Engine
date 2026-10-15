# Chess engine

A chess engine in plain JavaScript (ES modules, no dependencies), so it can run
directly in a phone's browser. The plan:

1. **Rules of chess** ✅
2. **Playable board** ✅ for two players on one device
3. **Engine** ✅ full strength: analysis mode and play vs engine
4. Weaker levels, once the full-strength engine is right
5. Installable to the home screen and playable offline

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
  **New game** starts a game against a friend or the engine.
- **Resign** (tap twice to confirm) ends the game; Take back withdraws it.
- **Export PGN** shows the game in PGN, the standard format, with a Copy
  button, ready to paste into Lichess, Chess.com or any chess app.
- The game is saved in the browser, so it survives a reload.

### Engine

**Current version: Engine 1.0**, kept as-is on the `engine-1.0` branch. To
build on it or measure a new version against it, check that branch out, e.g.
`git worktree add ../engine-1.0 engine-1.0`.

#### Comparing engine versions

`npm run match` plays two checkouts of this repository against each other:
paired openings (each played with both colors), equal thinking time, games in
parallel. With `--sprt` it runs a sequential probability ratio test, the
method Stockfish uses to accept changes: it stops as soon as the results
decide, with 5% error rates.

```sh
git worktree add ../engine-1.0 engine-1.0                 # once
npm run match -- --b ../engine-1.0 --sprt 0,10 --games 2000  # is this checkout ≥10 Elo better?
npm run match -- --b ../engine-1.0 --games 200 --time 100    # fixed-length match
```

"H1 accepted" means the new version is better; "H0 accepted" means it isn't
better by the margin tested. Small gains need many games (about ±45 Elo of
uncertainty at 200 games), so use SPRT rather than reading too much into a
short match. `npm run match -- --help` lists all options.

Known weaknesses of 1.0, from a review of a game against it: the evaluation
is materialistic and undervalues development, space and initiative. King
shelter also penalises central pawn moves in front of an uncastled king, and
there is no development or space term and little king safety. In gambit
positions it judged White about 1.5 pawns worse than a reference engine.

- **Analysis**: switch it on to see the evaluation (from White's side, in
  pawns; `#3` means mate in 3), the engine's main line, and an arrow for its
  best move. It analyses the position on the board for up to 30 seconds.
- **Play vs engine**: New game → Engine, choose your color and how long it
  thinks per move (2-20 s). It plays at full strength, with a small opening
  book for variety. Take back undoes your move and its reply.

How it works (`src/engine/`): a tapered evaluation (material, piece-square
tables, mobility, pawn structure, passed pawns, rooks on open files, king
shelter, bishop pair) and an alpha-beta search with iterative deepening,
quiescence search, a transposition table, null-move pruning, late move
reductions and killer/history move ordering. It runs in a Web Worker so the
board stays responsive.

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

## Handover: status and next steps

*For whoever picks this up next (including a new Claude session). Read this
first.*

**Status (Oct 2026).** Rules, board UI, style picker, analysis, play vs
engine, resign and PGN export are done and tested (`npm test`, 96 tests).
The engine is **Engine 1.0**, frozen on the `engine-1.0` branch. New work
goes on `main` (or a branch such as `engine-1.1`).

**Next: improve the evaluation** (`src/engine/evaluate.js`). In a gambit game
1.0 judged White about 1.5 pawns worse than a reference engine. Fix in this
order:
1. King shelter: only score it for a castled king (now it penalises central
   pawn moves in front of a king on e1/e8); add a penalty for a king left in
   the centre while queens are on.
2. Development: penalise minor pieces on their starting squares and early
   queen moves, scaled to the opening phase.
3. Space/centre, and pieces attackable by enemy pawns.
4. Later: tune the weights on a large set of positions (Texel tuning).

**How to test a change:**
1. Run `npm test`. The evaluation must stay colour-symmetric; a test checks it.
2. Run `git worktree add ../engine-1.0 engine-1.0` once.
3. Run `npm run match -- --b ../engine-1.0 --sprt 0,10 --games 2000`.
   Accept the change only on "H1 accepted". The book has 24 openings (48
   games before they repeat); a bigger opening set would help long matches.

**Notes:**
- Changes that didn't gain strength are logged, with commentary, in
  `FAILED_EXPERIMENTS.md`. Check it before retrying an idea.
- Earlier trials: a capture-exchange (SEE) pruning attempt showed no gain and
  was reverted; mobility was kept.
- The phone build is a claude.ai artifact made from `index.html` (with the
  page skeleton stripped) plus `app/` and `src/`; the artifact sandbox blocks
  downloads, hence Copy PGN.
- The owner is a strong player who tests by playing. Ask before starting the
  weaker play levels (planned only once full strength is satisfactory).

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
| `app/engine-worker.js`, `app/engine-client.js` | Runs the engine in a background thread |
| `src/engine/` | Evaluation, search, opening book |
| `app/pieces/` | Piece images, one folder per set, generated from `scripts/pieces/` |

## Testing

```sh
npm test                    # ~6 seconds
npm run perft -- 5          # perft breakdown and speed from the start position
npm run perft -- 4 <fen>    # ... or from any position
npm run match -- --b ../engine-1.0 --sprt 0,10   # engine vs engine (see Engine)
```

The tests include **perft**: counting every position reachable to a fixed depth
and matching published counts exactly. It covers the 6 standard reference
positions and 14 edge-case positions (illegal en passant, castling through check,
promotions with check, self-stalemate and more), about 30 million positions in all.
Unit tests cover each rule directly, along with a fuzz test of make/unmake
across random games.
