// The search: looks ahead through the moves of both sides to find the best
// move and a score for the position.
//
// Negamax alpha-beta (each side picks the move that is best for it, and
// branches that can't change the result are skipped), deepened one ply at a
// time until time runs out. Around that core:
//   - quiescence search: lines are followed to the end of their captures, so
//     a position is never judged in the middle of an exchange;
//   - a transposition table remembers positions already searched, keyed by
//     the position's Zobrist hash;
//   - move ordering (best move from earlier, then captures, then quiet moves
//     that caused cut-offs elsewhere) so the best moves are tried first;
//   - pruning that skips moves very unlikely to matter: null-move pruning,
//     reverse futility pruning, futility pruning and late move reductions.

import { evaluate, evaluateLazy } from './evaluate.js';
import {
  EMPTY, PAWN, KNIGHT, BISHOP, ROOK, QUEEN,
  FLAG_CAPTURE, FLAG_EN_PASSANT, moveFrom, moveTo, movePromotion,
} from '../board.js';
import { moveToUci } from '../notation.js';

export const MATE = 30000; // score for delivering mate now; mate in n plies scores MATE - n
export const INFINITY = 32000;
const MAX_PLY = 100;
export const isMateScore = (score) => Math.abs(score) >= MATE - MAX_PLY;

const TT_EXACT = 1;
const TT_LOWER = 2; // score is at least this (the search failed high)
const TT_UPPER = 3; // score is at most this (failed low)

// Piece values for move ordering only.
const ORDER_VALUE = [0, 100, 320, 330, 500, 900, 2000];

const now = () => performance.now();

class TranspositionTable {
  constructor(bits) {
    this.size = 1 << bits;
    this.mask = this.size - 1;
    this.keys = new Int32Array(this.size); // high half of the hash, to verify a hit
    this.moves = new Int32Array(this.size);
    this.scores = new Int16Array(this.size);
    this.depths = new Int8Array(this.size);
    this.flags = new Uint8Array(this.size);
  }

  clear() {
    this.flags.fill(0);
  }
}

export class Engine {
  // `ttBits` sets the transposition table at 2^ttBits entries (12 bytes each).
  constructor({ ttBits = 19 } = {}) {
    this.tt = new TranspositionTable(ttBits);
    this.killers = new Int32Array(MAX_PLY * 2);
    this.history = new Int32Array(16 * 128);
    this.pvTable = new Int32Array(MAX_PLY * MAX_PLY);
    this.pvLength = new Int32Array(MAX_PLY);
    // Per-ply scratch lists, so the search doesn't allocate.
    this.moveLists = Array.from({ length: MAX_PLY }, () => []);
    this.scoreLists = Array.from({ length: MAX_PLY }, () => []);
    this.nodes = 0;
    this.stopped = false;
  }

  newGame() {
    this.tt.clear();
    this.history.fill(0);
  }

  // Searches `pos` and returns the best move and its evaluation. Stops at
  // `depth` plies, after `timeMs` milliseconds, or after `nodes` positions,
  // whichever comes first. `onInfo` is called after each completed depth.
  // `pos` is returned to its original state.
  search(pos, { depth: maxDepth = MAX_PLY - 1, timeMs = Infinity, nodes: maxNodes = Infinity, onInfo } = {}) {
    this.pos = pos;
    this.nodes = 0;
    this.stopped = false;
    this.startTime = now();
    this.deadline = this.startTime + timeMs;
    this.maxNodes = maxNodes;
    this.canStop = false; // depth 1 always finishes, so there is always a sensible move
    this.killers.fill(0);
    for (let i = 0; i < this.history.length; i++) this.history[i] >>= 2;

    const legal = pos.legalMoves();
    if (legal.length === 0) {
      return { bestMove: null, score: pos.inCheck() ? -MATE : 0, mate: pos.inCheck() ? 0 : null, pv: [], depth: 0, nodes: 0, timeMs: 0 };
    }

    let best = { move: legal[0], score: 0, pv: [legal[0]], depth: 0 };
    let previous = 0;
    for (let depth = 1; depth <= Math.min(maxDepth, MAX_PLY - 1); depth++) {
      let score;
      if (depth >= 5 && !isMateScore(previous)) {
        // Aspiration window: expect a score near the last one, widen if wrong.
        let delta = 35;
        let alpha = previous - delta;
        let beta = previous + delta;
        for (;;) {
          score = this.negamax(depth, alpha, beta, 0, false);
          if (this.stopped) break;
          if (score <= alpha) alpha = Math.max(-INFINITY, alpha - delta);
          else if (score >= beta) beta = Math.min(INFINITY, beta + delta);
          else break;
          delta *= 2;
        }
      } else {
        score = this.negamax(depth, -INFINITY, INFINITY, 0, false);
      }
      if (this.stopped) break; // an unfinished depth can't be trusted
      this.canStop = true;

      previous = score;
      const pv = Array.from(this.pvTable.subarray(0, this.pvLength[0]));
      best = { move: pv[0] ?? best.move, score, pv: pv.length ? pv : [best.move], depth };
      onInfo?.(this.report(best));

      const elapsed = now() - this.startTime;
      if (legal.length === 1 && depth >= 4) break; // only one move: no need to think
      if (isMateScore(score) && MATE - Math.abs(score) <= depth) break; // forced mate found
      if (elapsed > timeMs * 0.5) break; // the next depth would likely run out of time
    }
    return this.report(best);
  }

