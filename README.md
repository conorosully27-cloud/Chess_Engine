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

**Current version: Engine 1.6.** Engine 1.0 is kept as-is on the
`engine-1.0` branch; to measure a new version against an older one, check
that out with `git worktree add`, e.g. `git worktree add ../engine-1.0
engine-1.0`. Every version's measured gain is in `ENGINE_CHANGELOG.md`.

*Changes in 1.6 over 1.5:* better move ordering. When a quiet move refutes
a line, the quiet moves tried before it there count against themselves in
the engine's move history, so moves that keep failing are tried later
(11% fewer positions searched). Estimated **+8 Elo** over 1.5 (95% -5 to
+21) after 2000 games at 100 ms a move; too small to prove, released as a
likely small gain.

*Changes in 1.5 over 1.4:* late move pruning. Near the end of the search,
once the first few quiet moves have been tried (4, 7 or 12 at the last 1,
2 or 3 plies), the rest are skipped, except moves that give check: moves
ordered that late almost never matter there. Estimated **+73 Elo** over
1.4 (95% +43 to +104), SPRT H1 accepted after 353 games at 100 ms a move.

*Changes in 1.4 over 1.3:* speed. Positions keep running totals of the
material and piece-square scores as moves are made, a cheaper test for
moves that leave the king in check, lazy evaluation (in the capture search,
when material alone is far outside the window, the full evaluation is
skipped), and the capture search skips captures of cheaper pieces that a
pawn guards. About 20-25% faster to the same depth. Estimated **+46 Elo**
over 1.3 (95% +23 to +70), SPRT H1 accepted after 572 games at 100 ms a
move.

*Changes in 1.3 over 1.2:* king safety, Texel-tuned together with all the
other weights. The main term is "king danger": attack units for enemy
pieces bearing on the squares around the king (by piece type), weak
squares there, and safe checks the enemy has, charged as units² in the
middlegame, so one attacker is a nuisance but three are a crisis. Also
pawn storms against the king and open files next to it, and a cache of
evaluations that makes the search faster without changing it. Estimated
**+28 Elo** over 1.2 (95% +11 to +46), SPRT H1 accepted after 1151 games
at 100 ms a move.

*Changes in 1.2 over 1.1:* new endgame evaluation terms, Texel-tuned
together with all the other weights: own and enemy king distance to each
passed pawn, blocked passed pawns, rooks behind passed pawns (ours and the
enemy's, Tarrasch's rule), a rook on the seventh rank confining the king or
hitting pawns, and the rule of the square for passers in pawn endings. The
search is unchanged. Estimated **+44 Elo** over 1.1 (95% +22 to +68), SPRT
H1 accepted after 637 games at 100 ms a move.

*Changes in 1.1 over 1.0:* all evaluation weights (material, piece-square
tables, pawn structure, mobility, rook files, king shelter, tempo) were
Texel-tuned on 1.4 million positions from the Zurichess "quiet-labeled"
set; the search is unchanged. Estimated **+18 Elo** over 1.0 (95% +5 to
+31), SPRT H1 accepted after 2154 games at 100 ms a move. Notable shifts:
pieces are worth more against pawns in the middlegame, passed pawns count
mostly in the endgame, and a missing shelter pawn costs more.

#### Comparing engine versions

`npm run match` plays two checkouts of this repository against each other:
paired openings (each played with both colors), equal thinking time, games in
parallel. With `--sprt` it runs a sequential probability ratio test, the
method Stockfish uses to accept changes: it stops as soon as the results
decide, with 5% error rates.

```sh
git worktree add ../engine-1.6 1576297                    # once: the Engine 1.6 commit
npm run match -- --b ../engine-1.6 --fens test/openings.epd --sprt 0,10 --games 2000
npm run match -- --b ../engine-1.6 --games 200 --time 100    # fixed-length match, book openings
npm run match -- --b ../engine-1.6 --fens test/endgames.epd --games 400   # endgames only
```

