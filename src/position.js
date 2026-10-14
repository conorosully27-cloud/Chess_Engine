import {
  WHITE, BLACK, EMPTY, PAWN, KNIGHT, BISHOP, ROOK, QUEEN, KING, PIECE_CHARS,
  A1, E1, H1, A8, E8, H8,
  CASTLE_WK, CASTLE_WQ, CASTLE_BK, CASTLE_BQ,
  KNIGHT_OFFSETS, BISHOP_OFFSETS, ROOK_OFFSETS, KING_OFFSETS,
  FLAG_CAPTURE, FLAG_EN_PASSANT, FLAG_CASTLE, FLAG_DOUBLE_PUSH,
  encodeMove, fileOf, rankOf, makeSquare, parseSquare, squareName,
} from './board.js';
import {
  Z_PIECE_LO, Z_PIECE_HI, Z_CASTLING_LO, Z_CASTLING_HI, Z_EP_LO, Z_EP_HI, Z_SIDE_LO, Z_SIDE_HI,
} from './zobrist.js';
import { moveToSan, moveToUci, parseMove } from './notation.js';

export const START_FEN = 'rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1';

// Castling rights are AND'ed with CASTLING_MASK[from] & CASTLING_MASK[to] on
// every move, so moving a king or rook, or capturing a rook on its home
// square, removes the matching rights.
const CASTLING_MASK = new Uint8Array(128).fill(15);
CASTLING_MASK[A1] = 15 & ~CASTLE_WQ;
CASTLING_MASK[H1] = 15 & ~CASTLE_WK;
CASTLING_MASK[E1] = 15 & ~(CASTLE_WK | CASTLE_WQ);
CASTLING_MASK[A8] = 15 & ~CASTLE_BQ;
CASTLING_MASK[H8] = 15 & ~CASTLE_BK;
CASTLING_MASK[E8] = 15 & ~(CASTLE_BK | CASTLE_BQ);

// Undo stack: a fixed-size record of Int32s per move made, so make/unmake
// allocate nothing. Each record holds what the move destroys plus the hash of
// the position before it (used by the repetition rule).
const U_MOVE = 0, U_CAPTURED = 1, U_CASTLING = 2, U_EP = 3, U_HALFMOVE = 4, U_HASH_LO = 5, U_HASH_HI = 6;
const UNDO_SIZE = 8;

export class Position {
  constructor(fen = START_FEN) {
    this.board = new Int8Array(128);
    this.turn = WHITE;
    this.castling = 0;
    // Set only when the side to move can legally capture en passant, so that
    // positions differing only in a useless en passant square count as the
    // same position for repetition (as FIDE rules require).
    this.epSquare = -1;
    this.halfmoveClock = 0;
    this.fullmoveNumber = 1;
    this.kingSquare = [-1, -1]; // indexed by color >> 3
    this.hashLo = 0;
    this.hashHi = 0;
    this.ply = 0; // moves made since the FEN was loaded
    this.undoStack = new Int32Array(256 * UNDO_SIZE);
    this._loadFen(fen);
  }

  // ---------------------------------------------------------------- FEN

