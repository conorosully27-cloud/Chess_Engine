// Board UI: renders the game and turns pointer input into moves.
//
// Two ways to move, working the same with mouse, touch and pen (Pointer Events):
//   - drag a piece and drop it on a legal square;
//   - tap a piece, then tap a legal square.
// Only the player to move can pick pieces up. Legal squares show a small
// square (or corner brackets for captures), and dropping anywhere illegal
// sends the piece back.

import { Game, PROMOTION_PIECES } from './game.js';
import { EMPTY, QUEEN, ROOK, BISHOP, KNIGHT, PIECE_CHARS, makeSquare, fileOf, rankOf, squareName, moveFrom, moveTo } from '../src/index.js';

const STORAGE_KEY = 'chess-engine/game';
const DRAG_THRESHOLD = 4; // CSS px a press must travel before it counts as a drag
const reducedMotion = window.matchMedia('(prefers-reduced-motion: reduce)');
const PIECE_NAMES = { [QUEEN]: 'Queen', [ROOK]: 'Rook', [BISHOP]: 'Bishop', [KNIGHT]: 'Knight' };

const $ = (id) => document.getElementById(id);
const boardEl = $('board');
const promotionEl = $('promotion');
const promotionChoicesEl = $('promotion-choices');
const statusEl = $('status');
const statusHeadlineEl = $('status-headline');
const statusDetailEl = $('status-detail');
const statusMoveEl = $('status-move');
const turnMarkEl = $('turn-mark');
const ranksEl = $('ranks');
const filesEl = $('files');
const movesEl = $('moves');
const movesEmptyEl = $('moves-empty');
const takeBackButton = $('take-back');
const flipButton = $('flip');
const newGameButton = $('new-game');

// 'wK', 'bN', ... (matches the image file names), or '' for an empty square.
function pieceName(piece) {
  return piece === EMPTY ? '' : (piece & 8 ? 'b' : 'w') + PIECE_CHARS[piece & 7];
}

// SAN as figurine notation: "Nf3" becomes a knight symbol + "f3". The
// letter stays in the text for screen readers and copying.
function figurines(san) {
  return san.split(/([KQRBN])/).filter(Boolean).map((part) => {
    if (!/^[KQRBN]$/.test(part)) return document.createTextNode(part);
    const fig = Object.assign(document.createElement('span'), { className: 'fig' });
    fig.dataset.fig = part;
    fig.append(Object.assign(document.createElement('span'), { className: 'visually-hidden', textContent: part }));
    return fig;
  });
}

function loadSaved() {
  try {
    const saved = JSON.parse(localStorage.getItem(STORAGE_KEY));
    if (saved && Array.isArray(saved.moves)) return saved;
  } catch {
    // Storage unavailable or corrupt: start fresh.
  }
  return { moves: [], flipped: false };
}

class BoardUI {
  constructor() {
    const saved = loadSaved();
    this.game = Game.fromUci(saved.moves);
    this.flipped = Boolean(saved.flipped);
    this.selected = -1; // square of the picked-up piece, or -1
    this.drag = null; // in-progress press/drag, see onPointerDown
    this.ghost = null; // the piece image that follows the pointer while dragging
    this.pendingPromotion = null; // { from, to } while the promotion picker is open
    this.squareEls = new Map(); // 0x88 square -> element
    this.confirmTimer = 0;

    boardEl.addEventListener('pointerdown', (e) => this.onPointerDown(e));
    boardEl.addEventListener('pointermove', (e) => this.onPointerMove(e));
    boardEl.addEventListener('pointerup', (e) => this.onPointerUp(e));
    boardEl.addEventListener('pointercancel', (e) => this.onPointerCancel(e));
    boardEl.addEventListener('contextmenu', (e) => e.preventDefault());
    promotionEl.addEventListener('click', (e) => {
      const choice = e.target.closest('[data-promotion]');
      if (choice) this.finishPromotion(Number(choice.dataset.promotion));
      else this.cancelPromotion();
    });
    document.addEventListener('keydown', (e) => {
      if (e.key !== 'Escape') return;
      if (this.pendingPromotion) this.cancelPromotion();
      else if (this.selected >= 0 && !this.drag) this.select(-1);
    });
    takeBackButton.addEventListener('click', () => this.takeBack());
    flipButton.addEventListener('click', () => this.flip());
    newGameButton.addEventListener('click', () => this.newGame());

    this.buildBoard();
    this.render();
  }