`--fens test/openings.epd` starts each game pair from one of 1000 different
positions (3 random moves each, made by `scripts/make-openings.js`), so long
matches don't repeat openings; without it the match cycles through the 24
book openings.

"H1 accepted" means the new version is better; "H0 accepted" means it isn't
better by the margin tested. Small gains need many games (about ±45 Elo of
uncertainty at 200 games), so use SPRT rather than reading too much into a
short match. `npm run match -- --help` lists all options.

Known weaknesses: it is often over-optimistic (about +1 in balanced
openings), and the search treats any position seen once before in the game
as a draw (a fix lost Elo in self-play; see `FAILED_EXPERIMENTS.md`).
Results against chess.com bots (with 1.2) are in `ENGINE_CHANGELOG.md`.

- **Analysis**: switch it on to see the evaluation (from White's side, in
  pawns; `#3` means mate in 3), the engine's main line, and an arrow for its
  best move. It analyses the position on the board for up to 30 seconds.
- **Play vs engine**: New game → Engine, choose your color and how long it
  thinks per move (2-20 s). It plays at full strength, with a small opening
  book for variety. Take back undoes your move and its reply.

How it works (`src/engine/`): a tapered evaluation (material, piece-square
tables, mobility, pawn structure, passed pawns, rooks on open files, king
shelter, bishop pair, and endgame terms for passed pawns, rooks behind
passers, the seventh rank and the rule of the square, and king safety: king
danger, pawn storms and open files at the king), with every weight in
`weights.js` Texel-tuned by `scripts/tune.js`, and an alpha-beta search with iterative deepening,
quiescence search, a transposition table, null-move pruning, late move
reductions, late move pruning and killer/history move ordering. It runs in a Web Worker so the
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

*For whoever picks this up next (including a new Claude session).* Read
**`HANDOFF.md`** first: it has the current state, the working process, the
prioritised next steps (starting with a known repetition bug) and the
owner's preferences. Accepted changes and their Elo gains are in
`ENGINE_CHANGELOG.md`; changes that didn't gain strength, with commentary,
are in `FAILED_EXPERIMENTS.md`.

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
| `src/engine/` | Evaluation (`evaluate.js`, weights in `weights.js`), search, opening book |
| `scripts/match.js`, `scripts/sprt.js` | Engine-vs-engine matches and the SPRT |
| `scripts/tune.js`, `scripts/eval-trace.js` | Texel tuner and the evaluation as a linear model |
| `scripts/make-openings.js` | Generates `test/openings.epd` |
| `test/openings.epd`, `test/endgames.epd` | Match starting positions: 1000 openings, 270 endgames |
| `HANDOFF.md`, `ENGINE_CHANGELOG.md`, `FAILED_EXPERIMENTS.md` | Status and next steps, accepted changes, rejected ones |
| `app/pieces/` | Piece images, one folder per set, generated from `scripts/pieces/` |

## Testing

```sh
npm test                    # ~7 seconds, 105 tests
npm run perft -- 5          # perft breakdown and speed from the start position
npm run perft -- 4 <fen>    # ... or from any position
npm run match -- --b ../engine-1.6 --fens test/openings.epd --sprt 0,10   # engine vs engine (see Engine)
npm run tune -- --data quiet-labeled.v7.epd.gz     # Texel tuning (see HANDOFF.md)
```

The tests include **perft**: counting every position reachable to a fixed depth
and matching published counts exactly. It covers the 6 standard reference
positions and 14 edge-case positions (illegal en passant, castling through check,
promotions with check, self-stalemate and more), about 30 million positions in all.
Unit tests cover each rule directly, along with a fuzz test of make/unmake
across random games.

### Cute Chess / other UCI GUIs

`uci.js` runs the engine over the UCI protocol (needs Node.js on your PATH).
In Cute Chess: Tools → Settings → Engines → **+**, set Command to the full
path of `engine.bat`, Working directory to this folder, Protocol to `UCI`.
