# Engine changelog

Changes that passed an SPRT match and were kept, with their measured
strength gain, newest first. Failed attempts are in `FAILED_EXPERIMENTS.md`.

Every result is from `npm run match -- --sprt 0,10` at 100 ms a move: "H1
accepted" means the change is at least 10 Elo stronger with 95% confidence.
The Elo figure is the match estimate with its 95% range; gains add up only
roughly, since each is measured against the version before it.

| Version / change | Measured against | Games | W-L-D | Elo (95% range) | Commit |
|---|---|---|---|---|---|
| Engine 1.3: king safety | Engine 1.2 | 1151 | 483-389-279 | **+28** (+11 to +46) | `b902214` |
| Engine 1.2: tuned endgame terms | Engine 1.1 | 637 | 267-186-184 | **+44** (+22 to +68) | `5a42226` |
| Engine 1.1: Texel-tuned weights | Engine 1.0 | 2154 | 897-786-471 | **+18** (+5 to +31) | `19aa147` |
| Engine 1.0: baseline | - | - | - | - | `engine-1.0` branch |

Running total over Engine 1.0: about **+90 Elo** (rough, see above).

## External results

Games the owner played against chess.com bots, relaying moves between the
bot and this app. Not a rating, but the only outside measure so far.

| Date | Engine | Thinking time | Opponent | Result | Notes |
|---|---|---|---|---|---|
| 2026-10-15 | 1.0 (app not yet updated) | analysis mode | chess.com 2200 bot | Win | "without much difficulty" |
| 2026-10-15 | 1.2 | analysis mode | chess.com 2600 bot ("Grandmaster", level 22) | Win | chess.com accuracy 95.6 vs the bot's 88.7 |
| 2026-10-15 | 1.2 | 2 s a move | chess.com 2900 bot | Loss | lost the exchange to the repetition bug (24.Rf4, see `HANDOFF.md`), then ground down |
| 2026-10-15 | 1.2 | 10 s a move | chess.com 2900 bot | Loss | no single blunder; queen went pawn-hunting (Qa7, Qa6, Qxa5) and Black's kingside attack crept in: king safety |

## Engine 1.3: king safety

King-safety evaluation terms, Texel-tuned together with all the existing
weights:
- King danger, the classic non-linear model: attack units for enemy pieces
  bearing on the king zone (the king's square, its neighbours and the three
  squares two ranks ahead), by piece type; zone squares hit; weak zone
  squares (attacked, guarded only by our king or queen); safe checks by
  piece type; a discount when the enemy has no queen. Charged units² / 720
  in the middlegame and units / 20 in the endgame, and only with two
  attackers, or one plus an enemy queen. The tuner was extended to fit the
  square directly.
- Pawn storms: the enemy's most advanced pawn on each file around the king,
  by rank, free or blocked.
- Half-open and open files on and next to the king.
- An evaluation cache (same search, about 12% faster), which wins back part
  of the ~26% the attack scan costs.

Held-back tuning error 0.0606 -> 0.0598.

- SPRT vs 1.2 from the varied openings: H1 accepted after 1151 games,
  +483 -389 =279, **+28 Elo** (+11 to +46). The owner accepted this run as
  the final test.
- The same engine plus the repetition fix: +17 Elo (+1 to +32) at 1424
  games, stopped; the fix was dropped (see `FAILED_EXPERIMENTS.md`).

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
