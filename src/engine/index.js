// Engine 1.3 (Engine 1.0 is kept on the engine-1.0 branch). Later
// versions are measured against it with engine-vs-engine matches.
//
// Changes over 1.2: king safety, Texel-tuned with the rest: king danger
// (attack units from pieces bearing on the king zone, weak squares, safe
// checks; charged units² in the middlegame), pawn storms and open files at
// the king; plus an evaluation cache (same search, faster). About +28 Elo
// over 1.2 (95% +11 to +46) in an 1151-game SPRT match at 100 ms a move.
//
// 1.2 over 1.1: tuned endgame terms, about +44 Elo.
// 1.1 over 1.0: all evaluation weights Texel-tuned, about +18 Elo.
export const ENGINE_VERSION = '1.3';

export { Engine, MATE, INFINITY, isMateScore } from './search.js';
export { evaluate } from './evaluate.js';
export { BOOK_LINES, bookMove, chooseMove, positionFromHistory } from './play.js';
