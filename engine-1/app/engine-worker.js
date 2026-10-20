// Runs the engine in a background thread so the board stays responsive while
// it thinks. Messages in:
//   { id, type: 'analyse', history, fen, timeMs, depth }  search the position
//   { id, type: 'play', history, fen, timeMs }             pick a move for a game
// where `history` is the game's moves as UCI from `fen` (if given) or the
// starting position.
// Messages out: { id, type: 'info', info } after each depth, then
// { id, type: 'done', result }. To stop early, the page ends the worker.

import { Engine, chooseMove, positionFromHistory } from '../src/engine/index.js';

const engine = new Engine({ ttBits: 20 });

self.onmessage = ({ data }) => {
  const { id, type, history, fen, timeMs, depth } = data;
  const pos = positionFromHistory(history, fen);
  const onInfo = (info) => self.postMessage({ id, type: 'info', info });
  // The opening book only knows games from the starting position.
  const result = type === 'play' && !fen
    ? chooseMove(engine, pos, history, { timeMs, onInfo })
    : engine.search(pos, { timeMs, depth, onInfo });
  self.postMessage({ id, type: 'done', result });
};
