// Static evaluation: how good a position looks without searching further,
// in centipawns (100 = one pawn) from the side to move's point of view.
//
// Every term has a middlegame and an endgame value, blended by how much
// material is left ("tapered" evaluation), since what matters changes as
// pieces come off: a king should hide in the middlegame and march in the
// endgame, and passed pawns grow more valuable as the board empties.

import {
  WHITE, EMPTY, PAWN, KNIGHT, BISHOP, ROOK, QUEEN, KING,
  KNIGHT_OFFSETS, BISHOP_OFFSETS, ROOK_OFFSETS, KING_OFFSETS,
} from '../board.js';

import { WEIGHTS } from './weights.js';

const W = WEIGHTS;

// Fixed piece values for deciding when mop-up applies; not tuned.
const MOPUP_VALUE = [0, 90, 320, 330, 480, 950, 0];

// Game phase: 24 with all pieces on the board, 0 with only kings and pawns.
const PHASE_WEIGHT = [0, 0, 1, 1, 2, 4, 0];
const MAX_PHASE = 24;

const PST_BY_TYPE = [null, W.pst.pawn, W.pst.knight, W.pst.bishop, W.pst.rook, W.pst.queen, W.pst.king];

// Material plus table bonus for every (piece, 0x88 square), precomputed.
// Values are positive for both colors; evaluate() applies the sign.
const MG = new Int16Array(16 * 128);
const EG = new Int16Array(16 * 128);
for (let type = PAWN; type <= KING; type++) {
  const { mg: mgTable, eg: egTable } = PST_BY_TYPE[type];
  for (let rank = 0; rank < 8; rank++) {
    for (let file = 0; file < 8; file++) {
      const sq = (rank << 4) | file;
      const whiteIndex = (7 - rank) * 8 + file; // tables list rank 8 first
      const blackIndex = rank * 8 + file; // mirrored for Black
      MG[type * 128 + sq] = W.material.mg[type] + mgTable[whiteIndex];
      EG[type * 128 + sq] = W.material.eg[type] + egTable[whiteIndex];
      MG[(8 | type) * 128 + sq] = W.material.mg[type] + mgTable[blackIndex];
      EG[(8 | type) * 128 + sq] = W.material.eg[type] + egTable[blackIndex];
    }
  }
}

const BISHOP_PAIR = W.bishopPair;
const DOUBLED_PAWN = W.doubledPawn;
const ISOLATED_PAWN = W.isolatedPawn;
const PASSED_PAWN_MG = W.passedPawn.mg;
const PASSED_PAWN_EG = W.passedPawn.eg;
const ROOK_OPEN_FILE = W.rookOpenFile;
const ROOK_SEMI_OPEN_FILE = W.rookSemiOpenFile;
const [SHELTER_NEAR, SHELTER_FAR, SHELTER_NONE] = W.kingShelter;
const TEMPO = W.tempo;
// Endgame terms (applied in full only once material is off; see weights.js).
const PASSED_OWN_KING = W.passedOwnKing;
const PASSED_ENEMY_KING = W.passedEnemyKing;
const PASSED_BLOCKED = W.passedBlocked;
const ROOK_BEHIND_PASSER = W.rookBehindPasser;
const ROOK_BEHIND_ENEMY_PASSER = W.rookBehindEnemyPasser;
const ROOK_ON_SEVENTH = W.rookOnSeventh;
const UNSTOPPABLE_PASSER = W.unstoppablePasser;

// Mobility: per square a piece can move to, measured from a typical count
// so an average piece scores about zero. [mg, eg, typical count]
export const MOBILITY_TYPICAL = [0, 0, 4, 6, 7, 13, 0];
const MOBILITY = {
  [KNIGHT]: [...W.mobility.knight, MOBILITY_TYPICAL[KNIGHT]],
  [BISHOP]: [...W.mobility.bishop, MOBILITY_TYPICAL[BISHOP]],
  [ROOK]: [...W.mobility.rook, MOBILITY_TYPICAL[ROOK]],
  [QUEEN]: [...W.mobility.queen, MOBILITY_TYPICAL[QUEEN]],
};

// Scratch space reused by every call, so evaluation never allocates.
const pawnCount = [new Int8Array(8), new Int8Array(8)]; // [color][file]
const pawnMostAdvanced = [new Int8Array(8), new Int8Array(8)]; // furthest rank reached, relative to that side
const pawnLeastAdvanced = [new Int8Array(8), new Int8Array(8)];
const pawnSquares = [new Int16Array(16), new Int16Array(16)];
const rookSquares = [new Int16Array(16), new Int16Array(16)];
const bishops = new Int32Array(2); // [color]
const pawns = new Int32Array(2);
const rooks = new Int32Array(2);
const nonPawnMaterial = new Int32Array(2);
const unstoppable = new Int8Array(2);