  _loadFen(fen) {
    const fail = (why) => {
      throw new Error(`Invalid FEN "${fen}": ${why}`);
    };
    const fields = String(fen).trim().split(/\s+/);
    if (fields.length < 4 || fields.length > 6) fail('expected 4 to 6 fields');
    const [placement, side, castling, ep, halfmove = '0', fullmove = '1'] = fields;
    const b = this.board;

    const rows = placement.split('/');
    if (rows.length !== 8) fail('the board must have 8 ranks');
    const kingCount = [0, 0];
    for (let i = 0; i < 8; i++) {
      const rank = 7 - i;
      let file = 0;
      for (const ch of rows[i]) {
        if (ch >= '1' && ch <= '8') {
          file += ch.charCodeAt(0) - 48;
          if (file > 8) fail(`rank ${rank + 1} has more than 8 squares`);
          continue;
        }
        const piece = PIECE_CHARS.indexOf(ch);
        if (piece <= 0) fail(`unknown piece "${ch}"`);
        if (file >= 8) fail(`rank ${rank + 1} has more than 8 squares`);
        if ((piece & 7) === PAWN && (rank === 0 || rank === 7)) fail('pawns cannot be on the first or last rank');
        const sq = makeSquare(file, rank);
        b[sq] = piece;
        if ((piece & 7) === KING) {
          kingCount[piece >> 3]++;
          this.kingSquare[piece >> 3] = sq;
        }
        file++;
      }
      if (file !== 8) fail(`rank ${rank + 1} has ${file} squares`);
    }
    if (kingCount[0] !== 1 || kingCount[1] !== 1) fail('each side must have exactly one king');

    if (side !== 'w' && side !== 'b') fail('side to move must be "w" or "b"');
    this.turn = side === 'w' ? WHITE : BLACK;
    const them = this.turn ^ 8;

    if (!/^(-|K?Q?k?q?)$/.test(castling)) fail(`bad castling field "${castling}"`);
    let rights = 0;
    if (castling.includes('K')) rights |= CASTLE_WK;
    if (castling.includes('Q')) rights |= CASTLE_WQ;
    if (castling.includes('k')) rights |= CASTLE_BK;
    if (castling.includes('q')) rights |= CASTLE_BQ;
    // Drop rights whose king or rook isn't on its starting square, so we can
    // never generate an impossible castle from a sloppy FEN.
    if (b[E1] !== (WHITE | KING)) rights &= ~(CASTLE_WK | CASTLE_WQ);
    if (b[H1] !== (WHITE | ROOK)) rights &= ~CASTLE_WK;
    if (b[A1] !== (WHITE | ROOK)) rights &= ~CASTLE_WQ;
    if (b[E8] !== (BLACK | KING)) rights &= ~(CASTLE_BK | CASTLE_BQ);
    if (b[H8] !== (BLACK | ROOK)) rights &= ~CASTLE_BK;
    if (b[A8] !== (BLACK | ROOK)) rights &= ~CASTLE_BQ;
    this.castling = rights;

    if (ep !== '-') {
      const sq = parseSquare(ep);
      if (sq < 0 || rankOf(sq) !== (this.turn === WHITE ? 5 : 2)) fail(`bad en passant square "${ep}"`);
      // The pawn that just moved two squares must be in front of the square,
      // with the square it passed over and its starting square empty.
      const forward = this.turn === WHITE ? 16 : -16;
      if (b[sq - forward] !== (them | PAWN) || b[sq] !== EMPTY || b[sq + forward] !== EMPTY) {
        fail(`en passant square "${ep}" does not follow a double pawn push`);
      }
      this.epSquare = sq;
    }

    if (!/^\d+$/.test(halfmove) || !/^\d+$/.test(fullmove)) fail('move counters must be non-negative integers');
    this.halfmoveClock = Number(halfmove);
    this.fullmoveNumber = Math.max(1, Number(fullmove));

    if (this.isAttacked(this.kingSquare[them >> 3], this.turn)) fail('the side not to move is in check');
    if (this.epSquare >= 0 && !this._enPassantCapturable(this.epSquare)) this.epSquare = -1;

    this._computeHash();
  }

  fen() {
    let placement = '';
    for (let rank = 7; rank >= 0; rank--) {
      let empty = 0;
      for (let file = 0; file < 8; file++) {
        const piece = this.board[makeSquare(file, rank)];
        if (piece === EMPTY) {
          empty++;
        } else {
          if (empty) placement += empty;
          empty = 0;
          placement += PIECE_CHARS[piece];
        }
      }
      if (empty) placement += empty;
      if (rank > 0) placement += '/';
    }
    let castling = '';
    if (this.castling & CASTLE_WK) castling += 'K';
    if (this.castling & CASTLE_WQ) castling += 'Q';
    if (this.castling & CASTLE_BK) castling += 'k';
    if (this.castling & CASTLE_BQ) castling += 'q';
    const ep = this.epSquare >= 0 ? squareName(this.epSquare) : '-';
    const side = this.turn === WHITE ? 'w' : 'b';
    return `${placement} ${side} ${castling || '-'} ${ep} ${this.halfmoveClock} ${this.fullmoveNumber}`;
  }

