// Move notation: UCI ("e2e4", "e7e8q"), which engines speak, and SAN ("e4",
// "Nxf7+", "O-O", "e8=Q#"), which people read and PGN files use.

import {
  BLACK, PAWN, KNIGHT, BISHOP, ROOK, QUEEN, KING, PIECE_CHARS,
  FLAG_CAPTURE, FLAG_CASTLE,
  moveFrom, moveTo, movePromotion, fileOf, rankOf, parseSquare, squareName,
} from './board.js';

const SAN_PIECE_LETTERS = ' PNBRQK'; // indexed by piece type
const LETTER_TO_TYPE = { N: KNIGHT, B: BISHOP, R: ROOK, Q: QUEEN, K: KING };

export function moveToUci(move) {
  const promotion = movePromotion(move);
  return squareName(moveFrom(move)) + squareName(moveTo(move)) + (promotion ? PIECE_CHARS[BLACK | promotion] : '');
}

// Returns the legal move matching a UCI string, or null.
export function parseUci(pos, text) {
  const match = /^([a-h][1-8])([a-h][1-8])([nbrq])?$/.exec(String(text).trim().toLowerCase());
  if (!match) return null;
  const from = parseSquare(match[1]);
  const to = parseSquare(match[2]);
  const promotion = match[3] ? LETTER_TO_TYPE[match[3].toUpperCase()] : 0;
  for (const move of pos.legalMoves()) {
    if (moveFrom(move) === from && moveTo(move) === to && movePromotion(move) === promotion) return move;
  }
  return null;
}

// SAN for a legal move in `pos`, which must be the position before the move.
export function moveToSan(pos, move) {
  const from = moveFrom(move);
  const to = moveTo(move);
  const piece = pos.board[from];
  const type = piece & 7;
  let san;

  if (move & FLAG_CASTLE) {
    san = to > from ? 'O-O' : 'O-O-O';
  } else if (type === PAWN) {
    san = move & FLAG_CAPTURE ? `${'abcdefgh'[fileOf(from)]}x` : '';
    san += squareName(to);
    const promotion = movePromotion(move);
    if (promotion) san += `=${SAN_PIECE_LETTERS[promotion]}`;
  } else {
    san = SAN_PIECE_LETTERS[type];
    // If another piece of the same kind can also reach `to`, add the origin
    // file if that tells them apart, else the rank, else both.
    let ambiguous = false, sameFile = false, sameRank = false;
    for (const other of pos.legalMoves()) {
      const otherFrom = moveFrom(other);
      if (otherFrom === from || moveTo(other) !== to || pos.board[otherFrom] !== piece) continue;
      ambiguous = true;
      if (fileOf(otherFrom) === fileOf(from)) sameFile = true;
      if (rankOf(otherFrom) === rankOf(from)) sameRank = true;
    }
    if (ambiguous) {
      if (!sameFile) san += 'abcdefgh'[fileOf(from)];
      else if (!sameRank) san += rankOf(from) + 1;
      else san += squareName(from);
    }
    if (move & FLAG_CAPTURE) san += 'x';
    san += squareName(to);
  }

  pos.makeMove(move);
  if (pos.inCheck()) san += pos.hasLegalMove() ? '+' : '#';
  pos.unmakeMove();
  return san;
}

// Returns the legal move matching a SAN string, or null if there is no such
// move or it is ambiguous. Accepts common variations: "0-0" for castling,
// missing "=" in promotions, check marks and annotations, and extra
// disambiguation such as "Ngf3" or "e2e4".
export function parseSan(pos, text) {
  const san = String(text)
    .trim()
    .replace(/\s*e\.p\.$/, '')
    .replace(/[+#!?]+$/, '')
    .replace(/0/g, 'O');

  const legal = pos.legalMoves();
  if (san === 'O-O' || san === 'O-O-O') {
    const kingSide = san === 'O-O';
    return legal.find((m) => (m & FLAG_CASTLE) && (moveTo(m) > moveFrom(m)) === kingSide) ?? null;
  }

  const match = /^([NBRQK])?([a-h])?([1-8])?x?([a-h][1-8])(?:=?([NBRQnbrq]))?$/.exec(san);
  if (!match) return null;
  const [, pieceLetter, fromFile, fromRank, toName, promotionLetter] = match;
  const type = pieceLetter ? LETTER_TO_TYPE[pieceLetter] : PAWN;
  const to = parseSquare(toName);
  const promotion = promotionLetter ? LETTER_TO_TYPE[promotionLetter.toUpperCase()] : 0;

  let found = null;
  for (const move of legal) {
    const from = moveFrom(move);
    if (moveTo(move) !== to || (pos.board[from] & 7) !== type || movePromotion(move) !== promotion) continue;
    if (fromFile && 'abcdefgh'[fileOf(from)] !== fromFile) continue;
    if (fromRank && rankOf(from) + 1 !== Number(fromRank)) continue;
    if (found !== null) return null; // ambiguous
    found = move;
  }
  return found;
}

// Accepts either SAN or UCI.
export function parseMove(pos, text) {
  return parseSan(pos, text) ?? parseUci(pos, text);
}