export function evaluate(pos) {
  const b = pos.board;
  let mg = 0;
  let eg = 0;
  let phase = 0;
  bishops.fill(0);
  pawns.fill(0);
  rooks.fill(0);
  nonPawnMaterial.fill(0);
  unstoppable.fill(0);
  for (let c = 0; c < 2; c++) {
    pawnCount[c].fill(0);
    pawnMostAdvanced[c].fill(-1);
    pawnLeastAdvanced[c].fill(8);
  }

  for (let sq = 0; sq < 120; sq++) {
    if (sq & 0x88) { sq += 7; continue; }
    const piece = b[sq];
    if (piece === EMPTY) continue;
    const type = piece & 7;
    const c = piece >> 3; // 0 = White, 1 = Black
    const sign = c === 0 ? 1 : -1;
    mg += sign * MG[piece * 128 + sq];
    eg += sign * EG[piece * 128 + sq];
    phase += PHASE_WEIGHT[type];
    if (type === PAWN) {
      const file = sq & 7;
      const relRank = c === 0 ? sq >> 4 : 7 - (sq >> 4);
      pawnCount[c][file]++;
      if (relRank > pawnMostAdvanced[c][file]) pawnMostAdvanced[c][file] = relRank;
      if (relRank < pawnLeastAdvanced[c][file]) pawnLeastAdvanced[c][file] = relRank;
      pawnSquares[c][pawns[c]++] = sq;
    } else if (type !== KING) {
      nonPawnMaterial[c] += MOPUP_VALUE[type];
      if (type === BISHOP) bishops[c]++;
      else if (type === ROOK) rookSquares[c][rooks[c]++] = sq;
      const [mgWeight, egWeight, typical] = MOBILITY[type];
      const moves = mobility(b, sq, type, piece & 8) - typical;
      mg += sign * mgWeight * moves;
      eg += sign * egWeight * moves;
    }
  }

  // Bare kings and lone minor pieces can't win.
  if (pawns[0] + pawns[1] === 0 && phase <= 2 && pos.isInsufficientMaterial()) return 0;

  for (let c = 0; c < 2; c++) {
    const sign = c === 0 ? 1 : -1;
    const them = 1 - c;

    if (bishops[c] >= 2) {
      mg += sign * BISHOP_PAIR[0];
      eg += sign * BISHOP_PAIR[1];
    }

    for (let file = 0; file < 8; file++) {
      if (pawnCount[c][file] > 1) {
        mg += sign * DOUBLED_PAWN[0] * (pawnCount[c][file] - 1);
        eg += sign * DOUBLED_PAWN[1] * (pawnCount[c][file] - 1);
      }
    }

    for (let i = 0; i < pawns[c]; i++) {
      const sq = pawnSquares[c][i];
      const file = sq & 7;
      const relRank = c === 0 ? sq >> 4 : 7 - (sq >> 4);
      const left = file > 0 ? pawnCount[c][file - 1] : 0;
      const right = file < 7 ? pawnCount[c][file + 1] : 0;
      if (left === 0 && right === 0) {
        mg += sign * ISOLATED_PAWN[0];
        eg += sign * ISOLATED_PAWN[1];
      }
      // Passed: no enemy pawn ahead of it on its own or a neighboring file.
      // An enemy pawn's relative rank r is rank 7 - r from our side, so it
      // stands ahead of us when 7 - r > relRank.
      let passed = true;
      for (let f = Math.max(0, file - 1); f <= Math.min(7, file + 1) && passed; f++) {
        if (pawnCount[them][f] && 7 - pawnLeastAdvanced[them][f] > relRank) passed = false;
      }
      if (passed) {
        mg += sign * PASSED_PAWN_MG[relRank];
        eg += sign * PASSED_PAWN_EG[relRank];
        // Kings matter around a passer: ours escorting it, theirs stopping it.
        const front = sq + (c === 0 ? 16 : -16);
        eg += sign * PASSED_OWN_KING[relRank] * distance(pos.kingSquare[c], front);
        eg += sign * PASSED_ENEMY_KING[relRank] * distance(pos.kingSquare[them], front);
        if (b[front] !== EMPTY) eg += sign * PASSED_BLOCKED[relRank];
        // Tarrasch: rooks belong behind passed pawns, ours and theirs.
        const behind = rookBehind(b, sq, c);
        if (behind === c) {
          mg += sign * ROOK_BEHIND_PASSER[0];
          eg += sign * ROOK_BEHIND_PASSER[1];
        } else if (behind === them) {
          mg -= sign * ROOK_BEHIND_ENEMY_PASSER[0];
          eg -= sign * ROOK_BEHIND_ENEMY_PASSER[1];
        }
        if (!unstoppable[c] && nonPawnMaterial[them] === 0 && outsideSquare(pos, sq, c)) unstoppable[c] = 1;
      }
    }
    if (unstoppable[c]) eg += sign * UNSTOPPABLE_PASSER;

    for (let i = 0; i < rooks[c]; i++) {
      const file = rookSquares[c][i] & 7;
      if (pawnCount[c][file] === 0) {
        const open = pawnCount[them][file] === 0;
        mg += sign * (open ? ROOK_OPEN_FILE[0] : ROOK_SEMI_OPEN_FILE[0]);
        eg += sign * (open ? ROOK_OPEN_FILE[1] : ROOK_SEMI_OPEN_FILE[1]);
      }
      if (onSeventh(pos, rookSquares[c][i], c, pawnLeastAdvanced[them])) {
        mg += sign * ROOK_ON_SEVENTH[0];
        eg += sign * ROOK_ON_SEVENTH[1];
      }
    }

    mg += sign * kingShelter(b, pos.kingSquare[c], c);
  }

  // Mop-up: against a bare king, drive it to the edge and bring our king
  // close, which is how king-and-queen or king-and-rook mates are won.
  for (let c = 0; c < 2; c++) {
    const them = 1 - c;
    if (pawns[them] === 0 && nonPawnMaterial[them] === 0 && nonPawnMaterial[c] >= MOPUP_VALUE[ROOK]) {
      const loser = pos.kingSquare[them];
      const winner = pos.kingSquare[c];
      const lf = loser & 7, lr = loser >> 4;
      const centerDistance = Math.max(3 - lf, lf - 4) + Math.max(3 - lr, lr - 4);
      const kingDistance = Math.abs(lf - (winner & 7)) + Math.abs(lr - (winner >> 4));
      eg += (c === 0 ? 1 : -1) * (10 * centerDistance + 4 * (14 - kingDistance));
    }
  }

  if (phase > MAX_PHASE) phase = MAX_PHASE;
  const score = ((mg * phase + eg * (MAX_PHASE - phase)) / MAX_PHASE) | 0;
  return (pos.turn === WHITE ? score : -score) + TEMPO;
}

