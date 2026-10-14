// The game session behind the board: which pieces the player to move may
// pick up, where they can go, playing moves (including the promotion choice),
// the scoresheet and the result. It has no DOM code, so it runs under Node's
// test runner as well as in the browser.

import {
  Position, parseUci, WHITE, EMPTY, QUEEN, ROOK, BISHOP, KNIGHT,
  moveFrom, moveTo, movePromotion,
} from '../src/index.js';

// Offered in this order when a pawn promotes.
export const PROMOTION_PIECES = [QUEEN, KNIGHT, ROOK, BISHOP];

const DRAW_REASONS = {
  'insufficient material': 'Insufficient material',
  'fifty-move rule': 'Fifty-move rule',
  'threefold repetition': 'Threefold repetition',
};

export class Game {
  constructor() {
    this.reset();
  }

  // Rebuilds a game from UCI moves, as saved by uciHistory(). Stops at the
  // first move that isn't legal, so a corrupted save still loads.
  static fromUci(moves) {
    const game = new Game();
    for (const uci of moves) {
      const move = parseUci(game.pos, uci);
      if (move === null) break;
      game._play(move);
    }
    return game;
  }

  reset() {
    this.pos = new Position();
    this.sanHistory = [];
    this._refresh();
  }

  _refresh() {
    this.legal = this.pos.legalMoves();
    this.status = this.pos.status();
  }

  _play(move) {
    this.sanHistory.push(this.pos.toSan(move)); // SAN must be computed before the move
    this.pos.makeMove(move);
    this._refresh();
  }

  // 'w' or 'b'
  get turn() {
    return this.pos.turn === WHITE ? 'w' : 'b';
  }

  get isOver() {
    return this.status.isOver;
  }

  get ply() {
    return this.pos.ply;
  }

  // The last move played, or null.
  get lastMove() {
    const history = this.pos.history();
    return history.length ? history[history.length - 1] : null;
  }

  // Can the player to move pick up the piece on `sq` (a 0x88 square)?
  canMoveFrom(sq) {
    const piece = this.pos.board[sq];
    return !this.isOver && piece !== EMPTY && (piece & 8) === this.pos.turn;
  }

  // Squares the piece on `from` can legally move to.
  targets(from) {
    const squares = new Set();
    for (const move of this.legal) {
      if (moveFrom(move) === from) squares.add(moveTo(move));
    }
    return squares;
  }

  // Plays the piece on `from` to `to`. Returns 'moved'; 'promotion' when a
  // pawn reaches the last rank and the caller must ask which piece it becomes
  // (then call again with that piece type); or 'illegal'.
  play(from, to, promotion = 0) {
    if (this.isOver) return 'illegal';
    const candidates = this.legal.filter((m) => moveFrom(m) === from && moveTo(m) === to);
    if (candidates.length === 0) return 'illegal';
    let move = candidates[0];
    if (candidates.length > 1) {
      if (!promotion) return 'promotion';
      move = candidates.find((m) => movePromotion(m) === promotion);
      if (move === undefined) return 'illegal';
    }
    this._play(move);
    return 'moved';
  }

  takeBack() {
    if (this.pos.ply === 0) return false;
    this.pos.unmakeMove();
    this.sanHistory.pop();
    this._refresh();
    return true;
  }

  // Square of the king in check (or checkmated), or -1.
  checkedKingSquare() {
    return this.pos.inCheck() ? this.pos.kingSquare[this.pos.turn >> 3] : -1;
  }

  uciHistory() {
    return this.pos.history().map((m) => this.pos.toUci(m));
  }

  // Moves paired up the way a paper scoresheet lists them.
  scoresheet() {
    const rows = [];
    for (let i = 0; i < this.sanHistory.length; i += 2) {
      rows.push({ number: i / 2 + 1, white: this.sanHistory[i], black: this.sanHistory[i + 1] ?? '' });
    }
    return rows;
  }

  // What the status line shows. `side` is whose turn it is, the winner after
  // checkmate, or 'draw'.
  describe() {
    const { isOver, result, reason } = this.status;
    if (!isOver) {
      return {
        headline: `${this.turn === 'w' ? 'White' : 'Black'} to move`,
        detail: this.pos.inCheck() ? 'Check' : '',
        side: this.turn,
        tone: this.pos.inCheck() ? 'check' : 'normal',
      };
    }
    if (reason === 'checkmate') {
      const whiteWon = result === '1-0';
      return {
        headline: 'Checkmate',
        detail: `${whiteWon ? 'White' : 'Black'} wins · ${whiteWon ? '1–0' : '0–1'}`,
        side: whiteWon ? 'w' : 'b',
        tone: 'over',
      };
    }
    if (reason === 'stalemate') {
      return { headline: 'Stalemate', detail: 'Draw · ½–½', side: 'draw', tone: 'over' };
    }
    return { headline: 'Draw', detail: `${DRAW_REASONS[reason]} · ½–½`, side: 'draw', tone: 'over' };
  }
}