  // ---------------------------------------------------------------- geometry

  // Screen column/row (0-7, from the top left) of a square.
  col(sq) {
    return this.flipped ? 7 - fileOf(sq) : fileOf(sq);
  }

  row(sq) {
    return this.flipped ? rankOf(sq) : 7 - rankOf(sq);
  }

  // The square under a screen point, or -1 if it's off the board.
  squareAt(x, y) {
    const rect = boardEl.getBoundingClientRect();
    const col = Math.floor(((x - rect.left) / rect.width) * 8);
    const row = Math.floor(((y - rect.top) / rect.height) * 8);
    if (col < 0 || col > 7 || row < 0 || row > 7) return -1;
    return makeSquare(this.flipped ? 7 - col : col, this.flipped ? row : 7 - row);
  }

  // ---------------------------------------------------------------- rendering

  buildBoard() {
    boardEl.replaceChildren();
    this.squareEls.clear();
    for (let row = 0; row < 8; row++) {
      for (let col = 0; col < 8; col++) {
        const file = this.flipped ? 7 - col : col;
        const rank = this.flipped ? row : 7 - row;
        const sq = makeSquare(file, rank);
        const el = document.createElement('div');
        el.className = `square ${(file + rank) % 2 ? 'light' : 'dark'}`;
        el.dataset.square = squareName(sq);
        el.append(Object.assign(document.createElement('div'), { className: 'piece' }));
        boardEl.append(el);
        this.squareEls.set(sq, el);
      }
    }
    // Coordinates in the margin, as a printed diagram has them.
    const label = (text) => Object.assign(document.createElement('span'), { textContent: text });
    const ranks = [8, 7, 6, 5, 4, 3, 2, 1];
    const files = [...'abcdefgh'];
    if (this.flipped) {
      ranks.reverse();
      files.reverse();
    }
    ranksEl.replaceChildren(...ranks.map(label));
    filesEl.replaceChildren(...files.map(label));
  }

  render() {
    const game = this.game;
    const board = game.pos.board;
    const targets = this.selected >= 0 ? game.targets(this.selected) : new Set();
    const last = game.lastMove;
    const checked = game.checkedKingSquare();

    for (const [sq, el] of this.squareEls) {
      const piece = board[sq];
      el.lastElementChild.dataset.piece = pieceName(piece);
      el.classList.toggle('is-last', last !== null && (sq === moveFrom(last) || sq === moveTo(last)));
      el.classList.toggle('is-selected', sq === this.selected);
      el.classList.toggle('is-target', targets.has(sq));
      el.classList.toggle('is-occupied', piece !== EMPTY);
      el.classList.toggle('is-check', sq === checked);
      el.classList.toggle('is-movable', game.canMoveFrom(sq));
      el.classList.toggle('is-drag-origin', Boolean(this.drag?.active) && sq === this.drag.from);
      el.classList.toggle('is-hover', Boolean(this.drag?.active) && sq === this.drag.hover && targets.has(sq));
    }

    const { headline, detail, side, tone } = game.describe();
    statusHeadlineEl.textContent = headline;
    statusDetailEl.textContent = detail;
    statusEl.dataset.tone = tone;
    statusMoveEl.textContent = `Move ${game.pos.fullmoveNumber}`;
    turnMarkEl.dataset.side = side;
    boardEl.setAttribute('aria-label', `Chess board. ${headline}${detail ? `. ${detail}` : ''}`);

    this.renderScoresheet();
    takeBackButton.disabled = game.ply === 0;
    this.save();
  }

