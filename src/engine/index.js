// Engine 1.4 (Engine 1.0 is kept on the engine-1.0 branch). Later
// versions are measured against it with engine-vs-engine matches.
//
// Changes over 1.3: speed. Positions keep running material/piece-square
// sums; a cheaper legality test; lazy evaluation in the quiescence search;
// leaner evaluation scratch arrays; and the quiescence search skips
// captures of cheaper pieces guarded by a pawn. About +46 Elo over 1.3
// (95% +23 to +70) in a 572-game SPRT match at 100 ms a move.
//
// 1.3 over 1.2: king safety, about +28 Elo.
// 1.2 over 1.1: tuned endgame terms, about +44 Elo.
// 1.1 over 1.0: all evaluation weights Texel-tuned, about +18 Elo.
export const ENGINE_VERSION = '1.4';

export { Engine, MATE, INFINITY, isMateScore } from './search.js';
export { evaluate } from './evaluate.js';
export { BOOK_LINES, bookMove, chooseMove, positionFromHistory } from './play.js';