  // Scores every legal move with its own full search to `depth`, for the
  // weaker playing levels, which pick among them with some randomness.
  scoreRootMoves(pos, depth) {
    this.pos = pos;
    this.nodes = 0;
    this.stopped = false;
    this.deadline = Infinity;
    this.maxNodes = Infinity;
    this.canStop = false;
    return pos.legalMoves().map((move) => {
      pos.makeMove(move);
      const score = -this.negamax(depth - 1, -INFINITY, INFINITY, 1, false);
      pos.unmakeMove();
      return { move, score };
    });
  }

  report({ move, score, pv, depth }) {
    const elapsed = now() - this.startTime;
    let mate = null;
    if (isMateScore(score)) {
      const plies = MATE - Math.abs(score);
      mate = Math.sign(score) * Math.ceil(plies / 2);
    }
    return {
      bestMove: move ? moveToUci(move) : null,
      score,
      mate,
      pv: pv.map(moveToUci),
      depth,
      nodes: this.nodes,
      timeMs: Math.round(elapsed),
      nps: elapsed > 0 ? Math.round((this.nodes / elapsed) * 1000) : 0,
    };
  }

  checkLimits() {
    if (this.canStop && (now() >= this.deadline || this.nodes >= this.maxNodes)) this.stopped = true;
  }

