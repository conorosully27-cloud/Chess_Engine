// The game session behind the board: which pieces the player to move may
// pick up, where they can go, playing moves (including the promotion choice),
// the scoresheet and the result. It has no DOM code, so it runs under Node's
// test runner as well as in the browser.

import {
  Position, START_FEN, parseSan, parseUci, WHITE, EMPTY, QUEEN, ROOK, BISHOP, KNIGHT,
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
  // A game from the standard start, or from any position (a FEN; throws an
  // Error saying what is wrong if it can't be read).
  constructor(fen = START_FEN) {
    this.reset(fen);
  }

  // Rebuilds a game from UCI moves, as saved by uciHistory(), from `fen`.
  // Stops at the first move that isn't legal, so a corrupted save still
  // loads (and a bad FEN gives the standard start).
  static fromUci(moves, fen = START_FEN) {
    let game;
    try {
      game = new Game(fen);
    } catch {
      game = new Game();
    }
    for (const uci of moves) {
      const move = parseUci(game.pos, uci);
      if (move === null) break;
      game._play(move);
    }
    return game;
  }

  // A pasted FEN or PGN. PGN tags are read only for a starting FEN;
  // comments, variations, annotations and the result are skipped. Throws
  // an Error to show the user if the text can't be read.
  static fromText(text) {
    const trimmed = String(text).trim();
    if (!trimmed) throw new Error('Paste a FEN or a PGN first.');
    if (/^[pnbrqkPNBRQK1-8]+(\/[pnbrqkPNBRQK1-8]+){7}\s/.test(trimmed + ' ')) {
      try {
        return new Game(trimmed.split(/\s+/).slice(0, 6).join(' '));
      } catch (error) {
        throw new Error(`That FEN can't be read: ${error.message.replace(/^Invalid FEN "[^"]*": /, '')}.`);
      }
    }
    const fenTag = /\[\s*FEN\s+"([^"]*)"\s*\]/i.exec(trimmed);
    let game;
    try {
      game = new Game(fenTag ? fenTag[1] : START_FEN);
    } catch (error) {
      throw new Error(`The PGN's FEN tag can't be read: ${error.message.replace(/^Invalid FEN "[^"]*": /, '')}.`);
    }
    let movetext = trimmed
      .replace(/\[[^\]]*\]/g, ' ') // tags
      .replace(/\{[^}]*\}/g, ' ') // comments
      .replace(/;[^\n]*/g, ' ') // rest-of-line comments
      .replace(/\$\d+/g, ' '); // numeric annotations
    // Variations, innermost first (they can nest).
    for (let before = ''; before !== movetext;) {
      before = movetext;
      movetext = movetext.replace(/\([^()]*\)/g, ' ');
    }
    const tokens = movetext.split(/\s+/)
      .map((t) => t.replace(/^\d+\.+/, '')) // move numbers, also run into the move ("1.e4")
      .filter((t) => t && !/^(1-0|0-1|1\/2-1\/2|\*|\.+)$/.test(t));
    if (tokens.length === 0 && !fenTag) throw new Error('No moves found: paste a FEN, or a PGN with moves.');
    for (const san of tokens) {
      const move = parseSan(game.pos, san);
      if (move === null) {
        const number = `${game.pos.fullmoveNumber}${game.turn === 'w' ? '.' : '…'}`;
        throw new Error(`Move ${number} ${san} isn't legal in that position.`);
      }
      game._play(move);
    }
    return game;
  }

  // Back to the position the game started from (or another FEN).
  reset(fen = this.startFen ?? START_FEN) {
    this.pos = new Position(fen);
    this.startFen = this.pos.fen();
    this.sanHistory = [];
    this.resigned = null; // 'w' or 'b' once a side resigns
    this._refresh();
  }

  _refresh() {
    this.legal = this.pos.legalMoves();
    this.status = this.resigned
      ? { isOver: true, result: this.resigned === 'w' ? '0-1' : '1-0', reason: 'resignation' }
      : this.pos.status();
  }

  // The side `color` ('w' or 'b') resigns. Ignored once the game is over.
  resign(color) {
    if (this.isOver) return false;
    this.resigned = color;
    this._refresh();
    return true;
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
    if (this.resigned) {
      // Taking back after resigning resumes the game where it stood.
      this.resigned = null;
      this._refresh();
      return true;
    }
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

  // Whether the game started with Black to move (from a FEN): the first
  // scoresheet row then has "…" for White's move.
  get startsWithBlack() {
    return / b /.test(this.startFen);
  }

  // Moves paired up the way a paper scoresheet lists them, numbered from
  // the starting position.
  scoresheet() {
    const first = Number(this.startFen.split(' ')[5]) || 1;
    const sans = this.startsWithBlack ? ['…', ...this.sanHistory] : this.sanHistory;
    const rows = [];
    for (let i = 0; i < sans.length; i += 2) {
      rows.push({ number: first + i / 2, white: sans[i], black: sans[i + 1] ?? '' });
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
    if (reason === 'resignation') {
      const whiteWon = result === '1-0';
      return {
        headline: `${whiteWon ? 'Black' : 'White'} resigns`,
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

  // The game in PGN (Portable Game Notation), the standard format other chess
  // apps import. `white` and `black` name the players; `date` is a Date.
  pgn({ white = 'White', black = 'Black', date = new Date(), event = 'Casual game' } = {}) {
    const result = this.status.isOver ? this.status.result : '*';
    const pad = (n) => String(n).padStart(2, '0');
    const tags = [
      ['Event', event],
      ['Site', 'Chess Engine'],
      ['Date', `${date.getFullYear()}.${pad(date.getMonth() + 1)}.${pad(date.getDate())}`],
      ['Round', '-'],
      ['White', white],
      ['Black', black],
      ['Result', result],
    ];
    const termination = { checkmate: 'Checkmate', resignation: `${this.resigned === 'w' ? 'White' : 'Black'} resigns`,
      stalemate: 'Stalemate', 'insufficient material': 'Insufficient material',
      'fifty-move rule': 'Fifty-move rule', 'threefold repetition': 'Threefold repetition' }[this.status.reason];
    if (termination) tags.push(['Termination', termination]);
    if (this.startFen !== START_FEN) tags.push(['SetUp', '1'], ['FEN', this.startFen]);
    const header = tags.map(([k, v]) => `[${k} "${String(v).replace(/["\\]/g, '')}"]`).join('\n');

    // Movetext, wrapped at 80 characters as the PGN standard asks.
    const tokens = [];
    const first = Number(this.startFen.split(' ')[5]) || 1;
    const offset = this.startsWithBlack ? 1 : 0;
    this.sanHistory.forEach((san, i) => {
      const ply = i + offset;
      const number = first + Math.floor(ply / 2);
      if (ply % 2 === 0) tokens.push(`${number}. ${san}`);
      else tokens.push(i === 0 ? `${number}... ${san}` : san);
    });
    tokens.push(result);
    const lines = [];
    let line = '';
    for (const token of tokens) {
      if (line && line.length + 1 + token.length > 80) {
        lines.push(line);
        line = token;
      } else {
        line = line ? `${line} ${token}` : token;
      }
    }
    lines.push(line);
    return `${header}\n\n${lines.join('\n')}\n`;
  }
}
