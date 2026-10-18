// Runs the engine in a background thread so the board stays responsive while
// it thinks. Messages in:
//   { id, type: 'analyse', history, timeMs, depth }  search the position
//   { id, type: 'play', history, timeMs }             pick a move for a game
// where `history` is the game's moves as UCI from the starting position.
// Messages out: { id, type: 'info', info } after each depth, then
// { id, type: 'done', result }. To stop early, the page ends the worker.

import { Engine, chooseMove, positionFromHistory } from '../src/engine/index.js';

const engine = new Engine({ ttBits: 20 });

self.onmessage = ({ data }) => {
  const { id, type, history, timeMs, depth } = data;
  const pos = positionFromHistory(history);
  const onInfo = (info) => self.postMessage({ id, type: 'info', info });
  const result = type === 'play'
    ? chooseMove(engine, pos, history, { timeMs, onInfo })
    : engine.search(pos, { timeMs, depth, onInfo });
  self.postMessage({ id, type: 'done', result });
};