  negamax(depth, alpha, beta, ply, afterNullMove) {
    const pos = this.pos;
    const tt = this.tt;
    this.pvLength[ply] = ply;
    if ((++this.nodes & 2047) === 0) this.checkLimits();
    if (this.stopped) return 0;
    const pvNode = beta - alpha > 1;

    if (ply > 0) {
      // A repeated position or the fifty-move rule is a draw.
      if (pos.halfmoveClock >= 100 || pos.repetitionCount() >= 2) return 0;
      if (ply >= MAX_PLY - 1) return evaluate(pos);
      // Mate distance pruning: no line from here beats a mate already found nearer the root.
      alpha = Math.max(alpha, -MATE + ply);
      beta = Math.min(beta, MATE - ply - 1);
      if (alpha >= beta) return alpha;
    }

    const inCheck = pos.inCheck();
    if (inCheck) depth++; // look further when in check
    if (depth <= 0) return this.quiesce(alpha, beta, ply);

    // Transposition table: reuse an earlier result for this position if it
    // was searched deeply enough, or at least try its best move first.
    let ttMove = 0;
    const slot = pos.hashLo & tt.mask;
    if (tt.flags[slot] && tt.keys[slot] === pos.hashHi) {
      ttMove = tt.moves[slot];
      if (!pvNode && tt.depths[slot] >= depth) {
        const score = fromTT(tt.scores[slot], ply);
        const flag = tt.flags[slot];
        if (flag === TT_EXACT || (flag === TT_LOWER && score >= beta) || (flag === TT_UPPER && score <= alpha)) {
          return score;
        }
      }
    }

    let staticEval = 0;
    if (!inCheck) {
      staticEval = evaluate(pos);
      if (!pvNode && !isMateScore(beta)) {
        // Reverse futility: so far ahead that a shallow search won't change it.
        if (depth <= 4 && staticEval - 90 * depth >= beta) return staticEval;
        // Null move: if passing still leaves us at or above beta, a real move
        // would too. Not done without pieces, where passing could be best
        // (zugzwang).
        if (!afterNullMove && depth >= 3 && staticEval >= beta && hasPieces(pos)) {
          const reduction = depth >= 7 ? 3 : 2;
          pos.makeNullMove();
          const score = -this.negamax(depth - 1 - reduction, -beta, -beta + 1, ply + 1, true);
          pos.unmakeNullMove();
          if (this.stopped) return 0;
          if (score >= beta) return isMateScore(score) ? beta : score;
        }
      }
    }

    // Futility: near the leaves, quiet moves can't lift a hopeless position.
    const futile = !pvNode && !inCheck && depth <= 2 && staticEval + 150 * depth <= alpha;

    const moves = this.moveLists[ply];
    const scores = this.scoreLists[ply];
    moves.length = 0;
    pos.generatePseudoLegalMoves(moves);
    this.scoreMoves(moves, scores, ttMove, ply);

    const killer1 = this.killers[ply * 2];
    const killer2 = this.killers[ply * 2 + 1];
    const alphaAtStart = alpha;
    let bestScore = -INFINITY;
    let bestMove = 0;
    let legal = 0;

    for (let i = 0; i < moves.length; i++) {
      const move = pickNext(moves, scores, i);
      const quiet = !(move & FLAG_CAPTURE) && !movePromotion(move);
      const piece = pos.board[moveFrom(move)];
      if (!pos.makeMove(move, !inCheck)) continue;
      legal++;
      const givesCheck = pos.inCheck();

      if (futile && legal > 1 && quiet && !givesCheck) {
        pos.unmakeMove();
        continue;
      }

      let score;
      if (legal === 1) {
        score = -this.negamax(depth - 1, -beta, -alpha, ply + 1, false);
      } else {
        // Late move reductions: moves ordered late are rarely best, so search
        // them shallower first and only re-search if they surprise us.
        let reduction = 0;
        if (depth >= 3 && legal > 3 && quiet && !inCheck && !givesCheck && move !== killer1 && move !== killer2) {
          reduction = legal > 10 ? 2 : 1;
          if (pvNode && reduction > 0) reduction--;
          reduction = Math.min(reduction, depth - 2);
        }
        // Principal variation search: prove the move is no better with a null
        // window, and only search it fully if it is.
        score = -this.negamax(depth - 1 - reduction, -alpha - 1, -alpha, ply + 1, false);
        if (reduction > 0 && score > alpha) score = -this.negamax(depth - 1, -alpha - 1, -alpha, ply + 1, false);
        if (score > alpha && score < beta) score = -this.negamax(depth - 1, -beta, -alpha, ply + 1, false);
      }
      pos.unmakeMove();
      if (this.stopped) return 0;

      if (score > bestScore) {
        bestScore = score;
        bestMove = move;
        if (score > alpha) {
          alpha = score;
          this.updatePv(ply, move);
          if (score >= beta) {
            if (quiet) {
              if (move !== killer1) {
                this.killers[ply * 2 + 1] = killer1;
                this.killers[ply * 2] = move;
              }
              const h = piece * 128 + moveTo(move);
              this.history[h] = Math.min(this.history[h] + depth * depth, 500000);
            }
            break;
          }
        }
      }
    }

    if (legal === 0) return inCheck ? -MATE + ply : 0; // checkmate or stalemate

    const flag = bestScore >= beta ? TT_LOWER : bestScore > alphaAtStart ? TT_EXACT : TT_UPPER;
    if (tt.depths[slot] <= depth || tt.keys[slot] !== pos.hashHi || flag === TT_EXACT) {
      tt.keys[slot] = pos.hashHi;
      tt.moves[slot] = bestMove;
      tt.scores[slot] = toTT(bestScore, ply);
      tt.depths[slot] = depth;
      tt.flags[slot] = flag;
    }
    return bestScore;
  }

