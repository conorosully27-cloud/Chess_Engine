// Engine 1.5 (Engine 1.0 is kept on the engine-1.0 branch). Later
// versions are measured against it with engine-vs-engine matches.
//
// Changes over 1.4: late move pruning. In the last 3 plies of the search,
// off the main line and not in check, once 4, 7 or 12 quiet moves have
// been tried, the remaining quiet moves that don't give check are skipped.
// About +73 Elo over 1.4 (95% +43 to +104) in a 353-game SPRT match at
// 100 ms a move.
//
// 1.4 over 1.3: speed, about +46 Elo.
// 1.3 over 1.2: king safety, about +28 Elo.
// 1.2 over 1.1: tuned endgame terms, about +44 Elo.
// 1.1 over 1.0: all evaluation weights Texel-tuned, about +18 Elo.
export const ENGINE_VERSION = '1.5';

export { Engine, MATE, INFINITY, isMateScore } from './search.js';
export { evaluate } from './evaluate.js';
export { BOOK_LINES, bookMove, chooseMove, positionFromHistory } from './play.js';