  _computeHash() {
    let lo = 0, hi = 0;
    for (let sq = 0; sq < 120; sq++) {
      if (sq & 0x88) { sq += 7; continue; }
      const piece = this.board[sq];
      if (piece !== EMPTY) {
        lo ^= Z_PIECE_LO[piece * 128 + sq];
        hi ^= Z_PIECE_HI[piece * 128 + sq];
      }
    }
    lo ^= Z_CASTLING_LO[this.castling];
    hi ^= Z_CASTLING_HI[this.castling];
    if (this.epSquare >= 0) {
      lo ^= Z_EP_LO[fileOf(this.epSquare)];
      hi ^= Z_EP_HI[fileOf(this.epSquare)];
    }
    if (this.turn === BLACK) {
      lo ^= Z_SIDE_LO;
      hi ^= Z_SIDE_HI;
    }
    this.hashLo = lo;
    this.hashHi = hi;
  }

  // 64-bit Zobrist hash as a 16-character hex string.
  hashKey() {
    return (this.hashHi >>> 0).toString(16).padStart(8, '0') + (this.hashLo >>> 0).toString(16).padStart(8, '0');
  }

  // ---------------------------------------------------------------- attacks

  // Is `sq` attacked by any piece of color `by`?
  isAttacked(sq, by) {
    const b = this.board;

    // Pawns attack diagonally forward, so look diagonally backward from sq.
    const pawn = by | PAWN;
    const behind = by === WHITE ? -16 : 16;
    let s = sq + behind - 1;
    if ((s & 0x88) === 0 && b[s] === pawn) return true;
    s = sq + behind + 1;
    if ((s & 0x88) === 0 && b[s] === pawn) return true;

    const knight = by | KNIGHT;
    for (let i = 0; i < 8; i++) {
      s = sq + KNIGHT_OFFSETS[i];
      if ((s & 0x88) === 0 && b[s] === knight) return true;
    }

    const king = by | KING;
    for (let i = 0; i < 8; i++) {
      s = sq + KING_OFFSETS[i];
      if ((s & 0x88) === 0 && b[s] === king) return true;
    }

    const queen = by | QUEEN;
    const bishop = by | BISHOP;
    for (let i = 0; i < 4; i++) {
      const step = BISHOP_OFFSETS[i];
      for (s = sq + step; (s & 0x88) === 0; s += step) {
        const piece = b[s];
        if (piece === EMPTY) continue;
        if (piece === bishop || piece === queen) return true;
        break;
      }
    }

    const rook = by | ROOK;
    for (let i = 0; i < 4; i++) {
      const step = ROOK_OFFSETS[i];
      for (s = sq + step; (s & 0x88) === 0; s += step) {
        const piece = b[s];
        if (piece === EMPTY) continue;
        if (piece === rook || piece === queen) return true;
        break;
      }
    }

    return false;
  }

  inCheck() {
    return this.isAttacked(this.kingSquare[this.turn >> 3], this.turn ^ 8);
  }

  // ---------------------------------------------------------------- move generation

  // Appends every pseudo-legal move (one that obeys how the pieces move but
  // may leave the mover's king in check) to `moves`. Castling is fully
  // checked here, since its rules about attacked squares go beyond king safety.
  generatePseudoLegalMoves(moves = []) {
    const b = this.board;
    const us = this.turn;
    for (let sq = 0; sq < 120; sq++) {
      if (sq & 0x88) { sq += 7; continue; }
      const piece = b[sq];
      if (piece === EMPTY || (piece & 8) !== us) continue;
      switch (piece & 7) {
        case PAWN: this._pawnMoves(sq, moves); break;
        case KNIGHT: this._leaperMoves(sq, KNIGHT_OFFSETS, moves); break;
        case BISHOP: this._sliderMoves(sq, BISHOP_OFFSETS, moves); break;
        case ROOK: this._sliderMoves(sq, ROOK_OFFSETS, moves); break;
        case QUEEN: this._sliderMoves(sq, KING_OFFSETS, moves); break;
        case KING: this._leaperMoves(sq, KING_OFFSETS, moves); break;
      }
    }
    this._castlingMoves(moves);
    return moves;
  }

