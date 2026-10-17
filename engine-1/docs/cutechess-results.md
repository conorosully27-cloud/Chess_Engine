# Cute Chess results vs Stockfish

Engine 1.6, run through `uci.js` / `engine.bat` in Cute Chess, against Stockfish
with `UCI_LimitStrength` on and `UCI_Elo` set as noted. 200 games each (100
openings from `test/openings.epd`, each played from both colours). Nominal
Stockfish Elo is not a rating-list Elo: its `UCI_Elo` scale is calibrated at
longer time controls than these games used, so read the Elo as approximate.

## Run 2: Stockfish UCI_Elo 2550

Engine at commit `76b0ff0` (time-safety-margin fix in `uci.js`).

```
Score of Conor's engine vs Stockfish: 92 - 86 - 22 [0.515]
...      Conor's engine playing White: 56 - 37 - 7  [0.595] 100
...      Conor's engine playing Black: 36 - 49 - 15  [0.435] 100
...      White vs Black: 105 - 73 - 22  [0.580] 200
Elo difference: 10.4 +/- 45.6, LOS: 67.4 %, DrawRatio: 11.0 %
SPRT: llr 0 (0.0%), lbound -inf, ubound inf
200 of 200 games finished.

Player: Conor's engine
   "Draw by 3-fold repetition": 14
   "Draw by fifty moves rule": 3
   "Draw by insufficient mating material": 3
   "Draw by stalemate": 2
   "Loss: Black mates": 37
   "Loss: White mates": 49
   "Win: Black loses on time": 5
   "Win: Black mates": 32
   "Win: White loses on time": 4
   "Win: White mates": 51
Player: Stockfish
   "Draw by 3-fold repetition": 14
   "Draw by fifty moves rule": 3
   "Draw by insufficient mating material": 3
   "Draw by stalemate": 2
   "Loss: Black loses on time": 5
   "Loss: Black mates": 32
   "Loss: White loses on time": 4
   "Loss: White mates": 51
   "Win: Black mates": 37
   "Win: White mates": 49
```

Notes:

- Engine lost no games on time; Stockfish lost 9 (5 as Black, 4 as White).
- Without those 9 time wins the score is 83 - 86 - 22 (49.2%), about -5 Elo
  against 2550, so roughly 2545 at 1 s per move, with an error of about
  +/- 45 Elo.
- Colour split (59.5% as White, 43.5% as Black) is within the noise at 100
  games each.

## Run 1: Stockfish UCI_Elo 2700

Engine before the `uci.js` time-margin fix.

```
Score of Conor's engine vs Stockfish: 52 - 129 - 19 [0.307]
...      Conor's engine playing White: 23 - 68 - 9  [0.275] 100
...      Conor's engine playing Black: 29 - 61 - 10  [0.340] 100
...      White vs Black: 84 - 97 - 19  [0.468] 200
Elo difference: -141.0 +/- 49.6, LOS: 0.0 %, DrawRatio: 9.5 %
SPRT: llr 0 (0.0%), lbound -inf, ubound inf
200 of 200 games finished.

Player: Conor's engine
   "Draw by 3-fold repetition": 9
   "Draw by fifty moves rule": 6
   "Draw by insufficient mating material": 2
   "Draw by stalemate": 2
   "Loss: Black loses on time": 8
   "Loss: Black mates": 59
   "Loss: White loses on time": 9
   "Loss: White mates": 53
   "Win: Black loses on time": 5
   "Win: Black mates": 18
   "Win: White loses on time": 11
   "Win: White mates": 18
```

Notes:

- 33 of 200 games ended on time (engine lost 17, Stockfish lost 16), so
  timing noise affected this run. Without them the score is 36 - 112 - 19
  (27.2%), about -170 Elo against 2700, so roughly 2530.
- Run 1 gives about 2530 to 2560 against 2700 and run 2 about 2545 against
  2550, so the two runs agree.

## Summary

Engine 1.6 is about 2550 Elo on Stockfish's `UCI_Elo` scale at about 1 s per
move (error +/- 45). Re-test at the same settings after each engine change.
