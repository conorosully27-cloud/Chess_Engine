// Engine 1.1 (Engine 1.0 is kept on the engine-1.0 branch). Later
// versions are measured against it with engine-vs-engine matches.
//
// Changes over 1.0: every evaluation weight (material, piece-square tables,
// pawn structure, mobility, rook files, king shelter, tempo) Texel-tuned on
// 1.4 million labelled positions; search unchanged. About +18 Elo over 1.0
// (95% +5 to +31) in a 2154-game SPRT match at 100 ms a move.
export const ENGINE_VERSION = '1.1';

export { Engine, MATE, INFINITY, isMateScore } from './search.js';
export { evaluate } from './evaluate.js';
export { BOOK_LINES, bookMove, chooseMove, positionFromHistory } from './play.js';
