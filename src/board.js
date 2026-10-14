// Core constants and helpers shared by the rules and (later) the engine.
//
// Board layout: 0x88 mailbox. A square index is rank * 16 + file, where rank 0
// is White's back rank and file 0 is the a-file, so a1 = 0, h1 = 7, a8 = 112
// and h8 = 119. The unused right half of each 16-wide row means a square is on
// the board exactly when (sq & 0x88) === 0, so stepping off any edge is a
// single AND to detect.

export const WHITE = 0;
export const BLACK = 8;

export const EMPTY = 0;
export const PAWN = 1;
export const KNIGHT = 2;
export const BISHOP = 3;
export const ROOK = 4;
export const QUEEN = 5;
export const KING = 6;

// A piece is its type OR'd with its color, e.g. BLACK | KNIGHT === 10.
export const pieceType = (piece) => piece & 7;
export const pieceColor = (piece) => piece & 8;

// FEN letter for each piece code ('.' marks codes that aren't pieces).
export const PIECE_CHARS = '.PNBRQK..pnbrqk';

export const onBoard = (sq) => (sq & 0x88) === 0;
export const fileOf = (sq) => sq & 7;
export const rankOf = (sq) => sq >> 4;
export const makeSquare = (file, rank) => (rank << 4) | file;

export const squareName = (sq) => 'abcdefgh'[fileOf(sq)] + (rankOf(sq) + 1);

// Returns the 0x88 index for a name like "e4", or -1 if it isn't a square.
export function parseSquare(name) {
  if (typeof name !== 'string' || !/^[a-h][1-8]$/.test(name)) return -1;
  return makeSquare(name.charCodeAt(0) - 97, name.charCodeAt(1) - 49);
}

export const A1 = 0, B1 = 1, C1 = 2, D1 = 3, E1 = 4, F1 = 5, G1 = 6, H1 = 7;
export const A8 = 112, B8 = 113, C8 = 114, D8 = 115, E8 = 116, F8 = 117, G8 = 118, H8 = 119;

// Castling rights bit flags.
export const CASTLE_WK = 1;
export const CASTLE_WQ = 2;
export const CASTLE_BK = 4;
export const CASTLE_BQ = 8;

export const KNIGHT_OFFSETS = [33, 31, 18, 14, -14, -18, -31, -33];
export const BISHOP_OFFSETS = [17, 15, -15, -17];
export const ROOK_OFFSETS = [16, 1, -1, -16];
export const KING_OFFSETS = [17, 16, 15, 1, -1, -15, -16, -17];

// Moves are plain integers so move generation never allocates objects:
//   bits  0-6   from square
//   bits  7-13  to square
//   bits 14-16  promotion piece type (0 = none)
//   bits 17-20  flags below
export const FLAG_CAPTURE = 1 << 17; // also set on en passant captures
export const FLAG_EN_PASSANT = 1 << 18;
export const FLAG_CASTLE = 1 << 19;
export const FLAG_DOUBLE_PUSH = 1 << 20;

export const encodeMove = (from, to, promotion = 0, flags = 0) =>
  from | (to << 7) | (promotion << 14) | flags;
export const moveFrom = (move) => move & 0x7f;
export const moveTo = (move) => (move >> 7) & 0x7f;
export const movePromotion = (move) => (move >> 14) & 7;
export const isCapture = (move) => (move & FLAG_CAPTURE) !== 0;
