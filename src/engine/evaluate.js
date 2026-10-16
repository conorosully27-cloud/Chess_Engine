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

import { setPieceSquareTables } from '../position.js';
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

// The same, signed by color, plus each piece's phase weight: positions keep
// running sums of these as moves are made (psqMg, psqEg, psqPhase).
const PSQ = { mg: new Int16Array(16 * 128), eg: new Int16Array(16 * 128), phase: new Int8Array(16) };
for (let piece = 0; piece < 16; piece++) {
  const type = piece & 7;
  if (type < PAWN || type > KING) continue;
  const sign = piece & 8 ? -1 : 1;
  for (let i = piece * 128; i < piece * 128 + 128; i++) {
    PSQ.mg[i] = sign * MG[i];
    PSQ.eg[i] = sign * EG[i];
  }
  PSQ.phase[piece] = PHASE_WEIGHT[type];
}
setPieceSquareTables(PSQ);

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
// King safety (see kingSafety() below and weights.js).
const KING_DANGER = W.kingDanger;
const PAWN_STORM = W.pawnStorm;
const PAWN_STORM_BLOCKED = W.pawnStormBlocked;
const KING_OPEN_FILE = W.kingOpenFile;

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

// The search evaluates many positions more than once, so recent results
// are kept, keyed by the position's hash (both halves, so a hit is exact).
const CACHE_MASK = (1 << 16) - 1;
const cacheHashLo = new Int32Array(CACHE_MASK + 1);
const cacheHashHi = new Int32Array(CACHE_MASK + 1);
const cacheScores = new Int16Array(CACHE_MASK + 1);

export function evaluate(pos) {
  const slot = pos.hashLo & CACHE_MASK;
  if (cacheHashHi[slot] === pos.hashHi && cacheHashLo[slot] === pos.hashLo) return cacheScores[slot];
  // Clamped well short of mate scores (and of what the cache can hold).
  const score = Math.max(-20000, Math.min(20000, evaluateUncached(pos)));
  cacheHashLo[slot] = pos.hashLo;
  cacheHashHi[slot] = pos.hashHi;
  cacheScores[slot] = score;
  return score;
}

// Lazy evaluation, for the quiescence search's stand-pat: when material and
// tables alone are further than LAZY_MARGIN outside [alpha, beta], the rest
// of the evaluation (worth under 2 pawns 99% of the time) can't bring the
// score back inside, so the cheap score stands in for the full one.
export const LAZY_MARGIN = 300;

export function evaluateLazy(pos, alpha, beta) {
  const slot = pos.hashLo & CACHE_MASK;
  if (cacheHashHi[slot] === pos.hashHi && cacheHashLo[slot] === pos.hashLo) return cacheScores[slot];
  const quick = evaluateMaterial(pos);
  if (quick - LAZY_MARGIN >= beta || quick + LAZY_MARGIN <= alpha) return quick;
  return evaluate(pos);
}

// Material and piece-square tables only, tapered, from the side to move's
// point of view: a cheap first guess at evaluate(), for lazy evaluation.
export function evaluateMaterial(pos) {
  if (pos.psqTables !== PSQ) pos.trackPieceSquares(PSQ);
  const mg = pos.psqMg;
  const eg = pos.psqEg;
  let phase = pos.psqPhase;
  if (phase > MAX_PHASE) phase = MAX_PHASE;
  const score = ((mg * phase + eg * (MAX_PHASE - phase)) / MAX_PHASE) | 0;
  return (pos.turn === WHITE ? score : -score) + TEMPO;
}