  renderScoresheet() {
    const rows = this.game.scoresheet();
    const total = this.game.sanHistory.length;
    movesEmptyEl.hidden = rows.length > 0;
    movesEl.replaceChildren(...rows.map((row, i) => {
      const li = document.createElement('li');
      const cell = (san, ply) => {
        const span = document.createElement('span');
        span.append(...figurines(san));
        if (ply === total - 1) span.className = 'is-latest';
        return span;
      };
      li.append(
        Object.assign(document.createElement('span'), { className: 'num', textContent: `${row.number}.` }),
        cell(row.white, i * 2),
        cell(row.black, i * 2 + 1),
      );
      return li;
    }));
    movesEl.scrollTop = movesEl.scrollHeight;
  }

  // Slides the piece that just landed on `to` from `from`, for tap moves.
  // (A dragged piece is already where the player dropped it.)
  animateMove(from, to) {
    if (reducedMotion.matches) return;
    const pieceEl = this.squareEls.get(to).lastElementChild;
    const dx = (this.col(from) - this.col(to)) * 100;
    const dy = (this.row(from) - this.row(to)) * 100;
    pieceEl.style.zIndex = 4; // above the other pieces it passes over
    const animation = pieceEl.animate(
      [{ transform: `translate(${dx}%, ${dy}%)` }, { transform: 'translate(0, 0)' }],
      { duration: 160, easing: 'ease-out' },
    );
    animation.onfinish = animation.oncancel = () => pieceEl.style.removeProperty('z-index');
  }

  save() {
    try {
      localStorage.setItem(STORAGE_KEY, JSON.stringify({ moves: this.game.uciHistory(), flipped: this.flipped }));
    } catch {
      // Storage unavailable (private browsing): the game just won't survive a reload.
    }
  }

  // ---------------------------------------------------------------- input

  select(sq) {
    this.selected = sq;
    this.render();
  }

  onPointerDown(e) {
    if (e.button !== 0 || this.drag || this.pendingPromotion) return;
    const sq = this.squareAt(e.clientX, e.clientY);
    if (sq < 0) return;
    e.preventDefault();

    // Second tap of a tap-tap move.
    if (this.selected >= 0 && sq !== this.selected && this.game.targets(this.selected).has(sq)) {
      this.tryMove(this.selected, sq, { animate: true });
      return;
    }

    if (!this.game.canMoveFrom(sq)) {
      this.select(-1);
      return;
    }

    // Pick the piece up. Whether this becomes a drag or a tap is decided by
    // how far the pointer travels before it's released.
    this.drag = {
      pointerId: e.pointerId,
      from: sq,
      startX: e.clientX,
      startY: e.clientY,
      active: false,
      hover: -1,
      // Tapping the piece that's already selected puts it back down.
      deselectOnTap: sq === this.selected,
    };
    boardEl.setPointerCapture(e.pointerId);
    this.select(sq);
  }

  onPointerMove(e) {
    const drag = this.drag;
    if (!drag || e.pointerId !== drag.pointerId) return;
    if (!drag.active) {
      if (Math.hypot(e.clientX - drag.startX, e.clientY - drag.startY) < DRAG_THRESHOLD) return;
      drag.active = true;
      this.showGhost(drag.from);
      this.render();
    }
    this.moveGhost(e.clientX, e.clientY);
    const sq = this.squareAt(e.clientX, e.clientY);
    if (sq !== drag.hover) {
      const targets = this.game.targets(drag.from);
      this.squareEls.get(drag.hover)?.classList.remove('is-hover');
      if (targets.has(sq)) this.squareEls.get(sq).classList.add('is-hover');
      drag.hover = sq;
    }
  }

