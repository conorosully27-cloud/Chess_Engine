// The evaluation as a linear model of its weights, for Texel tuning.
//
// evaluate() in src/engine/evaluate.js adds up weights from WEIGHTS. trace()
// walks a position the same way but records how often each weight is used
// instead of adding it, so that
//
//   evaluate(pos) ≈ constant + Σ weight[i] * coefficient[i]
//
// (to within rounding). The tuner extracts these coefficients once per
// position and can then score millions of positions for any set of weights
// without touching the board again. test/tune.test.js checks the two stay in
// step: any change to evaluate() must be mirrored here.

import {
  WHITE, EMPTY, PAWN, KNIGHT, BISHOP, ROOK, QUEEN, KING,
} from '../src/board.js';
import { mobility, MOBILITY_TYPICAL } from '../src/engine/evaluate.js';

const PHASE_WEIGHT = [0, 0, 1, 1, 2, 4, 0];
const MAX_PHASE = 24;
const MOPUP_VALUE = [0, 90, 320, 330, 480, 950, 0];
const TYPE_NAMES = [null, 'pawn', 'knight', 'bishop', 'rook', 'queen', 'king'];

// ------------------------------------------------------------ weight layout

// Flattens WEIGHTS into one list of numbers, remembering where each array
// starts and whether each entry is a middlegame, endgame or tempo weight.
export function layout(weights) {
  const values = [];
  const kinds = []; // 'mg', 'eg' or 'tempo'
  const names = [];
  const offset = {};
  const walk = (node, path, kind) => {
    if (typeof node === 'number') {
      offset[path] = values.length;
      values.push(node);
      kinds.push(kind);
      names.push(path);
    } else if (Array.isArray(node)) {
      offset[path] = values.length;
      node.forEach((v, i) => {
        values.push(v);
        // A bare [mg, eg] pair, unless the whole array is already one kind.
        kinds.push(kind ?? (i === 0 ? 'mg' : 'eg'));
        names.push(`${path}[${i}]`);
      });
    } else {
      for (const key of Object.keys(node)) {
        const childKind = key === 'mg' || key === 'eg' ? key : kind;
        walk(node[key], path ? `${path}.${key}` : key, childKind);
      }
    }
  };
  for (const key of Object.keys(weights)) {
    const kind = key === 'kingShelter' ? 'mg' : key === 'tempo' ? 'tempo' : undefined;
    walk(weights[key], key, kind);
  }
  return { values, kinds, names, offset };
}

// Rebuilds a WEIGHTS-shaped object from a flat list of values.
export function unflatten(weights, values) {
  let i = 0;
  const walk = (node) => {
    if (typeof node === 'number') return values[i++];
    if (Array.isArray(node)) return node.map(() => values[i++]);
    const out = {};
    for (const key of Object.keys(node)) out[key] = walk(node[key]);
    return out;
  };
  return walk(weights);
}

// ------------------------------------------------------------------- trace

const pawnCount = [new Int8Array(8), new Int8Array(8)];
const pawnLeastAdvanced = [new Int8Array(8), new Int8Array(8)];

