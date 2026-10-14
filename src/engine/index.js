// Engine 1.0: the first full-strength engine (git tag engine-v1.0). Later
// versions are measured against it with engine-vs-engine matches.
export const ENGINE_VERSION = '1.0';

export { Engine, MATE, INFINITY, isMateScore } from './search.js';
export { evaluate } from './evaluate.js';
export { BOOK_LINES, bookMove, chooseMove, positionFromHistory } from './play.js';
