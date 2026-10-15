// King and pawn against king, solved exactly.
//
// Opposition, key squares and the "square" of the pawn all come down to
// counting moves precisely, which a shallow search can get wrong. Instead
// this file works out every K+P v K position once (about 390,000 of them)
// by retrograde analysis, the way endgame tables are built: mark the
// positions whose result is obvious (the pawn queens safely, the defender
// takes the pawn or is stalemated), then repeat until nothing changes:
//
//   - with the attacker to move, a position is won if some move reaches a
//     won position, and drawn if every move reaches a drawn one;
//   - with the defender to move, it is drawn if some move reaches a drawn
//     position, and won if every move reaches a won one.
//
// Whatever is still undecided at the end is a draw. The attacker is always
// White here with the pawn moving up the board; probe() flips other cases.
// Squares are 0-63 (a1 = 0, h8 = 63).

const UNKNOWN = 0, DRAW = 1, WIN = 2, INVALID = 3;

let table = null; // Uint8Array, indexed by index()

// White king, black king, pawn (0-63) and side to move (0 White, 1 Black).
const index = (wk, bk, p, stm) => ((stm * 64 + wk) * 64 + bk) * 64 + p;

const file = (sq) => sq & 7;
const rank = (sq) => sq >> 3;
const adjacent = (a, b) => a !== b && Math.abs(file(a) - file(b)) <= 1 && Math.abs(rank(a) - rank(b)) <= 1;
const pawnAttacks = (p, sq) => rank(sq) === rank(p) + 1 && Math.abs(file(sq) - file(p)) === 1;

// King moves from each square.
const KING_MOVES = [];
for (let sq = 0; sq < 64; sq++) {
  const moves = [];
  for (let df = -1; df <= 1; df++) {
    for (let dr = -1; dr <= 1; dr++) {
      const f = file(sq) + df, r = rank(sq) + dr;
      if ((df || dr) && f >= 0 && f < 8 && r >= 0 && r < 8) moves.push(r * 8 + f);
    }
  }
  KING_MOVES.push(moves);
}

function initial(wk, bk, p, stm) {
  if (wk === bk || wk === p || bk === p || adjacent(wk, bk)) return INVALID;
  if (stm === 0 && pawnAttacks(p, bk)) return INVALID; // Black in check with White to move
  if (stm === 0) {
    // The pawn promotes and the new queen can't be taken.
    const q = p + 8;
    if (rank(p) === 6 && q !== wk && q !== bk && (!adjacent(bk, q) || adjacent(wk, q))) return WIN;
    return UNKNOWN;
  }
  // Black takes an unprotected pawn.
  if (adjacent(bk, p) && !adjacent(wk, p)) return DRAW;
  // Black has no legal move: stalemate (or mate, if the pawn gives check).
  const canMove = KING_MOVES[bk].some((to) => to !== p && !adjacent(to, wk) && !pawnAttacks(p, to));
  if (!canMove) return pawnAttacks(p, bk) ? WIN : DRAW;
  return UNKNOWN;
}

// Result of the position after one move from (wk, bk, p, stm), or -1 for
// an illegal move. Pawn moves from the seventh rank are left to initial().
function classify(t, wk, bk, p, stm) {
  if (stm === 0) {
    let all = DRAW;
    const step = (r) => {
      if (r === WIN) return true;
      if (r !== DRAW) all = UNKNOWN;
      return false;
    };
    for (const to of KING_MOVES[wk]) {
      if (to === p || adjacent(to, bk)) continue;
      if (step(t[index(to, bk, p, 1)])) return WIN;
    }
    if (rank(p) < 6 && p + 8 !== wk && p + 8 !== bk) {
      if (step(t[index(wk, bk, p + 8, 1)])) return WIN;
      if (rank(p) === 1 && p + 16 !== wk && p + 16 !== bk && step(t[index(wk, bk, p + 16, 1)])) return WIN;
    }
    return all;
  }
  let all = WIN;
  for (const to of KING_MOVES[bk]) {
    if (to === p || adjacent(to, wk) || pawnAttacks(p, to)) continue;
    const r = t[index(wk, to, p, 0)];
    if (r === DRAW) return DRAW;
    if (r !== WIN) all = UNKNOWN;
  }
  return all;
}

function build() {
  const t = new Uint8Array(2 * 64 * 64 * 64);
  const pending = [];
  for (let stm = 0; stm < 2; stm++) {
    for (let wk = 0; wk < 64; wk++) {
      for (let bk = 0; bk < 64; bk++) {
        for (let p = 8; p < 56; p++) {
          const r = initial(wk, bk, p, stm);
          t[index(wk, bk, p, stm)] = r;
          if (r === UNKNOWN) pending.push(index(wk, bk, p, stm));
        }
      }
    }
  }
  let changed = true;
  let open = pending;
  while (changed) {
    changed = false;
    const still = [];
    for (const i of open) {
      const p = i & 63, bk = (i >> 6) & 63, wk = (i >> 12) & 63, stm = i >> 18;
      const r = classify(t, wk, bk, p, stm);
      if (r === UNKNOWN) still.push(i);
      else {
        t[i] = r;
        changed = true;
      }
    }
    open = still;
  }
  for (const i of open) t[i] = DRAW;
  return t;
}

// Builds the table now rather than on first use (it takes a fraction of a
// second), e.g. when an engine starts.
export function initKpk() {
  table ??= build();
}

// Is this K+P v K position won for the side with the pawn? Squares are 0x88
// as on the board; `pawnIsWhite` and `whiteToMove` give the orientation.
export function kpkWin(whiteKing, blackKing, pawn, pawnIsWhite, whiteToMove) {
  initKpk();
  const to64 = (sq) => (sq >> 4) * 8 + (sq & 7);
  let wk = to64(whiteKing), bk = to64(blackKing), p = to64(pawn), stm = whiteToMove ? 0 : 1;
  if (!pawnIsWhite) {
    // Flip the board top to bottom and swap colors, so the pawn is White's.
    const flip = (sq) => sq ^ 56;
    [wk, bk] = [flip(bk), flip(wk)];
    p = flip(p);
    stm ^= 1;
  }
  return table[index(wk, bk, p, stm)] === WIN;
}
