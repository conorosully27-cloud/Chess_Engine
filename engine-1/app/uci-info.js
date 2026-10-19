// Turns Engine 2's UCI "info" lines into the analysis objects the app shows,
// the same shape Engine 1's search reports:
//   { bestMove, score, mate, pv, depth, nodes, timeMs, nps }
// Scores are for the side to move; `mate` is in moves (negative: getting
// mated) or null.

export function parseInfo(line) {
  const words = line.trim().split(/\s+/);
  if (words[0] !== 'info') return null;
  const info = { bestMove: null, score: 0, mate: null, pv: [], depth: 0, nodes: 0, timeMs: 0, nps: 0 };
  for (let i = 1; i < words.length; i++) {
    switch (words[i]) {
      case 'depth': info.depth = Number(words[++i]); break;
      case 'nodes': info.nodes = Number(words[++i]); break;
      case 'nps': info.nps = Number(words[++i]); break;
      case 'time': info.timeMs = Number(words[++i]); break;
      case 'score':
        if (words[i + 1] === 'mate') {
          info.mate = Number(words[i + 2]);
          info.score = info.mate > 0 ? 100000 : -100000;
        } else {
          info.score = Number(words[i + 2]);
        }
        i += 2;
        break;
      case 'pv':
        info.pv = words.slice(i + 1);
        i = words.length;
        break;
      default: break;
    }
  }
  info.bestMove = info.pv[0] ?? null;
  return info;
}
