// Runs Engine 2 (C++, compiled to WebAssembly by engine-2/scripts/build_wasm.sh)
// in a background thread. Same messages as engine-worker.js (Engine 1.6):
//   in:  { id, type: 'analyse' | 'play', history, timeMs, depth }
//   out: { id, type: 'info', info } after each depth, then { id, type: 'done', result }
// The search runs on this worker's thread; to stop early the page ends the
// worker, as with Engine 1.6.

import createEngine2 from './engine2/engine2.js';
import { parseInfo } from './uci-info.js';
import { bookMove, positionFromHistory } from '../src/engine/index.js';
import { parseUci } from '../src/index.js';

let onLine = null;
// Loading is asynchronous: the handler below is attached at once (a message
// arriving while nothing listens would be lost) and waits for it.
const ready = createEngine2({ onInfo: (line) => onLine?.(line) }).then((engine) => {
  engine._engine_init(32);
  return engine.cwrap('engine_go', 'string', ['string', 'number', 'number']);
});

self.onmessage = async ({ data }) => {
  const { id, type, history, timeMs, depth } = data;
  if (type === 'play') {
    // A book move in the opening, for variety, as Engine 1.6 does.
    const book = bookMove(history);
    if (book && parseUci(positionFromHistory(history), book) !== null) {
      self.postMessage({ id, type: 'done', result: { bestMove: book, book: true } });
      return;
    }
  }
  const go = await ready;
  let last = null;
  onLine = (line) => {
    const info = parseInfo(line);
    if (!info) return;
    last = info;
    self.postMessage({ id, type: 'info', info });
  };
  const reply = go(history.join(' '), timeMs, type === 'analyse' ? (depth ?? 0) : 0);
  onLine = null;
  const move = reply.split(' ')[1];
  const bestMove = move && move !== '0000' ? move : null;
  self.postMessage({ id, type: 'done', result: { ...(last ?? parseInfo('info')), bestMove } });
};
