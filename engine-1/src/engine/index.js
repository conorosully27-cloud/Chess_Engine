// Engine 1.6 (Engine 1.0 is kept on the engine-1.0 branch). Later
// versions are measured against it with engine-vs-engine matches.
//
// Changes over 1.5: move ordering. When a quiet move causes a cut-off, the
// quiet moves tried before it at that node lose the history bonus it gains,
// so moves that keep failing sink in the ordering (11% fewer nodes). About
// +8 Elo over 1.5 (95% -5 to +21) in a 2000-game SPRT match at 100 ms a
// move: inconclusive, released on the owner's call.
//
// 1.5 over 1.4: late move pruning, about +73 Elo.
// 1.4 over 1.3: speed, about +46 Elo.
// 1.3 over 1.2: king safety, about +28 Elo.
// 1.2 over 1.1: tuned endgame terms, about +44 Elo.
// 1.1 over 1.0: all evaluation weights Texel-tuned, about +18 Elo.
export const ENGINE_VERSION = '1.6';

export { Engine, MATE, INFINITY, isMateScore } from './search.js';
export { evaluate } from './evaluate.js';
export { BOOK_LINES, bookMove, chooseMove, positionFromHistory } from './play.js';
