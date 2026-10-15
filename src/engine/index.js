// Engine 1.2 (Engine 1.0 is kept on the engine-1.0 branch). Later
// versions are measured against it with engine-vs-engine matches.
//
// Changes over 1.1: endgame evaluation terms, Texel-tuned with the rest:
// king distance (ours and theirs) to passed pawns, blocked passers, rooks
// behind passed pawns (Tarrasch), rook on the seventh, and the rule of the
// square in pawn endings; search unchanged. About +44 Elo over 1.1 (95% +22
// to +68) in a 637-game SPRT match at 100 ms a move.
//
// 1.1 over 1.0: all evaluation weights Texel-tuned, about +18 Elo.
export const ENGINE_VERSION = '1.2';

export { Engine, MATE, INFINITY, isMateScore } from './search.js';
export { evaluate } from './evaluate.js';
export { BOOK_LINES, bookMove, chooseMove, positionFromHistory } from './play.js';