// Returns null for positions evaluate() scores as dead draws. Otherwise
// { coef: Map(index -> count, White's point of view), phase (0-24),
//   mopup: constant endgame term from White's side, stm: +1/-1 }.
export function trace(pos, lay) {
  const o = lay.offset;
  const coef = new Map();
  const add = (index, n) => coef.set(index, (coef.get(index) ?? 0) + n);
  const b = pos.board;
  let phase = 0;
  const pawns = [[], []];
  const rooks = [[], []];
  const bishops = [0, 0];
  const nonPawnMaterial = [0, 0];
  for (let c = 0; c < 2; c++) {
    pawnCount[c].fill(0);
    pawnLeastAdvanced[c].fill(8);
  }

  for (let sq = 0; sq < 120; sq++) {
    if (sq & 0x88) { sq += 7; continue; }
    const piece = b[sq];
    if (piece === EMPTY) continue;
    const type = piece & 7;
    const c = piece >> 3;
    const sign = c === 0 ? 1 : -1;
    const rank = sq >> 4, file = sq & 7;
    const tableIndex = c === 0 ? (7 - rank) * 8 + file : rank * 8 + file;
    const name = TYPE_NAMES[type];
    add(o['material.mg'] + type, sign);
    add(o['material.eg'] + type, sign);
    add(o[`pst.${name}.mg`] + tableIndex, sign);
    add(o[`pst.${name}.eg`] + tableIndex, sign);
    phase += PHASE_WEIGHT[type];
    if (type === PAWN) {
      const relRank = c === 0 ? rank : 7 - rank;
      pawnCount[c][file]++;
      if (relRank < pawnLeastAdvanced[c][file]) pawnLeastAdvanced[c][file] = relRank;
      pawns[c].push(sq);
    } else if (type !== KING) {
      nonPawnMaterial[c] += MOPUP_VALUE[type];
      if (type === BISHOP) bishops[c]++;
      else if (type === ROOK) rooks[c].push(sq);
      const moves = mobility(b, sq, type, piece & 8) - MOBILITY_TYPICAL[type];
      add(o[`mobility.${name}`], sign * moves);
      add(o[`mobility.${name}`] + 1, sign * moves);
    }
  }

  if (pawns[0].length + pawns[1].length === 0 && phase <= 2 && pos.isInsufficientMaterial()) return null;

  for (let c = 0; c < 2; c++) {
    const sign = c === 0 ? 1 : -1;
    const them = 1 - c;
    if (bishops[c] >= 2) {
      add(o.bishopPair, sign);
      add(o.bishopPair + 1, sign);
    }
    for (let file = 0; file < 8; file++) {
      if (pawnCount[c][file] > 1) {
        add(o.doubledPawn, sign * (pawnCount[c][file] - 1));
        add(o.doubledPawn + 1, sign * (pawnCount[c][file] - 1));
      }
    }
    for (const sq of pawns[c]) {
      const file = sq & 7;
      const relRank = c === 0 ? sq >> 4 : 7 - (sq >> 4);
      const left = file > 0 ? pawnCount[c][file - 1] : 0;
      const right = file < 7 ? pawnCount[c][file + 1] : 0;
      if (left === 0 && right === 0) {
        add(o.isolatedPawn, sign);
        add(o.isolatedPawn + 1, sign);
      }
      let passed = true;
      for (let f = Math.max(0, file - 1); f <= Math.min(7, file + 1) && passed; f++) {
        if (pawnCount[them][f] && 7 - pawnLeastAdvanced[them][f] > relRank) passed = false;
      }
      if (passed) {
        add(o['passedPawn.mg'] + relRank, sign);
        add(o['passedPawn.eg'] + relRank, sign);
      }
    }
    for (const sq of rooks[c]) {
      const file = sq & 7;
      if (pawnCount[c][file] === 0) {
        const at = pawnCount[them][file] === 0 ? o.rookOpenFile : o.rookSemiOpenFile;
        add(at, sign);
        add(at + 1, sign);
      }
    }
    // King shelter: [pawn in front, pawn one further, no pawn].
    const kingSq = pos.kingSquare[c];
    const relRank = c === 0 ? kingSq >> 4 : 7 - (kingSq >> 4);
    if (relRank <= 1) {
      const pawn = (c << 3) | PAWN;
      const forward = c === 0 ? 16 : -16;
      const file = kingSq & 7;
      for (let f = Math.max(0, file - 1); f <= Math.min(7, file + 1); f++) {
        const front = (kingSq & 0x70) + f + forward;
        if (b[front] === pawn) add(o.kingShelter, sign);
        else if (b[front + forward] === pawn) add(o.kingShelter + 1, sign);
        else add(o.kingShelter + 2, sign);
      }
    }
  }

  let mopup = 0;
  for (let c = 0; c < 2; c++) {
    const them = 1 - c;
    if (pawns[them].length === 0 && nonPawnMaterial[them] === 0 && nonPawnMaterial[c] >= MOPUP_VALUE[ROOK]) {
      const loser = pos.kingSquare[them];
      const winner = pos.kingSquare[c];
      const lf = loser & 7, lr = loser >> 4;
      const centerDistance = Math.max(3 - lf, lf - 4) + Math.max(3 - lr, lr - 4);
      const kingDistance = Math.abs(lf - (winner & 7)) + Math.abs(lr - (winner >> 4));
      mopup += (c === 0 ? 1 : -1) * (10 * centerDistance + 4 * (14 - kingDistance));
    }
  }

  const stm = pos.turn === WHITE ? 1 : -1;
  add(o.tempo, stm);
  for (const [index, n] of coef) if (n === 0) coef.delete(index);
  return { coef, phase: Math.min(phase, MAX_PHASE), mopup, stm };
}

// The linear model's evaluation from White's side, before rounding.
export function linearEval(t, values, kinds) {
  const mgShare = t.phase / MAX_PHASE;
  let score = t.mopup * (1 - mgShare);
  for (const [index, n] of t.coef) {
    const kind = kinds[index];
    score += values[index] * n * (kind === 'mg' ? mgShare : kind === 'eg' ? 1 - mgShare : 1);
  }
  return score;
}