  legalMoves() {
    const legal = [];
    for (const move of this.generatePseudoLegalMoves()) {
      if (this.makeMove(move)) {
        this.unmakeMove();
        legal.push(move);
      }
    }
    return legal;
  }

  hasLegalMove() {
    for (const move of this.generatePseudoLegalMoves()) {
      if (this.makeMove(move)) {
        this.unmakeMove();
        return true;
      }
    }
    return false;
  }

  isLegal(move) {
    return this.legalMoves().includes(move);
  }

  _pawnMoves(from, moves) {
    const b = this.board;
    const us = this.turn;
    const forward = us === WHITE ? 16 : -16;
    const startRank = us === WHITE ? 1 : 6;
    const lastRank = us === WHITE ? 7 : 0;

    // Pawns are never on the last rank, so one step forward is on the board.
    const to = from + forward;
    if (b[to] === EMPTY) {
      if (rankOf(to) === lastRank) {
        this._addPromotions(from, to, 0, moves);
      } else {
        moves.push(encodeMove(from, to));
        if (rankOf(from) === startRank && b[to + forward] === EMPTY) {
          moves.push(encodeMove(from, to + forward, 0, FLAG_DOUBLE_PUSH));
        }
      }
    }

    for (let side = -1; side <= 1; side += 2) {
      const target = to + side;
      if (target & 0x88) continue;
      const victim = b[target];
      if (victim !== EMPTY && (victim & 8) !== us) {
        if (rankOf(target) === lastRank) this._addPromotions(from, target, FLAG_CAPTURE, moves);
        else moves.push(encodeMove(from, target, 0, FLAG_CAPTURE));
      } else if (target === this.epSquare) {
        moves.push(encodeMove(from, target, 0, FLAG_CAPTURE | FLAG_EN_PASSANT));
      }
    }
  }

  _addPromotions(from, to, flags, moves) {
    // Queen first: it's almost always the best, which helps search ordering later.
    moves.push(encodeMove(from, to, QUEEN, flags));
    moves.push(encodeMove(from, to, ROOK, flags));
    moves.push(encodeMove(from, to, BISHOP, flags));
    moves.push(encodeMove(from, to, KNIGHT, flags));
  }

  _leaperMoves(from, offsets, moves) {
    const b = this.board;
    const us = this.turn;
    for (let i = 0; i < offsets.length; i++) {
      const to = from + offsets[i];
      if (to & 0x88) continue;
      const target = b[to];
      if (target === EMPTY) moves.push(encodeMove(from, to));
      else if ((target & 8) !== us) moves.push(encodeMove(from, to, 0, FLAG_CAPTURE));
    }
  }

  _sliderMoves(from, directions, moves) {
    const b = this.board;
    const us = this.turn;
    for (let i = 0; i < directions.length; i++) {
      const step = directions[i];
      for (let to = from + step; (to & 0x88) === 0; to += step) {
        const target = b[to];
        if (target === EMPTY) {
          moves.push(encodeMove(from, to));
          continue;
        }
        if ((target & 8) !== us) moves.push(encodeMove(from, to, 0, FLAG_CAPTURE));
        break;
      }
    }
  }

  _castlingMoves(moves) {
    const b = this.board;
    const us = this.turn;
    const them = us ^ 8;
    const kingSide = us === WHITE ? CASTLE_WK : CASTLE_BK;
    const queenSide = us === WHITE ? CASTLE_WQ : CASTLE_BQ;
    if (!(this.castling & (kingSide | queenSide))) return;
    // Rights guarantee the king is on e1/e8 and the rook is on its corner.
    const e = us === WHITE ? E1 : E8;
    if (this.isAttacked(e, them)) return; // no castling out of check
    // The squares between king and rook must be empty, and the king may not
    // pass through or land on an attacked square (b1/b8 may be attacked).
    if ((this.castling & kingSide) && b[e + 1] === EMPTY && b[e + 2] === EMPTY &&
        !this.isAttacked(e + 1, them) && !this.isAttacked(e + 2, them)) {
      moves.push(encodeMove(e, e + 2, 0, FLAG_CASTLE));
    }
    if ((this.castling & queenSide) && b[e - 1] === EMPTY && b[e - 2] === EMPTY && b[e - 3] === EMPTY &&
        !this.isAttacked(e - 1, them) && !this.isAttacked(e - 2, them)) {
      moves.push(encodeMove(e, e - 2, 0, FLAG_CASTLE));
    }
  }