  onPointerUp(e) {
    const drag = this.drag;
    if (!drag || e.pointerId !== drag.pointerId) return;
    this.drag = null;
    if (drag.active) {
      this.hideGhost();
      const sq = this.squareAt(e.clientX, e.clientY);
      if (sq !== drag.from && this.game.targets(drag.from).has(sq)) {
        this.tryMove(drag.from, sq, { animate: false });
        return;
      }
      // Dropped somewhere illegal: the piece returns and stays selected.
    } else if (drag.deselectOnTap) {
      this.selected = -1;
    }
    this.render();
  }

  onPointerCancel(e) {
    if (!this.drag || e.pointerId !== this.drag.pointerId) return;
    this.drag = null;
    this.hideGhost();
    this.render();
  }

  showGhost(from) {
    const size = boardEl.getBoundingClientRect().width / 8;
    this.ghost = document.createElement('div');
    this.ghost.className = 'piece ghost';
    this.ghost.dataset.piece = pieceName(this.game.pos.board[from]);
    this.ghost.style.width = this.ghost.style.height = `${size}px`;
    this.ghostSize = size;
    document.body.append(this.ghost);
  }

  moveGhost(x, y) {
    const half = this.ghostSize / 2;
    this.ghost.style.transform = `translate(${x - half}px, ${y - half}px)`;
  }

  hideGhost() {
    this.ghost?.remove();
    this.ghost = null;
  }

  tryMove(from, to, { animate }) {
    const result = this.game.play(from, to);
    if (result === 'promotion') {
      this.askPromotion(from, to);
      return;
    }
    this.selected = -1;
    this.render();
    if (result === 'moved' && animate) this.animateMove(from, to);
  }

  // ---------------------------------------------------------------- promotion

  askPromotion(from, to) {
    this.pendingPromotion = { from, to };
    const color = this.game.turn;
    promotionChoicesEl.replaceChildren(...PROMOTION_PIECES.map((type) => {
      const button = document.createElement('button');
      button.type = 'button';
      button.className = 'promotion-choice';
      button.dataset.promotion = type;
      button.setAttribute('aria-label', PIECE_NAMES[type]);
      button.append(Object.assign(document.createElement('div'), { className: 'piece' }));
      button.firstChild.dataset.piece = color + PIECE_CHARS[type];
      return button;
    }));
    promotionEl.hidden = false;
    promotionChoicesEl.firstChild.focus({ preventScroll: true });
    this.render();
  }

  finishPromotion(type) {
    const { from, to } = this.pendingPromotion;
    this.pendingPromotion = null;
    promotionEl.hidden = true;
    this.game.play(from, to, type);
    this.selected = -1;
    this.render();
  }

  cancelPromotion() {
    this.pendingPromotion = null;
    promotionEl.hidden = true;
    this.render();
  }

  // ---------------------------------------------------------------- controls

  takeBack() {
    if (this.pendingPromotion) this.cancelPromotion();
    this.game.takeBack();
    this.selected = -1;
    this.render();
  }

  flip() {
    this.flipped = !this.flipped;
    this.buildBoard();
    this.render();
  }

  // Starting over loses the game, so mid-game it takes a second tap to confirm.
  newGame() {
    const needsConfirm = this.game.ply > 0 && !this.game.isOver;
    if (needsConfirm && !newGameButton.dataset.confirm) {
      newGameButton.dataset.confirm = 'true';
      newGameButton.textContent = 'Start over?';
      this.confirmTimer = setTimeout(() => this.resetNewGameButton(), 3000);
      return;
    }
    this.resetNewGameButton();
    if (this.pendingPromotion) this.cancelPromotion();
    this.game.reset();
    this.selected = -1;
    this.render();
  }

  resetNewGameButton() {
    clearTimeout(this.confirmTimer);
    delete newGameButton.dataset.confirm;
    newGameButton.textContent = 'New game';
  }
}

new BoardUI();