function evaluateUncached(pos) {
  const b = pos.board;
  // Material and tables, kept up to date by the position as moves are made.
  if (pos.psqTables !== PSQ) pos.trackPieceSquares(PSQ);
  let mg = pos.psqMg;
  let eg = pos.psqEg;
  let phase = pos.psqPhase;
  bishops.fill(0);
  pawns.fill(0);
  rooks.fill(0);
  nonPawnMaterial.fill(0);
  unstoppable.fill(0);
  attackScan(pos);
  for (let c = 0; c < 2; c++) {
    pawnCount[c].fill(0);
    pawnMostAdvanced[c].fill(-1);
    pawnLeastAdvanced[c].fill(8);
  }

  for (let i = 0; i < pieceCount; i++) {
    const sq = pieceList[i];
    const piece = b[sq];
    const type = piece & 7;
    const c = piece >> 3; // 0 = White, 1 = Black
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
    }
  }

  // Bare kings and lone minor pieces can't win.
  if (pawns[0] + pawns[1] === 0 && phase <= 2 && pos.isInsufficientMaterial()) return 0;

  for (let c = 0; c < 2; c++) {
    const sign = c === 0 ? 1 : -1;
    const them = 1 - c;

    // Mobility, totalled per piece type by attackScan().
    for (let type = KNIGHT; type <= QUEEN; type++) {
      const moves = mobilityTotal[(c << 3) | type];
      mg += sign * MOBILITY[type][0] * moves;
      eg += sign * MOBILITY[type][1] * moves;
    }

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

    // Files around the king without our pawns, and enemy pawns storming it.
    const king = pos.kingSquare[c];
    for (let f = Math.max(0, (king & 7) - 1); f <= Math.min(7, (king & 7) + 1); f++) {
      if (pawnCount[c][f] === 0) mg += sign * KING_OPEN_FILE[pawnCount[them][f] === 0 ? 1 : 0];
      const storm = stormPawn(b, king, c, f, pawnMostAdvanced[them][f]);
      if (storm >= 0) mg += sign * (storm & 8 ? PAWN_STORM_BLOCKED[storm & 7] : PAWN_STORM[storm]);
    }

    // King danger: attack units, worth more the more of them there are.
    const at = c * DANGER_TERMS;
    if (danger[at + DANGER.BASE]) {
      let units = 0;
      for (let i = 0; i < DANGER_TERMS; i++) units += KING_DANGER[i] * danger[at + i];
      if (units > 0) {
        mg -= (sign * units * units) / DANGER_MG_DIVISOR;
        eg -= (sign * units) / DANGER_EG_DIVISOR;
      }
    }
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

// ------------------------------------------------------------ king safety
//
// The classic king-danger model: count "attack units" against each king
// (pieces bearing on the squares around it, safe checks the enemy has,
// weak squares next to it), then charge units² / 720 in the middlegame, so
// one attacker is a nuisance but three are a crisis. Each unit's weight is
// in weights.js (kingDanger, indexed by DANGER). The endgame part is linear.

export const DANGER = {
  KNIGHT_ATTACKER: 0, // enemy knights hitting the king zone
  BISHOP_ATTACKER: 1,
  ROOK_ATTACKER: 2,
  QUEEN_ATTACKER: 3,
  ZONE_ATTACKS: 4, // squares of the zone hit, counted once per attacking piece
  WEAK_SQUARES: 5, // zone squares they attack that only our king or queen guards
  KNIGHT_CHECK: 6, // squares a piece of theirs can reach to give check, unguarded by us
  BISHOP_CHECK: 7,
  ROOK_CHECK: 8,
  QUEEN_CHECK: 9,
  NO_QUEEN: 10, // they have no queen
  BASE: 11, // constant, so a little pressure costs nothing
};
export const DANGER_TERMS = 12;
export const DANGER_MG_DIVISOR = 720;
export const DANGER_EG_DIVISOR = 20;

// The king zone: the king's square, the squares around it and the three
// squares two ranks in front. KING_ZONE[((c * 128 + king) << 7) + sq] is 1
// when sq is in the zone of color c's king on `king`; ZONE_SQUARES lists them.
const KING_ZONE = new Uint8Array(2 * 128 * 128);
const ZONE_SQUARES = new Int16Array(2 * 128 * 16).fill(-1);
for (let c = 0; c < 2; c++) {
  for (let king = 0; king < 120; king++) {
    if (king & 0x88) continue;
    const base = (c * 128 + king) << 7;
    let n = 0;
    const add = (sq) => {
      if (sq & 0x88) return;
      KING_ZONE[base + sq] = 1;
      ZONE_SQUARES[((c * 128 + king) << 4) + n++] = sq;
    };
    add(king);
    for (const step of KING_OFFSETS) add(king + step);
    const front = king + (c === 0 ? 32 : -32);
    if (front >= 0 && front < 128) for (const f of [-1, 0, 1]) if (((front + f) & 0x70) === (front & 0x70)) add(front + f);
  }
}

// Filled by attackScan() for the position last scanned.
const attackedBy = new Uint8Array(256); // [color * 128 + sq]: bit 1 << type per attacking type
export const danger = new Int16Array(2 * DANGER_TERMS); // [color of the king * DANGER_TERMS + term]
export const mobilityTotal = new Int16Array(16); // [piece]: Σ (moves - typical) over its pieces
const pieceList = new Int16Array(32); // squares of every piece, kings included
let pieceCount = 0;

// One pass over every piece's attacks: the attack maps, mobility, and the
// king-danger features for both kings. A king's danger is counted only when
// two pieces bear on its zone, or one does and the enemy has a queen.
export function attackScan(pos) {
  const b = pos.board;
  attackedBy.fill(0);
  for (let i = 0; i < 2 * DANGER_TERMS; i++) danger[i] = 0;
  for (let i = 0; i < 16; i++) mobilityTotal[i] = 0;
  let whiteAttackers = 0, blackAttackers = 0; // pieces bearing on that king's zone
  let whiteQueen = 0, blackQueen = 0;
  pieceCount = 0;

  for (let sq = 0; sq < 120; sq++) {
    if (sq & 0x88) { sq += 7; continue; }
    const piece = b[sq];
    if (piece === EMPTY) continue;
    pieceList[pieceCount++] = sq;
    const type = piece & 7;
    const color = piece & 8;
    const c = color >> 3;
    const map = c << 7;
    const bit = 1 << type;
    if (type === PAWN) {
      const ahead = sq + (c === 0 ? 16 : -16);
      if (((ahead - 1) & 0x88) === 0) attackedBy[map + ahead - 1] |= bit;
      if (((ahead + 1) & 0x88) === 0) attackedBy[map + ahead + 1] |= bit;
      continue;
    }
    if (type === KING) {
      for (let i = 0; i < 8; i++) {
        const to = sq + KING_OFFSETS[i];
        if ((to & 0x88) === 0) attackedBy[map + to] |= bit;
      }
      continue;
    }
    const zone = ((1 - c) * 128 + pos.kingSquare[1 - c]) << 7;
    let moves = 0;
    let hits = 0;
    if (type === KNIGHT) {
      for (let i = 0; i < 8; i++) {
        const to = sq + KNIGHT_OFFSETS[i];
        if (to & 0x88) continue;
        attackedBy[map + to] |= bit;
        hits += KING_ZONE[zone + to];
        if (b[to] === EMPTY || (b[to] & 8) !== color) moves++;
      }
    } else {
      if (type === QUEEN) {
        if (c === 0) whiteQueen = 1;
        else blackQueen = 1;
      }
      const directions = type === BISHOP ? BISHOP_OFFSETS : type === ROOK ? ROOK_OFFSETS : KING_OFFSETS;
      for (let i = 0; i < directions.length; i++) {
        const step = directions[i];
        for (let to = sq + step; (to & 0x88) === 0; to += step) {
          attackedBy[map + to] |= bit;
          hits += KING_ZONE[zone + to];
          if (b[to] === EMPTY) {
            moves++;
            continue;
          }
          if ((b[to] & 8) !== color) moves++;
          break;
        }
      }
    }
    mobilityTotal[piece] += moves - MOBILITY_TYPICAL[type];
    if (hits) {
      const at = (1 - c) * DANGER_TERMS;
      danger[at + type - KNIGHT]++;
      danger[at + DANGER.ZONE_ATTACKS] += hits;
      if (c === 0) blackAttackers++;
      else whiteAttackers++;
    }
  }

  for (let c = 0; c < 2; c++) {
    const them = 1 - c;
    const at = c * DANGER_TERMS;
    const attackers = c === 0 ? whiteAttackers : blackAttackers;
    const theirQueen = c === 0 ? blackQueen : whiteQueen;
    if (attackers <= 1 - theirQueen) {
      for (let i = at; i < at + DANGER_TERMS; i++) danger[i] = 0;
      continue;
    }
    const ours = c << 7;
    const theirs = them << 7;
    const king = pos.kingSquare[c];
    const list = (c * 128 + king) << 4;
    for (let i = list; i < list + 16 && ZONE_SQUARES[i] >= 0; i++) {
      const sq = ZONE_SQUARES[i];
      if (attackedBy[theirs + sq] && (attackedBy[ours + sq] & ~((1 << KING) | (1 << QUEEN))) === 0) danger[at + DANGER.WEAK_SQUARES]++;
    }
    // Safe checks: squares checking our king that a piece of theirs can
    // reach, not occupied by their own pieces, and not guarded by us.
    const theirColor = them << 3;
    for (let i = 0; i < 8; i++) {
      const sq = king + KNIGHT_OFFSETS[i];
      if (sq & 0x88 || (b[sq] !== EMPTY && (b[sq] & 8) === theirColor)) continue;
      if (attackedBy[theirs + sq] & (1 << KNIGHT) && !attackedBy[ours + sq]) danger[at + DANGER.KNIGHT_CHECK]++;
    }
    for (let i = 0; i < 8; i++) {
      const step = KING_OFFSETS[i];
      const diagonal = i === 0 || i === 2 || i === 5 || i === 7; // 17, 15, -15, -17
      const slider = diagonal ? 1 << BISHOP : 1 << ROOK;
      const term = diagonal ? DANGER.BISHOP_CHECK : DANGER.ROOK_CHECK;
      for (let sq = king + step; (sq & 0x88) === 0; sq += step) {
        if (b[sq] !== EMPTY && (b[sq] & 8) === theirColor) break;
        if (!attackedBy[ours + sq]) {
          if (attackedBy[theirs + sq] & slider) danger[at + term]++;
          if (attackedBy[theirs + sq] & (1 << QUEEN)) danger[at + DANGER.QUEEN_CHECK]++;
        }
        if (b[sq] !== EMPTY) break;
      }
    }
    if (!theirQueen) danger[at + DANGER.NO_QUEEN] = 1;
    danger[at + DANGER.BASE] = 1;
  }
}

// A storming pawn: the enemy's most advanced pawn on file `f` (`relRank`
// from their side, -1 if none), if it is still in front of the king of color
// `c`. Returns that rank (1-6), plus 8 if one of our pawns stands right in
// front of it (blocked), or -1.
export function stormPawn(b, king, c, f, relRank) {
  if (relRank < 0) return -1;
  const kingRelRank = c === 0 ? king >> 4 : 7 - (king >> 4);
  if (7 - relRank <= kingRelRank) return -1; // level with or behind the king
  const sq = ((c === 0 ? 7 - relRank : relRank) << 4) | f;
  return relRank | (b[sq + (c === 0 ? -16 : 16)] === ((c << 3) | PAWN) ? 8 : 0);
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