  // Can the side to move legally capture en passant onto epSq? Tries each
  // capture on the board directly, which catches the rare case where removing
  // both pawns from a rank exposes the king to a rook or queen.
  _enPassantCapturable(epSq) {
    const b = this.board;
    const us = this.turn;
    const pawn = us | PAWN;
    const victimSq = us === WHITE ? epSq - 16 : epSq + 16;
    const victim = b[victimSq];
    const kingSq = this.kingSquare[us >> 3];
    for (let side = -1; side <= 1; side += 2) {
      const from = victimSq + side;
      if ((from & 0x88) || b[from] !== pawn) continue;
      b[from] = EMPTY;
      b[victimSq] = EMPTY;
      b[epSq] = pawn;
      const legal = !this.isAttacked(kingSq, us ^ 8);
      b[from] = pawn;
      b[victimSq] = victim;
      b[epSq] = EMPTY;
      if (legal) return true;
    }
    return false;
  }

  // ---------------------------------------------------------------- make / unmake

  // Plays a pseudo-legal move. If it would leave the mover's king in check it
  // is taken back and false is returned; otherwise returns true.
  makeMove(move) {
    const us = this.turn;
    this._doMove(move);
    if (this.isAttacked(this.kingSquare[us >> 3], us ^ 8)) {
      this.unmakeMove();
      return false;
    }
    return true;
  }

  _doMove(move) {
    const b = this.board;
    const us = this.turn;
    const from = move & 0x7f;
    const to = (move >> 7) & 0x7f;
    const promotion = (move >> 14) & 7;
    const piece = b[from];
    let captured = b[to];
    let lo = this.hashLo, hi = this.hashHi;

    if ((this.ply + 1) * UNDO_SIZE > this.undoStack.length) {
      const bigger = new Int32Array(this.undoStack.length * 2);
      bigger.set(this.undoStack);
      this.undoStack = bigger;
    }
    const u = this.undoStack;
    const base = this.ply * UNDO_SIZE;
    u[base + U_MOVE] = move;
    u[base + U_CASTLING] = this.castling;
    u[base + U_EP] = this.epSquare;
    u[base + U_HALFMOVE] = this.halfmoveClock;
    u[base + U_HASH_LO] = lo;
    u[base + U_HASH_HI] = hi;
    this.ply++;

    if (this.epSquare >= 0) {
      lo ^= Z_EP_LO[fileOf(this.epSquare)];
      hi ^= Z_EP_HI[fileOf(this.epSquare)];
    }
    lo ^= Z_CASTLING_LO[this.castling];
    hi ^= Z_CASTLING_HI[this.castling];

    b[from] = EMPTY;
    lo ^= Z_PIECE_LO[piece * 128 + from];
    hi ^= Z_PIECE_HI[piece * 128 + from];

    if (move & FLAG_EN_PASSANT) {
      // The captured pawn sits beside the mover, not on the destination square.
      const victimSq = us === WHITE ? to - 16 : to + 16;
      captured = b[victimSq];
      b[victimSq] = EMPTY;
      lo ^= Z_PIECE_LO[captured * 128 + victimSq];
      hi ^= Z_PIECE_HI[captured * 128 + victimSq];
    } else if (captured !== EMPTY) {
      lo ^= Z_PIECE_LO[captured * 128 + to];
      hi ^= Z_PIECE_HI[captured * 128 + to];
    }
    u[base + U_CAPTURED] = captured;

    const placed = promotion ? (us | promotion) : piece;
    b[to] = placed;
    lo ^= Z_PIECE_LO[placed * 128 + to];
    hi ^= Z_PIECE_HI[placed * 128 + to];

    if (move & FLAG_CASTLE) {
      // King goes e->g with rook h->f, or e->c with rook a->d.
      const rookFrom = to > from ? to + 1 : to - 2;
      const rookTo = to > from ? to - 1 : to + 1;
      const rook = b[rookFrom];
      b[rookFrom] = EMPTY;
      b[rookTo] = rook;
      lo ^= Z_PIECE_LO[rook * 128 + rookFrom] ^ Z_PIECE_LO[rook * 128 + rookTo];
      hi ^= Z_PIECE_HI[rook * 128 + rookFrom] ^ Z_PIECE_HI[rook * 128 + rookTo];
    }

    if ((piece & 7) === KING) this.kingSquare[us >> 3] = to;

    this.castling &= CASTLING_MASK[from] & CASTLING_MASK[to];
    lo ^= Z_CASTLING_LO[this.castling];
    hi ^= Z_CASTLING_HI[this.castling];

    this.halfmoveClock = (piece & 7) === PAWN || captured !== EMPTY ? 0 : this.halfmoveClock + 1;
    if (us === BLACK) this.fullmoveNumber++;
    this.turn = us ^ 8;
    lo ^= Z_SIDE_LO;
    hi ^= Z_SIDE_HI;

    this.epSquare = -1;
    if (move & FLAG_DOUBLE_PUSH) {
      const passed = (from + to) >> 1;
      if (this._enPassantCapturable(passed)) {
        this.epSquare = passed;
        lo ^= Z_EP_LO[fileOf(passed)];
        hi ^= Z_EP_HI[fileOf(passed)];
      }
    }

    this.hashLo = lo;
    this.hashHi = hi;
  }