// King steps between two squares (Chebyshev distance).
export function distance(a, b) {
  return Math.max(Math.abs((a & 7) - (b & 7)), Math.abs((a >> 4) - (b >> 4)));
}

// Whose rook, if any, stands behind the pawn of color `c` on `sq` on its
// file with nothing in between: 0 = White, 1 = Black, -1 = none.
export function rookBehind(b, sq, c) {
  const back = c === 0 ? -16 : 16;
  for (let to = sq + back; (to & 0x88) === 0; to += back) {
    if (b[to] === EMPTY) continue;
    return (b[to] & 7) === ROOK ? b[to] >> 3 : -1;
  }
  return -1;
}

// Rule of the square: in a pawn ending, can the enemy king no longer catch
// the passer of color `c` on `sq`? Its path must also be clear.
export function outsideSquare(pos, sq, c) {
  const b = pos.board;
  const forward = c === 0 ? 16 : -16;
  const relRank = c === 0 ? sq >> 4 : 7 - (sq >> 4);
  const promotion = (sq & 7) | (c === 0 ? 0x70 : 0);
  for (let to = sq + forward; ; to += forward) {
    if (b[to] !== EMPTY) return false;
    if (to === promotion) break;
  }
  const steps = relRank === 1 ? 5 : 7 - relRank; // a pawn's first move can be two squares
  const theirMove = (pos.turn === WHITE ? 0 : 1) !== c;
  return distance(pos.kingSquare[1 - c], promotion) - (theirMove ? 1 : 0) > steps;
}

// A rook on the seventh rank (relative) that confines the enemy king to its
// back rank or attacks pawns still on their starting rank.
export function onSeventh(pos, sq, c, enemyLeastAdvanced) {
  const relRank = c === 0 ? sq >> 4 : 7 - (sq >> 4);
  if (relRank !== 6) return false;
  const king = pos.kingSquare[1 - c];
  if ((c === 0 ? king >> 4 : 7 - (king >> 4)) === 7) return true;
  for (let f = 0; f < 8; f++) if (enemyLeastAdvanced[f] === 1) return true;
  return false;
}

// How many squares the piece on `sq` can move to (empty or enemy-occupied).
export function mobility(b, sq, type, color) {
  let count = 0;
  if (type === KNIGHT) {
    for (let i = 0; i < 8; i++) {
      const to = sq + KNIGHT_OFFSETS[i];
      if ((to & 0x88) === 0 && (b[to] === EMPTY || (b[to] & 8) !== color)) count++;
    }
    return count;
  }
  const directions = type === BISHOP ? BISHOP_OFFSETS : type === ROOK ? ROOK_OFFSETS : KING_OFFSETS;
  for (let i = 0; i < directions.length; i++) {
    const step = directions[i];
    for (let to = sq + step; (to & 0x88) === 0; to += step) {
      if (b[to] === EMPTY) {
        count++;
        continue;
      }
      if ((b[to] & 8) !== color) count++;
      break;
    }
  }
  return count;
}

// Middlegame bonus for pawns sheltering a king on its back two ranks.
function kingShelter(b, kingSq, c) {
  const relRank = c === 0 ? kingSq >> 4 : 7 - (kingSq >> 4);
  if (relRank > 1) return 0;
  const pawn = (c << 3) | PAWN;
  const forward = c === 0 ? 16 : -16;
  const file = kingSq & 7;
  let bonus = 0;
  for (let f = Math.max(0, file - 1); f <= Math.min(7, file + 1); f++) {
    const front = (kingSq & 0x70) + f + forward;
    if (b[front] === pawn) bonus += SHELTER_NEAR;
    else if (b[front + forward] === pawn) bonus += SHELTER_FAR;
    else bonus += SHELTER_NONE;
  }
  return bonus;
}
