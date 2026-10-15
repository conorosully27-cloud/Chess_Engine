// Texel tuning: fits every evaluation weight in src/engine/weights.js to the
// results of real games.
//
//   npm run tune -- --data quiet-labeled.v7.epd.gz
//   npm run tune -- --data positions.epd --epochs 2000 --limit 500000
//
// The data is an EPD file (optionally gzipped) of quiet positions, each with
// its game result: lines like `<fen fields> c9 "1-0";`. The Zurichess
// "quiet-labeled" sets are the usual choice.
//
// The idea (Peter Österlund's, for Texel): turn the evaluation into a
// predicted score with a logistic curve, 1 / (1 + 10^(-K * eval / 400)), and
// pick the weights that make it predict the game results best (least mean
// squared error). Because the evaluation is linear in its weights (see
// eval-trace.js), each position's coefficients are extracted once, and the
// error and its gradient for all weights together are then cheap to compute,
// so the weights are fitted by gradient descent (Adam) rather than by
// nudging one weight at a time.
//
// 10% of the positions are held back to check the fit generalises; the
// weights that score best on them are written back to weights.js (or
// --out). Test the result with an SPRT match before keeping it.

import { createReadStream, writeFileSync } from 'node:fs';
import { createGunzip } from 'node:zlib';
import { createInterface } from 'node:readline';
import { resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import { Position } from '../src/index.js';
import { WEIGHTS } from '../src/engine/weights.js';
import { layout, trace, unflatten } from './eval-trace.js';

const ROOT = resolve(fileURLToPath(import.meta.url), '../..');

async function main() {
  const options = parseArgs(process.argv.slice(2));
  if (options.help || !options.data) {
    console.log(`Usage: npm run tune -- --data <file.epd[.gz]> [options]

  --data <file>     EPD positions with results ("1-0", "0-1", "1/2-1/2")
  --epochs <n>      passes of gradient descent (default 1000)
  --lr <x>          Adam learning rate in centipawns (default 1)
  --limit <n>       use only the first n positions
  --validate <x>    share of positions held back (default 0.1)
  --out <file>      where to write the weights (default src/engine/weights.js)`);
    process.exit(options.help ? 0 : 1);
  }
  const epochs = Number(options.epochs ?? 1000);
  const lr = Number(options.lr ?? 1);
  const out = resolve(options.out ?? `${ROOT}/src/engine/weights.js`);

  const lay = layout(WEIGHTS);
  const nParams = lay.values.length;
  console.log(`${nParams} weights; loading ${options.data}...`);
  const data = await loadPositions(options.data, lay, Number(options.limit ?? Infinity), Number(options.validate ?? 0.1));
  console.log(`${data.train.n} training positions, ${data.valid.n} held back (${data.skipped} skipped)`);

  const weights = Float64Array.from(lay.values);
  const K = fitK(data.train, weights);
  console.log(`K = ${K.toFixed(3)}; error ${error(data.train, weights, K).toFixed(6)} (training), ` +
    `${error(data.valid, weights, K).toFixed(6)} (held back)`);

  // Adam.
  const m = new Float64Array(nParams);
  const v = new Float64Array(nParams);
  const grad = new Float64Array(nParams);
  let best = { error: error(data.valid, weights, K), weights: Float64Array.from(weights), epoch: 0 };
  const started = Date.now();
  for (let epoch = 1; epoch <= epochs; epoch++) {
    gradient(data.train, weights, K, grad);
    for (let i = 0; i < nParams; i++) {
      m[i] = 0.9 * m[i] + 0.1 * grad[i];
      v[i] = 0.999 * v[i] + 0.001 * grad[i] * grad[i];
      const mHat = m[i] / (1 - 0.9 ** epoch);
      const vHat = v[i] / (1 - 0.999 ** epoch);
      weights[i] -= (lr * mHat) / (Math.sqrt(vHat) + 1e-12);
    }
    if (epoch % 25 === 0 || epoch === epochs) {
      const trainError = error(data.train, weights, K);
      const validError = error(data.valid, weights, K);
      if (validError < best.error) best = { error: validError, weights: Float64Array.from(weights), epoch };
      const seconds = ((Date.now() - started) / 1000).toFixed(0);
      console.log(`epoch ${epoch}: error ${trainError.toFixed(6)} (training), ${validError.toFixed(6)} (held back)  ${seconds}s`);
    }
    if (epoch % 100 === 0 || epoch === epochs) save(out, best.weights);
  }
  console.log(`Best held-back error ${best.error.toFixed(6)} at epoch ${best.epoch}; weights written to ${out}`);
}

function save(file, values) {
  const rounded = Array.from(values, (x) => Math.round(x) || 0);
  writeFileSync(file, formatWeights(unflatten(WEIGHTS, rounded)));
}

// ------------------------------------------------------------------- data

// Positions packed for speed: for position j, entries start[j]..start[j+1]
// of `index`/`share` say which weights it uses and how much (the coefficient
// already scaled by the game phase), plus a constant term and the result.
async function loadPositions(file, lay, limit, validateShare) {
  const lines = [];
  const input = createReadStream(file);
  const reader = createInterface({ input: file.endsWith('.gz') ? input.pipe(createGunzip()) : input, crlfDelay: Infinity });
  for await (const line of reader) {
    if (line.trim()) lines.push(line);
    if (lines.length >= limit) break;
  }
  reader.close();
  input.destroy();

  // Shuffle (seeded, so runs are repeatable) before splitting off the
  // held-back set: the files are often sorted by game.
  let seed = 12345;
  const random = () => ((seed = (seed * 1103515245 + 12345) >>> 0) / 2 ** 32);
  for (let i = lines.length - 1; i > 0; i--) {
    const j = Math.floor(random() * (i + 1));
    [lines[i], lines[j]] = [lines[j], lines[i]];
  }

  const validCount = Math.floor(lines.length * validateShare);
  let skipped = 0;
  const pack = (subset) => {
    const results = [];
    const constants = [];
    const starts = [0];
    const index = [];
    const share = [];
    for (const line of subset) {
      const result = parseResult(line);
      let pos;
      try {
        pos = new Position(line.split(/\s+/).slice(0, 4).join(' ') + ' 0 1');
      } catch {
        skipped++;
        continue;
      }
      const t = result === null ? null : trace(pos, lay);
      if (!t) { skipped++; continue; }
      const mgShare = t.phase / 24;
      for (const [i, n] of t.coef) {
        const kind = lay.kinds[i];
        index.push(i);
        share.push(n * (kind === 'mg' ? mgShare : kind === 'eg' ? 1 - mgShare : 1));
      }
      starts.push(index.length);
      constants.push(t.mopup * (1 - mgShare));
      results.push(result);
    }
    return {
      n: results.length,
      result: Float64Array.from(results),
      constant: Float64Array.from(constants),
      start: Uint32Array.from(starts),
      index: Uint16Array.from(index),
      share: Float32Array.from(share),
    };
  };
  const valid = pack(lines.slice(0, validCount));
  const train = pack(lines.slice(validCount));
  return { train, valid, skipped };
}

function parseResult(line) {
  if (line.includes('"1-0"')) return 1;
  if (line.includes('"0-1"')) return 0;
  if (line.includes('"1/2-1/2"')) return 0.5;
  return null;
}

// ------------------------------------------------------------- the fitting

function evaluateAll(set, weights, out) {
  const { start, index, share, constant } = set;
  for (let j = 0; j < set.n; j++) {
    let score = constant[j];
    for (let e = start[j], end = start[j + 1]; e < end; e++) score += weights[index[e]] * share[e];
    out[j] = score;
  }
}

const scratch = new Map();
function scores(set, weights) {
  let out = scratch.get(set);
  if (!out) scratch.set(set, (out = new Float64Array(set.n)));
  evaluateAll(set, weights, out);
  return out;
}

const sigmoid = (score, K) => 1 / (1 + Math.pow(10, (-K * score) / 400));

function error(set, weights, K) {
  const q = scores(set, weights);
  let sum = 0;
  for (let j = 0; j < set.n; j++) {
    const d = set.result[j] - sigmoid(q[j], K);
    sum += d * d;
  }
  return sum / set.n;
}

// d(error)/d(weight) for every weight at once.
function gradient(set, weights, K, grad) {
  const q = scores(set, weights);
  const { start, index, share, result } = set;
  const k = (K * Math.LN10) / 400;
  grad.fill(0);
  for (let j = 0; j < set.n; j++) {
    const s = sigmoid(q[j], K);
    const g = (-2 * (result[j] - s) * s * (1 - s) * k) / set.n;
    for (let e = start[j], end = start[j + 1]; e < end; e++) grad[index[e]] += g * share[e];
  }
}

// The K that best fits the current weights (golden-section search), so the
// weights are not rescaled just to match the curve.
function fitK(set, weights) {
  let lo = 0.1, hi = 3;
  const ratio = (Math.sqrt(5) - 1) / 2;
  for (let i = 0; i < 40; i++) {
    const a = hi - ratio * (hi - lo);
    const b = lo + ratio * (hi - lo);
    if (error(set, weights, a) < error(set, weights, b)) hi = b;
    else lo = a;
  }
  return (lo + hi) / 2;
}

// --------------------------------------------------------------- output

const pad = (x) => String(x).padStart(4);
const table = (values, indent) => {
  const rows = [];
  for (let r = 0; r < 8; r++) rows.push(indent + values.slice(r * 8, r * 8 + 8).map(pad).join(', ') + ',');
  return rows.join('\n');
};
const pair = ([a, b]) => `[${a}, ${b}]`;
const list = (values) => `[${values.join(', ')}]`;

// weights.js as text, comments included, so a tuned file reads like the
// hand-written one.
export function formatWeights(w) {
  const pst = (name) => `    ${name}: {
      mg: [
${table(w.pst[name].mg, '        ')}
      ],
      eg: [
${table(w.pst[name].eg, '        ')}
      ],
    },`;
  return `// Every tunable weight of the static evaluation, in centipawns.
//
// Each pair or { mg, eg } holds a middlegame and an endgame value, blended by
// evaluate() according to how much material is left. Piece-square tables
// are written from White's side with rank 8 at the top, as the board is
// printed; Black uses them mirrored.
//
// scripts/tune.js rewrites this file (Texel tuning), so keep it plain data.

// prettier-ignore
export const WEIGHTS = {
  material: {
    mg: ${list(w.material.mg)},
    eg: ${list(w.material.eg)},
  },
  pst: {
${['pawn', 'knight', 'bishop', 'rook', 'queen', 'king'].map(pst).join('\n')}
  },
  bishopPair: ${pair(w.bishopPair)},
  doubledPawn: ${pair(w.doubledPawn)},
  isolatedPawn: ${pair(w.isolatedPawn)},
  // By how far the pawn has advanced (relative rank 0-7; 1-6 are used).
  passedPawn: {
    mg: ${list(w.passedPawn.mg)},
    eg: ${list(w.passedPawn.eg)},
  },
  rookOpenFile: ${pair(w.rookOpenFile)},
  rookSemiOpenFile: ${pair(w.rookSemiOpenFile)},
  // Per square a piece can move to, beyond a typical count (see evaluate.js).
  mobility: {
    knight: ${pair(w.mobility.knight)},
    bishop: ${pair(w.mobility.bishop)},
    rook: ${pair(w.mobility.rook)},
    queen: ${pair(w.mobility.queen)},
  },
  // Middlegame only: a pawn right in front of the king, one square further,
  // and no pawn on that file near the king.
  kingShelter: ${list(w.kingShelter)},
  // Endgame, per rank of a passed pawn (0-7): per step of distance from our
  // king and from theirs to the square in front of it, and when that square
  // is occupied.
  passedOwnKing: ${list(w.passedOwnKing)},
  passedEnemyKing: ${list(w.passedEnemyKing)},
  passedBlocked: ${list(w.passedBlocked)},
  rookBehindPasser: ${pair(w.rookBehindPasser)},
  rookBehindEnemyPasser: ${pair(w.rookBehindEnemyPasser)},
  rookOnSeventh: ${pair(w.rookOnSeventh)},
  // A pawn ending passer the enemy king can no longer catch.
  unstoppablePasser: ${w.unstoppablePasser},
  tempo: ${w.tempo}, // having the move is worth a little
};
`;
}

function parseArgs(args) {
  const options = {};
  for (let i = 0; i < args.length; i++) {
    const arg = args[i];
    if (arg === '--help' || arg === '-h') options.help = true;
    else if (arg.startsWith('--')) options[arg.slice(2)] = args[++i];
  }
  return options;
}

if (process.argv[1] === fileURLToPath(import.meta.url)) await main();