  // Takes back the last move made.
  unmakeMove() {
    if (this.ply === 0) throw new Error('No move to take back');
    this.ply--;
    const b = this.board;
    const u = this.undoStack;
    const base = this.ply * UNDO_SIZE;
    const move = u[base + U_MOVE];
    const captured = u[base + U_CAPTURED];
    this.castling = u[base + U_CASTLING];
    this.epSquare = u[base + U_EP];
    this.halfmoveClock = u[base + U_HALFMOVE];
    this.hashLo = u[base + U_HASH_LO];
    this.hashHi = u[base + U_HASH_HI];

    this.turn ^= 8;
    const us = this.turn;
    if (us === BLACK) this.fullmoveNumber--;

    const from = move & 0x7f;
    const to = (move >> 7) & 0x7f;
    const piece = (move >> 14) & 7 ? (us | PAWN) : b[to];
    b[from] = piece;
    if (move & FLAG_EN_PASSANT) {
      b[to] = EMPTY;
      b[us === WHITE ? to - 16 : to + 16] = captured;
    } else {
      b[to] = captured;
    }

    if (move & FLAG_CASTLE) {
      const rookFrom = to > from ? to + 1 : to - 2;
      const rookTo = to > from ? to - 1 : to + 1;
      b[rookFrom] = b[rookTo];
      b[rookTo] = EMPTY;
    }

    if ((piece & 7) === KING) this.kingSquare[us >> 3] = from;
  }

  // Moves made since the FEN was loaded, oldest first.
  history() {
    const moves = [];
    for (let i = 0; i < this.ply; i++) moves.push(this.undoStack[i * UNDO_SIZE + U_MOVE]);
    return moves;
  }

  // ---------------------------------------------------------------- notation

  toSan(move) {
    return moveToSan(this, move);
  }

  toUci(move) {
    return moveToUci(move);
  }

  // Parses a move in SAN ("Nf3", "exd5", "O-O", "e8=Q") or UCI ("g1f3",
  // "e7e8q") notation. Returns the move, or null if it isn't legal here.
  parseMove(text) {
    return parseMove(this, text);
  }

  // Parses and plays a move given as SAN or UCI. Throws if it isn't legal.
  play(text) {
    const move = this.parseMove(text);
    if (move === null) throw new Error(`Illegal or ambiguous move "${text}" in ${this.fen()}`);
    this.makeMove(move);
    return move;
  }

  // ---------------------------------------------------------------- game end

  isCheckmate() {
    return this.inCheck() && !this.hasLegalMove();
  }

  isStalemate() {
    return !this.inCheck() && !this.hasLegalMove();
  }

