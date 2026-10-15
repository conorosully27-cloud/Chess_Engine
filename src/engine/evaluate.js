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

const MG_VALUE = [0, 90, 320, 330, 480, 950, 0];
const EG_VALUE = [0, 110, 290, 310, 530, 960, 0];

// Game phase: 24 with all pieces on the board, 0 with only kings and pawns.
const PHASE_WEIGHT = [0, 0, 1, 1, 2, 4, 0];
const MAX_PHASE = 24;

// Piece-square tables, written from White's side with rank 8 at the top as
// the board is printed. Black uses the same tables mirrored.
// prettier-ignore
const PST = {
  pawnMg: [
      0,   0,   0,   0,   0,   0,   0,   0,
     50,  50,  50,  50,  50,  50,  50,  50,
     10,  10,  20,  30,  30,  20,  10,  10,
      5,   5,  10,  25,  25,  10,   5,   5,
      0,   0,   0,  20,  20,   0,   0,   0,
      5,  -5, -10,   0,   0, -10,  -5,   5,
      5,  10,  10, -20, -20,  10,  10,   5,
      0,   0,   0,   0,   0,   0,   0,   0,
  ],
  pawnEg: [
      0,   0,   0,   0,   0,   0,   0,   0,
     80,  80,  80,  80,  80,  80,  80,  80,
     50,  50,  50,  50,  50,  50,  50,  50,
     30,  30,  30,  30,  30,  30,  30,  30,
     15,  15,  15,  15,  15,  15,  15,  15,
      5,   5,   5,   5,   5,   5,   5,   5,
      0,   0,   0,   0,   0,   0,   0,   0,
      0,   0,   0,   0,   0,   0,   0,   0,
  ],
  knight: [
    -50, -40, -30, -30, -30, -30, -40, -50,
    -40, -20,   0,   0,   0,   0, -20, -40,
    -30,   0,  10,  15,  15,  10,   0, -30,
    -30,   5,  15,  20,  20,  15,   5, -30,
    -30,   0,  15,  20,  20,  15,   0, -30,
    -30,   5,  10,  15,  15,  10,   5, -30,
    -40, -20,   0,   5,   5,   0, -20, -40,
    -50, -40, -30, -30, -30, -30, -40, -50,
  ],
  bishop: [
    -20, -10, -10, -10, -10, -10, -10, -20,
    -10,   0,   0,   0,   0,   0,   0, -10,
    -10,   0,   5,  10,  10,   5,   0, -10,
    -10,   5,   5,  10,  10,   5,   5, -10,
    -10,   0,  10,  10,  10,  10,   0, -10,
    -10,  10,  10,  10,  10,  10,  10, -10,
    -10,   5,   0,   0,   0,   0,   5, -10,
    -20, -10, -10, -10, -10, -10, -10, -20,
  ],
  rook: [
      0,   0,   0,   0,   0,   0,   0,   0,
      5,  10,  10,  10,  10,  10,  10,   5,
     -5,   0,   0,   0,   0,   0,   0,  -5,
     -5,   0,   0,   0,   0,   0,   0,  -5,
     -5,   0,   0,   0,   0,   0,   0,  -5,
     -5,   0,   0,   0,   0,   0,   0,  -5,
     -5,   0,   0,   0,   0,   0,   0,  -5,
      0,   0,   0,   5,   5,   0,   0,   0,
  ],
  queen: [
    -20, -10, -10,  -5,  -5, -10, -10, -20,
    -10,   0,   0,   0,   0,   0,   0, -10,
    -10,   0,   5,   5,   5,   5,   0, -10,
     -5,   0,   5,   5,   5,   5,   0,  -5,
      0,   0,   5,   5,   5,   5,   0,  -5,
    -10,   5,   5,   5,   5,   5,   0, -10,
    -10,   0,   5,   0,   0,   0,   0, -10,
    -20, -10, -10,  -5,  -5, -10, -10, -20,
  ],
  kingMg: [
    -30, -40, -40, -50, -50, -40, -40, -30,
    -30, -40, -40, -50, -50, -40, -40, -30,
    -30, -40, -40, -50, -50, -40, -40, -30,
    -30, -40, -40, -50, -50, -40, -40, -30,
    -20, -30, -30, -40, -40, -30, -30, -20,
    -10, -20, -20, -20, -20, -20, -20, -10,
     20,  20,   0,   0,   0,   0,  20,  20,
     20,  30,  10,   0,   0,  10,  30,  20,
  ],
  kingEg: [
    -50, -40, -30, -20, -20, -30, -40, -50,
    -30, -20, -10,   0,   0, -10, -20, -30,
    -30, -10,  20,  30,  30,  20, -10, -30,
    -30, -10,  30,  40,  40,  30, -10, -30,
    -30, -10,  30,  40,  40,  30, -10, -30,
    -30, -10,  20,  30,  30,  20, -10, -30,
    -30, -30,   0,   0,   0,   0, -30, -30,
    -50, -30, -30, -30, -30, -30, -30, -50,
  ],
};

