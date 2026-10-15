# Engine changelog

Changes that passed an SPRT match and were kept, with their measured
strength gain, newest first. Failed attempts are in `FAILED_EXPERIMENTS.md`.

Every result is from `npm run match -- --sprt 0,10` at 100 ms a move: "H1
accepted" means the change is at least 10 Elo stronger with 95% confidence.
The Elo figure is the match estimate with its 95% range; gains add up only
roughly, since each is measured against the version before it.

| Version / change | Measured against | Games | W-L-D | Elo (95% range) | Commit |
|---|---|---|---|---|---|
| Engine 1.2: tuned endgame terms | Engine 1.1 | 637 | 267-186-184 | **+44** (+22 to +68) | see below |
| Engine 1.1: Texel-tuned weights | Engine 1.0 | 2154 | 897-786-471 | **+18** (+5 to +31) | `19aa147` |
| Engine 1.0: baseline | - | - | - | - | `engine-1.0` branch |

Running total over Engine 1.0: about **+62 Elo** (rough, see above).

## Engine 1.2: tuned endgame terms

New endgame-weighted evaluation terms, all Texel-tuned together with the
existing weights: own and enemy king distance to each passed pawn, blocked
passed pawns (per rank), rook behind its own or the enemy's passed pawn
(Tarrasch's rule), rook on the seventh confining the king or hitting pawns,
and the rule of the square for passers in pawn endings.

- Final test, SPRT vs 1.1 from the opening book: H1 accepted after 637
  games, +267 -186 =184, **+44 Elo** (+22 to +68). This is the version's
  figure in the table.
- Earlier development run of the same engine vs 1.1: H1 accepted after 464
  games, +58 Elo (+32 to +86).
- 400 games from the endgame suite (`--fens test/endgames.epd`): +134 -74
  =192, +53 Elo (+28 to +77), confirming the gain comes from endgame play.
- Tried on top and left out: drawish-ending scaling and an exact KPK table
  (neutral, see `FAILED_EXPERIMENTS.md`).

## Engine 1.1: Texel-tuned weights

All 820 evaluation weights of 1.0 fitted to 1.4 million labelled positions
(Zurichess quiet-labeled.v7); search unchanged. Biggest shifts: pieces worth
more against pawns in the middlegame, passed pawns valued mostly in the
endgame, a missing king-shelter pawn costs more. SPRT needed a 154-game
extension past the 2000-game cap to decide.