  // True when neither side can possibly checkmate: bare kings, a single
  // minor piece, or only bishops that all stand on the same color of square.
  isInsufficientMaterial() {
    let knights = 0, lightBishops = 0, darkBishops = 0;
    for (let sq = 0; sq < 120; sq++) {
      if (sq & 0x88) { sq += 7; continue; }
      switch (this.board[sq] & 7) {
        case PAWN:
        case ROOK:
        case QUEEN:
          return false;
        case KNIGHT:
          knights++;
          break;
        case BISHOP:
          if ((fileOf(sq) + rankOf(sq)) & 1) lightBishops++;
          else darkBishops++;
          break;
      }
    }
    const minors = knights + lightBishops + darkBishops;
    if (minors <= 1) return true;
    return knights === 0 && (lightBishops === 0 || darkBishops === 0);
  }

  // 50 moves by each side with no capture or pawn move.
  isFiftyMoveRule() {
    return this.halfmoveClock >= 100;
  }

  // How many times the current position has occurred in the moves played
  // since the FEN was loaded (including now). Only positions since the last
  // capture or pawn move can match, since those can never recur.
  repetitionCount() {
    let count = 1;
    const u = this.undoStack;
    const stop = Math.max(0, this.ply - this.halfmoveClock);
    for (let i = this.ply - 2; i >= stop; i -= 2) {
      const base = i * UNDO_SIZE;
      if (u[base + U_HASH_LO] === this.hashLo && u[base + U_HASH_HI] === this.hashHi) count++;
    }
    return count;
  }

  isThreefoldRepetition() {
    return this.repetitionCount() >= 3;
  }

  // Overall game state. Threefold repetition and the fifty-move rule end the
  // game automatically (as most chess apps do) rather than needing a claim.
  // Checkmate is tested first, so a mate on the 100th half-move still wins.
  status() {
    if (!this.hasLegalMove()) {
      if (this.inCheck()) {
        return { isOver: true, result: this.turn === WHITE ? '0-1' : '1-0', reason: 'checkmate' };
      }
      return { isOver: true, result: '1/2-1/2', reason: 'stalemate' };
    }
    if (this.isInsufficientMaterial()) return { isOver: true, result: '1/2-1/2', reason: 'insufficient material' };
    if (this.isFiftyMoveRule()) return { isOver: true, result: '1/2-1/2', reason: 'fifty-move rule' };
    if (this.isThreefoldRepetition()) return { isOver: true, result: '1/2-1/2', reason: 'threefold repetition' };
    return { isOver: false, result: '*', reason: null };
  }

  // ---------------------------------------------------------------- helpers

  // The piece on a named square, e.g. pieceAt('e1') -> { color: 'w', type: 'k' }.
  pieceAt(name) {
    const sq = parseSquare(name);
    if (sq < 0) throw new Error(`Not a square: "${name}"`);
    const piece = this.board[sq];
    if (piece === EMPTY) return null;
    return { color: piece & 8 ? 'b' : 'w', type: PIECE_CHARS[BLACK | (piece & 7)] };
  }

  // Counts leaf nodes of the legal move tree to `depth`. Comparing against
  // published counts is the standard way to verify a move generator.
  perft(depth) {
    if (depth === 0) return 1;
    let nodes = 0;
    for (const move of this.generatePseudoLegalMoves()) {
      if (!this.makeMove(move)) continue;
      nodes += depth === 1 ? 1 : this.perft(depth - 1);
      this.unmakeMove();
    }
    return nodes;
  }

  // Per-move perft counts, keyed by UCI move, for debugging mismatches.
  perftDivide(depth) {
    const result = {};
    for (const move of this.legalMoves()) {
      this.makeMove(move);
      result[moveToUci(move)] = this.perft(depth - 1);
      this.unmakeMove();
    }
    return result;
  }

  ascii() {
    let out = '';
    for (let rank = 7; rank >= 0; rank--) {
      out += `${rank + 1} `;
      for (let file = 0; file < 8; file++) out += ` ${PIECE_CHARS[this.board[makeSquare(file, rank)]]}`;
      out += '\n';
    }
    return `${out}   a b c d e f g h\n`;
  }
}