const TABLES = {
  [PAWN]: [PST.pawnMg, PST.pawnEg],
  [KNIGHT]: [PST.knight, PST.knight],
  [BISHOP]: [PST.bishop, PST.bishop],
  [ROOK]: [PST.rook, PST.rook],
  [QUEEN]: [PST.queen, PST.queen],
  [KING]: [PST.kingMg, PST.kingEg],
};

// Material plus table bonus for every (piece, 0x88 square), precomputed.
// Values are positive for both colors; evaluate() applies the sign.
const MG = new Int16Array(16 * 128);
const EG = new Int16Array(16 * 128);
for (let type = PAWN; type <= KING; type++) {
  const [mgTable, egTable] = TABLES[type];
  for (let rank = 0; rank < 8; rank++) {
    for (let file = 0; file < 8; file++) {
      const sq = (rank << 4) | file;
      const whiteIndex = (7 - rank) * 8 + file; // tables list rank 8 first
      const blackIndex = rank * 8 + file; // mirrored for Black
      MG[type * 128 + sq] = MG_VALUE[type] + mgTable[whiteIndex];
      EG[type * 128 + sq] = EG_VALUE[type] + egTable[whiteIndex];
      MG[(8 | type) * 128 + sq] = MG_VALUE[type] + mgTable[blackIndex];
      EG[(8 | type) * 128 + sq] = EG_VALUE[type] + egTable[blackIndex];
    }
  }
}

const BISHOP_PAIR = [30, 50];
const DOUBLED_PAWN = [-10, -20];
const ISOLATED_PAWN = [-12, -16];
// By how far the pawn has advanced (relative rank 1-6).
const PASSED_PAWN_MG = [0, 5, 10, 15, 25, 40, 60, 0];
const PASSED_PAWN_EG = [0, 10, 20, 35, 55, 85, 130, 0];
const ROOK_OPEN_FILE = [25, 10];
const ROOK_SEMI_OPEN_FILE = [12, 6];
const TEMPO = 10; // having the move is worth a little

// Mobility: per square a piece can move to, measured from a typical count
// so an average piece scores about zero. [mg, eg, typical count]
const MOBILITY = {
  [KNIGHT]: [4, 4, 4],
  [BISHOP]: [5, 5, 6],
  [ROOK]: [2, 4, 7],
  [QUEEN]: [1, 2, 13],
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

export function evaluate(pos) {
  const b = pos.board;
  let mg = 0;
  let eg = 0;
  let phase = 0;
  bishops.fill(0);
  pawns.fill(0);
  rooks.fill(0);
  nonPawnMaterial.fill(0);
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
      nonPawnMaterial[c] += MG_VALUE[type];
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
      }
    }

    for (let i = 0; i < rooks[c]; i++) {
      const file = rookSquares[c][i] & 7;
      if (pawnCount[c][file] === 0) {
        const open = pawnCount[them][file] === 0;
        mg += sign * (open ? ROOK_OPEN_FILE[0] : ROOK_SEMI_OPEN_FILE[0]);
        eg += sign * (open ? ROOK_OPEN_FILE[1] : ROOK_SEMI_OPEN_FILE[1]);
      }
    }

    mg += sign * kingShelter(b, pos.kingSquare[c], c);
  }

  // Mop-up: against a bare king, drive it to the edge and bring our king
  // close, which is how king-and-queen or king-and-rook mates are won.
  for (let c = 0; c < 2; c++) {
    const them = 1 - c;
    if (pawns[them] === 0 && nonPawnMaterial[them] === 0 && nonPawnMaterial[c] >= MG_VALUE[ROOK]) {
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

// How many squares the piece on `sq` can move to (empty or enemy-occupied).
function mobility(b, sq, type, color) {
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

// Middlegame bonus for pawns sheltering a castled king (on the a-c or g-h
// files) on its back two ranks. A king still in the centre gets nothing, so
// pushing the d-, e- or f-pawn in front of it isn't punished.
function kingShelter(b, kingSq, c) {
  const file = kingSq & 7;
  if (file > 2 && file < 6) return 0;
  const relRank = c === 0 ? kingSq >> 4 : 7 - (kingSq >> 4);
  if (relRank > 1) return 0;
  const pawn = (c << 3) | PAWN;
  const forward = c === 0 ? 16 : -16;
  let bonus = 0;
  for (let f = Math.max(0, file - 1); f <= Math.min(7, file + 1); f++) {
    const front = (kingSq & 0x70) + f + forward;
    if (b[front] === pawn) bonus += 12;
    else if (b[front + forward] === pawn) bonus += 6;
    else bonus -= 12;
  }
  return bonus;
}
