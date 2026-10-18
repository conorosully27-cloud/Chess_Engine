# Engine changelog

Changes that passed an SPRT match and were kept, with their measured
strength gain, newest first. Failed attempts are in `FAILED_EXPERIMENTS.md`.

Every result is from `npm run match -- --sprt 0,10` at 100 ms a move: "H1
accepted" means the change is at least 10 Elo stronger with 95% confidence.
The Elo figure is the match estimate with its 95% range; gains add up only
roughly, since each is measured against the version before it.

| Version / change | Measured against | Games | W-L-D | Elo (95% range) | Commit |
|---|---|---|---|---|---|
| Engine 1.6: history penalty | Engine 1.5 | 2000 | 708-662-630 | +8 (-5 to +21), not significant | `1576297` |
| Engine 1.5: late move pruning | Engine 1.4 | 353 | 161-88-104 | **+73** (+43 to +104) | `648648e` |
| Engine 1.4: speed | Engine 1.3 | 572 | 230-154-188 | **+46** (+23 to +70) | `9d16dc3` |
| Engine 1.3: king safety | Engine 1.2 | 1151 | 483-389-279 | **+28** (+11 to +46) | `b902214` |
| Engine 1.2: tuned endgame terms | Engine 1.1 | 637 | 267-186-184 | **+44** (+22 to +68) | `5a42226` |
| Engine 1.1: Texel-tuned weights | Engine 1.0 | 2154 | 897-786-471 | **+18** (+5 to +31) | `19aa147` |
| Engine 1.0: baseline | - | - | - | - | `engine-1.0` branch |

Running total over Engine 1.0: about **+217 Elo** (rough, see above).

## External results

Games the owner played against chess.com bots, relaying moves between the
bot and this app. Not a rating, but the only outside measure so far.

| Date | Engine | Thinking time | Opponent | Result | Notes |
|---|---|---|---|---|---|
| 2026-10-15 | 1.0 (app not yet updated) | analysis mode | chess.com 2200 bot | Win | "without much difficulty" |
| 2026-10-15 | 1.2 | analysis mode | chess.com 2600 bot ("Grandmaster", level 22) | Win | chess.com accuracy 95.6 vs the bot's 88.7 |
| 2026-10-15 | 1.2 | 2 s a move | chess.com 2900 bot | Loss | lost the exchange to the repetition bug (24.Rf4, see `HANDOFF.md`), then ground down |
| 2026-10-15 | 1.2 | 10 s a move | chess.com 2900 bot | Loss | no single blunder; queen went pawn-hunting (Qa7, Qa6, Qxa5) and Black's kingside attack crept in: king safety |

## Engine 1.6: history penalty (move ordering)

When a quiet move causes a cut-off, the quiet moves searched before it at
that node lose the history bonus it gains (depth squared), so moves that
keep failing sink in the ordering. Over 600 searches to depth 8: 11% fewer
nodes, first-move cut-offs 81.8% -> 82.9%.

- SPRT vs 1.5: 2000 games, +708 -662 =630, **+8 Elo** (-5 to +21), LLR
  0.72, inconclusive at the game limit. Released on the owner's call as a
  likely small gain, to close 1.6 before the NNUE work (Engine 2.0).
- Tried with it and left out: a lazy margin of 2 pawns plus the bad-capture
  skip for any guard (+4 together, see `FAILED_EXPERIMENTS.md`); the
  countermove heuristic (worse ordering); steeper late move reductions
  (parked). A Texel retune found nothing to gain (held-back error
  0.059835 -> 0.059832).

## Engine 1.5: late move pruning

In the last 3 plies of the search, off the main line and not in check,
once 4, 7 or 12 quiet moves have been tried (at depth 1, 2, 3), the
remaining quiet moves that don't give check are skipped. About half the
nodes to depth 9 on the benchmark (3.16M -> 1.48M).

- SPRT vs 1.4 (one game at a time): H1 accepted after 353 games,
  +161 -88 =104, **+73 Elo** (+43 to +104). The owner accepted this run
  as the final test.
- Logarithmic late move reductions were tested alongside and left out
  (neutral, see `FAILED_EXPERIMENTS.md`).
- For fun: a 1000-game match of 1.4 vs 1.0 was cut short by a machine
  restart at 527 games: +349 -95 =83, Elo +183 (+154 to +214).

## Engine 1.4: speed

Faster search, plus two changes that let it skip work:
- Positions keep running sums of material + piece-square scores and game
  phase, updated in make/unmake (`setPieceSquareTables`), so evaluate()
  no longer adds them up. Identical search.
- `makeMove(move, notInCheck)`: when not in check, a non-king move from a
  square not in line with the king can't expose it, so the attack test is
  skipped. Identical search.
- Flat evaluation scratch arrays, cleared without fill(). Identical search.
- Lazy evaluation: the quiescence search's stand-pat uses material and
  tables alone when they are more than 3 pawns outside the window (the
  rest of the evaluation is under 1.9 pawns 99% of the time there).
- The quiescence search skips captures by a piece worth more than its
  victim onto a square an enemy pawn guards.

About 20-25% less time to depth 9 on the benchmark positions.

- SPRT vs 1.3, everything above: H1 accepted after 572 games, +230 -154 =188,
  **+46 Elo** (+23 to +70). The owner accepted this run as the final test.
- Without the last two (only the first two items plus lazy evaluation)
  vs 1.3: +17 Elo (-4 to +39) at 699 games, stopped.
- Full 1.4 vs 1.4 without lazy evaluation: H1 accepted after 590 games,
  +44 Elo (+22 to +67).
- Full 1.4 vs 1.4 without the bad-capture skip: +27 Elo (+8 to +47) at 856
  games, LLR 2.27; cut short by a machine restart, accepted by the owner.

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
