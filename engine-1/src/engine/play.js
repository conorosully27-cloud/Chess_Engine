// Choosing the engine's move in a game: a book move in the opening (for
// variety), otherwise a full-strength search for the given time.

import { Position } from '../position.js';
import { parseUci } from '../notation.js';

// Main lines of popular openings, as UCI moves from the starting position.
const BOOK = [
  'e2e4 e7e5 g1f3 b8c6 f1b5 a7a6 b5a4 g8f6 e1g1 f8e7', // Ruy Lopez
  'e2e4 e7e5 g1f3 b8c6 f1c4 f8c5 c2c3 g8f6 d2d3 d7d6', // Italian Game
  'e2e4 e7e5 g1f3 b8c6 d2d4 e5d4 f3d4 g8f6 d4c6 b7c6', // Scotch Game
  'e2e4 e7e5 g1f3 g8f6 f3e5 d7d6 e5f3 f6e4 d2d4 d6d5', // Petrov Defence
  'e2e4 e7e5 b1c3 g8f6 f2f4 d7d5 f4e5 f6e4', // Vienna Gambit
  'e2e4 c7c5 g1f3 d7d6 d2d4 c5d4 f3d4 g8f6 b1c3 a7a6', // Sicilian, Najdorf
  'e2e4 c7c5 g1f3 b8c6 d2d4 c5d4 f3d4 g8f6 b1c3 e7e5', // Sicilian, Sveshnikov
  'e2e4 c7c5 g1f3 e7e6 d2d4 c5d4 f3d4 b8c6 b1c3 d8c7', // Sicilian, Taimanov
  'e2e4 c7c5 b1c3 b8c6 g2g3 g7g6 f1g2 f8g7 d2d3 d7d6', // Closed Sicilian
  'e2e4 e7e6 d2d4 d7d5 b1c3 g8f6 c1g5 f8e7 e4e5 f6d7', // French Defence
  'e2e4 c7c6 d2d4 d7d5 b1c3 d5e4 c3e4 c8f5 e4g3 f5g6', // Caro-Kann
  'e2e4 d7d5 e4d5 d8d5 b1c3 d5a5 d2d4 g8f6 g1f3 c8f5', // Scandinavian
  'e2e4 d7d6 d2d4 g8f6 b1c3 g7g6 g1f3 f8g7 f1e2 e8g8', // Pirc Defence
  'd2d4 d7d5 c2c4 e7e6 b1c3 g8f6 c1g5 f8e7 e2e3 e8g8', // Queen's Gambit Declined
  'd2d4 d7d5 c2c4 c7c6 g1f3 g8f6 b1c3 d5c4 a2a4 c8f5', // Slav Defence
  'd2d4 d7d5 c2c4 d5c4 g1f3 g8f6 e2e3 e7e6 f1c4 c7c5', // Queen's Gambit Accepted
  'd2d4 g8f6 c2c4 g7g6 b1c3 f8g7 e2e4 d7d6 g1f3 e8g8', // King's Indian
  'd2d4 g8f6 c2c4 e7e6 b1c3 f8b4 e2e3 e8g8 f1d3 d7d5', // Nimzo-Indian
  'd2d4 g8f6 c2c4 e7e6 g1f3 b7b6 g2g3 c8b7 f1g2 f8e7', // Queen's Indian
  'd2d4 g8f6 c2c4 g7g6 b1c3 d7d5 c4d5 f6d5 e2e4 d5c3', // Grünfeld
  'd2d4 d7d5 g1f3 g8f6 c1f4 e7e6 e2e3 c7c5 c2c3 b8c6', // London System
  'c2c4 e7e5 b1c3 g8f6 g1f3 b8c6 g2g3 d7d5 c4d5 f6d5', // English Opening
  'c2c4 g8f6 b1c3 e7e6 e2e4 c7c5 e4e5 f6g8 g1f3 b8c6', // English, Mikenas
  'g1f3 d7d5 g2g3 g8f6 f1g2 e7e6 e1g1 f8e7 d2d3 e8g8', // Réti / King's Indian Attack
].map((line) => line.split(' '));

export const BOOK_LINES = BOOK;

// A book move for the game so far (UCI moves from the start), or null.
export function bookMove(history, random = Math.random) {
  const candidates = new Set();
  for (const line of BOOK) {
    if (line.length > history.length && history.every((move, i) => line[i] === move)) {
      candidates.add(line[history.length]);
    }
  }
  if (candidates.size === 0) return null;
  const moves = [...candidates];
  return moves[Math.floor(random() * moves.length)];
}

// Picks the engine's move in a game. `pos` must include the game's history
// (so the search knows about repetitions); `history` is the same moves as UCI,
// for the opening book. Returns { bestMove, ... } like Engine.search, with
// `book: true` for a book move.
export function chooseMove(engine, pos, history, { timeMs = 3000, useBook = true, random = Math.random, onInfo } = {}) {
  const book = useBook ? bookMove(history, random) : null;
  if (book && parseUci(pos, book) !== null) return { bestMove: book, book: true };
  return engine.search(pos, { timeMs, onInfo });
}

// Builds a Position from the start, with `history` (UCI moves) played, so
// repetitions earlier in the game are known to the search.
export function positionFromHistory(history, fen = undefined) {
  const pos = new Position(fen);
  for (const uci of history) {
    const move = parseUci(pos, uci);
    if (move === null) throw new Error(`Illegal move in history: ${uci}`);
    pos.makeMove(move);
  }
  return pos;
}