  // Searches only captures (or every move when in check) until the position
  // is quiet, then trusts the static evaluation. "Stand pat": the side to move
  // can usually decline to capture, so the evaluation is a lower bound.
  quiesce(alpha, beta, ply) {
    const pos = this.pos;
    this.pvLength[ply] = ply;
    if ((++this.nodes & 2047) === 0) this.checkLimits();
    if (this.stopped) return 0;
    if (ply >= MAX_PLY - 1) return evaluate(pos);

    const inCheck = pos.inCheck();
    let bestScore;
    if (inCheck) {
      bestScore = -MATE + ply;
    } else {
      // Lazy: far outside the window, material and tables are enough.
      bestScore = evaluateLazy(pos, alpha, beta);
      if (bestScore >= beta) return bestScore;
      if (bestScore > alpha) alpha = bestScore;
    }

    const moves = this.moveLists[ply];
    const scores = this.scoreLists[ply];
    moves.length = 0;
    pos.generatePseudoLegalMoves(moves, !inCheck);
    this.scoreMoves(moves, scores, 0, ply);

    let legal = 0;
    for (let i = 0; i < moves.length; i++) {
      const move = pickNext(moves, scores, i);
      // Delta pruning: even winning this piece outright wouldn't reach alpha.
      if (!inCheck && !movePromotion(move)) {
        const victim = move & FLAG_EN_PASSANT ? PAWN : pos.board[moveTo(move)] & 7;
        if (bestScore + ORDER_VALUE[victim] + 200 < alpha) continue;
      }
      if (!pos.makeMove(move, !inCheck)) continue;
      legal++;
      const score = -this.quiesce(-beta, -alpha, ply + 1);
      pos.unmakeMove();
      if (this.stopped) return 0;
      if (score > bestScore) {
        bestScore = score;
        if (score > alpha) {
          alpha = score;
          this.updatePv(ply, move);
          if (score >= beta) break;
        }
      }
    }
    if (inCheck && legal === 0) return -MATE + ply;
    return bestScore;
  }

  // Orders moves: the remembered best move, then captures by most valuable
  // victim / least valuable attacker, promotions, killer moves (quiet moves
  // that refuted another line at this ply), then by history.
  scoreMoves(moves, scores, ttMove, ply) {
    const b = this.pos.board;
    const killer1 = this.killers[ply * 2];
    const killer2 = this.killers[ply * 2 + 1];
    scores.length = moves.length;
    for (let i = 0; i < moves.length; i++) {
      const move = moves[i];
      let score;
      if (move === ttMove) {
        score = 3000000;
      } else if (move & FLAG_CAPTURE) {
        const victim = move & FLAG_EN_PASSANT ? PAWN : b[moveTo(move)] & 7;
        const attacker = b[moveFrom(move)] & 7;
        score = 2000000 + ORDER_VALUE[victim] * 10 - ORDER_VALUE[attacker] + ORDER_VALUE[movePromotion(move)];
      } else if (movePromotion(move)) {
        score = 1900000 + ORDER_VALUE[movePromotion(move)];
      } else if (move === killer1) {
        score = 1800000;
      } else if (move === killer2) {
        score = 1700000;
      } else {
        score = this.history[b[moveFrom(move)] * 128 + moveTo(move)];
      }
      scores[i] = score;
    }
  }

  updatePv(ply, move) {
    const row = ply * MAX_PLY;
    const childRow = (ply + 1) * MAX_PLY;
    this.pvTable[row + ply] = move;
    const childLength = this.pvLength[ply + 1];
    for (let i = ply + 1; i < childLength; i++) this.pvTable[row + i] = this.pvTable[childRow + i];
    this.pvLength[ply] = Math.max(childLength, ply + 1);
  }
}

// Moves the highest-scored remaining move to index i and returns it.
function pickNext(moves, scores, i) {
  let best = i;
  for (let j = i + 1; j < moves.length; j++) if (scores[j] > scores[best]) best = j;
  if (best !== i) {
    const m = moves[i]; moves[i] = moves[best]; moves[best] = m;
    const s = scores[i]; scores[i] = scores[best]; scores[best] = s;
  }
  return moves[i];
}

// Does the side to move have anything besides pawns and its king?
function hasPieces(pos) {
  const b = pos.board;
  const us = pos.turn;
  for (let sq = 0; sq < 120; sq++) {
    if (sq & 0x88) { sq += 7; continue; }
    const piece = b[sq];
    if (piece !== EMPTY && (piece & 8) === us) {
      const type = piece & 7;
      if (type === KNIGHT || type === BISHOP || type === ROOK || type === QUEEN) return true;
    }
  }
  return false;
}

// Mate scores are stored relative to the position, not the root, so they
// stay correct when the position is reached at a different depth.
function toTT(score, ply) {
  if (score >= MATE - MAX_PLY) return score + ply;
  if (score <= -(MATE - MAX_PLY)) return score - ply;
  return score;
}

function fromTT(score, ply) {
  if (score >= MATE - MAX_PLY) return score - ply;
  if (score <= -(MATE - MAX_PLY)) return score + ply;
  return score;
}
