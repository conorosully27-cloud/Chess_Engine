// Board UI: renders the game and turns pointer input into moves.
//
// Two ways to move, working the same with mouse, touch and pen (Pointer Events):
//   - drag a piece and drop it on a legal square;
//   - tap a piece, then tap a legal square.
// Only the player to move can pick pieces up. Legal squares show a dot (or
// corner brackets for captures), and dropping anywhere illegal sends the
// piece back. The Style button picks the piece set and board colors.
//
// The engine runs in background workers (see engine-client.js): one plays its
// side when you play against it, the other powers the analysis panel.

import { Game, PROMOTION_PIECES } from './game.js';
import { EngineClient } from './engine-client.js';
import {
  Position, parseUci, EMPTY, QUEEN, ROOK, BISHOP, KNIGHT, PIECE_CHARS,
  makeSquare, fileOf, rankOf, squareName, moveFrom, moveTo, movePromotion,
} from '../src/index.js';

const STORAGE_KEY = 'chess-engine/game';
const STYLE_KEY = 'chess-engine/style';
const DRAG_THRESHOLD = 4; // CSS px a press must travel before it counts as a drag
const ANALYSIS_LIMITS = { timeMs: 30000, depth: 40 }; // then it stops, to save battery
const ENGINE_MIN_DELAY = 500; // ms, so instant (book) replies don't feel abrupt
const PV_PLIES = 10; // moves of the engine's line to show
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
const styleButton = $('style-button');
const styleSheetEl = $('style-sheet');
const pieceOptionsEl = $('piece-options');
const boardOptionsEl = $('board-options');
const arrowsEl = $('arrows');
const analysisToggle = $('analysis-toggle');
const analysisBodyEl = $('analysis-body');
const analysisMetaEl = $('analysis-meta');
const evalScoreEl = $('eval-score');
const evalFillEl = $('eval-fill');
const evalLineEl = $('eval-line');
const newGameSheetEl = $('new-game-sheet');

// Piece sets live in app/pieces/<id>/ (drawn by scripts/build-pieces.js);
// board schemes are defined in style.css under [data-board="<id>"].
const PIECE_SETS = [
  { id: 'ink', name: 'Ink' },
  { id: 'soft', name: 'Soft' },
  { id: 'tokens', name: 'Tokens' },
  { id: 'bauhaus', name: 'Bauhaus' },
  { id: 'pixel', name: 'Pixel' },
];
const BOARDS = [
  { id: 'walnut', name: 'Walnut' },
  { id: 'stone', name: 'Stone' },
  { id: 'lagoon', name: 'Lagoon' },
  { id: 'coral', name: 'Coral' },
  { id: 'midnight', name: 'Midnight' },
];
const PIECES_URL = new URL('./pieces/', import.meta.url);
let pieceSet = PIECE_SETS[0].id;

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

// Shows piece `name` ('wK', ...) on an element, from `set`; '' clears it.
function paintPiece(el, name, set = pieceSet) {
  if (el.dataset.piece === name && el.dataset.set === set) return;
  el.dataset.piece = name;
  el.dataset.set = set;
  el.style.backgroundImage = name ? `url("${new URL(`${set}/${name}.svg`, PIECES_URL)}")` : '';
}

function loadStyle() {
  const style = { pieces: PIECE_SETS[0].id, board: BOARDS[0].id };
  try {
    const saved = JSON.parse(localStorage.getItem(STYLE_KEY));
    if (PIECE_SETS.some((s) => s.id === saved?.pieces)) style.pieces = saved.pieces;
    if (BOARDS.some((b) => b.id === saved?.board)) style.board = saved.board;
  } catch {
    // Storage unavailable or corrupt: use the defaults.
  }
  return style;
}

function loadSaved() {
  try {
    const saved = JSON.parse(localStorage.getItem(STORAGE_KEY));
    if (saved && Array.isArray(saved.moves)) return { ...DEFAULT_SAVE, ...saved };
  } catch {
    // Storage unavailable or corrupt: start fresh.
  }
  return { ...DEFAULT_SAVE };
}

const DEFAULT_SAVE = { moves: [], flipped: false, opponent: 'friend', human: 'w', thinkMs: 5000, analysis: false };

class BoardUI {
  constructor() {
    const saved = loadSaved();
    this.game = Game.fromUci(saved.moves);
    this.flipped = Boolean(saved.flipped);
    // opponent: 'friend' (both sides on this device) or 'engine'.
    this.settings = { opponent: saved.opponent, human: saved.human, thinkMs: saved.thinkMs };
    this.analysisOn = Boolean(saved.analysis);
    this.player = new EngineClient(); // plays the engine's moves
    this.analyst = new EngineClient(); // runs the analysis panel
    this.thinkingFor = null; // the position (UCI history) the player is thinking about
    this.analysingFor = null; // the position the analysis is for
    this.analysis = null; // latest analysis info, or null
    this.engineError = false;
    this.selected = -1; // square of the picked-up piece, or -1
    this.drag = null; // in-progress press/drag, see onPointerDown
    this.ghost = null; // the piece image that follows the pointer while dragging
    this.pendingPromotion = null; // { from, to } while the promotion picker is open
    this.squareEls = new Map(); // 0x88 square -> element

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
      if (!styleSheetEl.hidden) this.closeStyle();
      else if (!newGameSheetEl.hidden) this.closeNewGame();
      else if (this.pendingPromotion) this.cancelPromotion();
      else if (this.selected >= 0 && !this.drag) this.select(-1);
    });
    takeBackButton.addEventListener('click', () => this.takeBack());
    flipButton.addEventListener('click', () => this.flip());
    newGameButton.addEventListener('click', () => this.openNewGame());
    analysisToggle.addEventListener('click', () => this.setAnalysis(!this.analysisOn));
    $('new-game-cancel').addEventListener('click', () => this.closeNewGame());
    $('start-game').addEventListener('click', () => this.startNewGame());
    newGameSheetEl.addEventListener('click', (e) => {
      if (e.target === newGameSheetEl) this.closeNewGame();
      const choice = e.target.closest('.segmented button');
      if (choice) {
        for (const b of choice.parentElement.children) b.setAttribute('aria-pressed', String(b === choice));
        this.updateNewGameForm();
      }
    });

    this.style = loadStyle();
    this.applyStyle();
    styleButton.addEventListener('click', () => (styleSheetEl.hidden ? this.openStyle() : this.closeStyle()));
    $('style-done').addEventListener('click', () => this.closeStyle());
    // The backdrop is transparent: a tap outside the sheet closes it.
    styleSheetEl.addEventListener('click', (e) => {
      if (e.target === styleSheetEl) this.closeStyle();
    });

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
      paintPiece(el.lastElementChild, pieceName(piece));
      el.classList.toggle('is-last', last !== null && (sq === moveFrom(last) || sq === moveTo(last)));
      el.classList.toggle('is-selected', sq === this.selected);
      el.classList.toggle('is-target', targets.has(sq));
      el.classList.toggle('is-occupied', piece !== EMPTY);
      el.classList.toggle('is-check', sq === checked);
      el.classList.toggle('is-movable', this.canPlayerMove(sq));
      el.classList.toggle('is-drag-origin', Boolean(this.drag?.active) && sq === this.drag.from);
      el.classList.toggle('is-hover', Boolean(this.drag?.active) && sq === this.drag.hover && targets.has(sq));
    }

    let { headline, detail, side, tone } = game.describe();
    if (this.vsEngine) {
      // Speak to the player: "Your move", "You win" rather than colors.
      const human = this.settings.human;
      if (!game.isOver) headline = game.turn === human ? 'Your move' : 'Engine thinking';
      else if (game.status.reason === 'checkmate') {
        const humanWon = (game.status.result === '1-0') === (human === 'w');
        detail = detail.replace(/^(White|Black) wins/, humanWon ? 'You win' : 'Engine wins');
      }
    }
    if (this.engineError) detail = 'The engine could not start in this browser';
    statusHeadlineEl.textContent = headline;
    statusDetailEl.textContent = detail;
    statusEl.dataset.tone = tone;
    statusMoveEl.textContent = `Move ${game.pos.fullmoveNumber}`;
    turnMarkEl.dataset.side = side;
    boardEl.setAttribute('aria-label', `Chess board. ${headline}${detail ? `. ${detail}` : ''}`);

    this.renderScoresheet();
    takeBackButton.disabled = this.vsEngine ? this.humanPlies() === 0 : game.ply === 0;
    this.save();
    this.syncEngines();
    this.renderAnalysis();
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
      localStorage.setItem(STORAGE_KEY, JSON.stringify({
        moves: this.game.uciHistory(),
        flipped: this.flipped,
        ...this.settings,
        analysis: this.analysisOn,
      }));
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

    if (!this.canPlayerMove(sq)) {
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
    paintPiece(this.ghost, pieceName(this.game.pos.board[from]));
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
      paintPiece(button.firstChild, color + PIECE_CHARS[type]);
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
    if (this.vsEngine) {
      // Undo back to before your last move (taking back the engine's reply too).
      if (this.humanPlies() === 0) return;
      this.player.stop();
      this.thinkingFor = null;
      do this.game.takeBack(); while (this.game.turn !== this.settings.human);
    } else {
      this.game.takeBack();
    }
    this.selected = -1;
    this.render();
  }

  flip() {
    this.flipped = !this.flipped;
    this.buildBoard();
    this.render();
  }

  // ---------------------------------------------------------------- new game

  openNewGame() {
    const press = (group, value) => {
      for (const b of $(group).children) b.setAttribute('aria-pressed', String(b.dataset.value === String(value)));
    };
    press('opponent-options', this.settings.opponent);
    press('color-options', this.settings.human);
    press('time-options', this.settings.thinkMs);
    this.updateNewGameForm();
    newGameSheetEl.hidden = false;
    $('start-game').focus({ preventScroll: true });
  }

  updateNewGameForm() {
    const opponent = $('opponent-options').querySelector('[aria-pressed="true"]').dataset.value;
    $('engine-settings').hidden = opponent !== 'engine';
  }

  closeNewGame() {
    newGameSheetEl.hidden = true;
    newGameButton.focus({ preventScroll: true });
  }

  startNewGame() {
    const chosen = (group) => $(group).querySelector('[aria-pressed="true"]').dataset.value;
    this.settings = {
      opponent: chosen('opponent-options'),
      human: chosen('color-options'),
      thinkMs: Number(chosen('time-options')),
    };
    newGameSheetEl.hidden = true;
    if (this.pendingPromotion) this.cancelPromotion();
    this.player.stop();
    this.analyst.stop();
    this.thinkingFor = this.analysingFor = null;
    this.analysis = null;
    this.game.reset();
    this.selected = -1;
    // Playing Black against the engine, see the board from Black's side.
    const flipped = this.vsEngine && this.settings.human === 'b';
    if (flipped !== this.flipped) {
      this.flipped = flipped;
      this.buildBoard();
    }
    this.render();
  }

  // ---------------------------------------------------------------- engine

  get vsEngine() {
    return this.settings.opponent === 'engine';
  }

  get engineToMove() {
    return this.vsEngine && !this.game.isOver && this.game.turn !== this.settings.human;
  }

  canPlayerMove(sq) {
    return this.game.canMoveFrom(sq) && !this.engineToMove;
  }

  // Moves the human has made in an engine game.
  humanPlies() {
    const ply = this.game.ply;
    return this.settings.human === 'w' ? Math.ceil(ply / 2) : Math.floor(ply / 2);
  }

  // Starts or stops the two engines to match the current position: the player
  // when it's the engine's turn, the analysis whenever it's switched on (but
  // not while the engine is thinking, so they don't share the CPU).
  syncEngines() {
    const history = this.game.uciHistory();
    const key = history.join(' ');

    if (this.engineToMove) {
      if (this.thinkingFor !== key) {
        this.thinkingFor = key;
        const started = performance.now();
        this.player.run({ type: 'play', history, timeMs: this.settings.thinkMs }, {
          onDone: (result) => {
            const wait = Math.max(0, ENGINE_MIN_DELAY - (performance.now() - started));
            setTimeout(() => this.playEngineMove(key, result.bestMove), wait);
          },
          onError: () => this.engineFailed(),
        });
      }
    } else if (this.thinkingFor !== null) {
      this.player.stop();
      this.thinkingFor = null;
    }

    const analyse = this.analysisOn && !this.engineToMove && !this.game.isOver;
    if (analyse) {
      if (this.analysingFor !== key) {
        this.analysingFor = key;
        this.analysis = null;
        this.analyst.run({ type: 'analyse', history, ...ANALYSIS_LIMITS }, {
          onInfo: (info) => this.showAnalysis(key, info, false),
          onDone: (result) => this.showAnalysis(key, result, true),
          onError: () => this.engineFailed(),
        });
      }
    } else if (this.analysingFor !== null) {
      this.analyst.stop();
      this.analysingFor = null;
      this.analysis = null;
    }
  }

  playEngineMove(key, uci) {
    // Ignore a reply to a position that has since changed (e.g. taken back).
    if (this.thinkingFor !== key || this.game.uciHistory().join(' ') !== key) return;
    this.thinkingFor = null;
    const move = uci && parseUci(this.game.pos, uci);
    if (!move) return;
    this.game.play(moveFrom(move), moveTo(move), movePromotion(move));
    this.selected = -1;
    this.render();
    this.animateMove(moveFrom(move), moveTo(move));
  }

  engineFailed() {
    this.engineError = true;
    this.thinkingFor = this.analysingFor = null;
    this.render();
  }

  setAnalysis(on) {
    this.analysisOn = on;
    this.render();
  }

  showAnalysis(key, info, done) {
    if (this.analysingFor !== key || !info.bestMove) return;
    this.analysis = { ...info, done };
    this.renderAnalysis();
  }

  renderAnalysis() {
    analysisToggle.setAttribute('aria-checked', String(this.analysisOn));
    analysisBodyEl.hidden = !this.analysisOn;
    const info = this.analysisOn ? this.analysis : null;
    this.renderArrow(info?.bestMove ?? null);
    if (!this.analysisOn) {
      analysisMetaEl.textContent = '';
      return;
    }
    if (!info) {
      evalScoreEl.textContent = '…';
      evalFillEl.style.width = '50%';
      analysisMetaEl.textContent = '';
      evalLineEl.textContent = this.game.isOver ? 'The game is over.'
        : this.engineToMove ? 'Paused while the engine thinks.' : 'Thinking…';
      return;
    }
    // The engine scores for the side to move; show everything from White's side.
    const sign = this.game.turn === 'w' ? 1 : -1;
    let whiteShare;
    if (info.mate !== null) {
      const mate = info.mate * sign;
      evalScoreEl.textContent = mate > 0 ? `#${mate}` : `#-${-mate}`;
      whiteShare = mate > 0 ? 1 : 0;
    } else {
      const pawns = (info.score * sign) / 100;
      evalScoreEl.textContent = `${pawns > 0 ? '+' : pawns < 0 ? '−' : ''}${Math.abs(pawns).toFixed(2)}`;
      whiteShare = 1 / (1 + Math.exp(-info.score * sign / 250));
    }
    evalFillEl.style.width = `${(whiteShare * 100).toFixed(1)}%`;
    analysisMetaEl.textContent = `Depth ${info.depth}${info.done ? '' : '…'}`;
    evalLineEl.replaceChildren(...this.lineNodes(info.pv));
  }

  // The engine's line in figurine notation with move numbers: "12… ♘f6 13. e5".
  lineNodes(pv) {
    const pos = new Position(this.game.pos.fen());
    const nodes = [];
    pv.slice(0, PV_PLIES).forEach((uci, i) => {
      const move = parseUci(pos, uci);
      if (move === null) return;
      const white = pos.turn === 0;
      if (white || i === 0) {
        const number = `${pos.fullmoveNumber}${white ? '.' : '…'}`;
        nodes.push(Object.assign(document.createElement('span'), { className: 'num', textContent: `${i ? ' ' : ''}${number} ` }));
      } else {
        nodes.push(document.createTextNode(' '));
      }
      const span = document.createElement('span');
      if (i === 0) span.className = 'best';
      span.append(...figurines(pos.toSan(move)));
      nodes.push(span);
      pos.makeMove(move);
    });
    return nodes;
  }

  // An arrow over the board for the engine's best move.
  renderArrow(uci) {
    if (!uci) {
      arrowsEl.replaceChildren();
      return;
    }
    const from = parseUci(this.game.pos, uci);
    if (from === null) {
      arrowsEl.replaceChildren();
      return;
    }
    const a = moveFrom(from);
    const b = moveTo(from);
    const x1 = this.col(a) + 0.5, y1 = this.row(a) + 0.5;
    const x2 = this.col(b) + 0.5, y2 = this.row(b) + 0.5;
    const length = Math.hypot(x2 - x1, y2 - y1);
    const angle = (Math.atan2(y2 - y1, x2 - x1) * 180) / Math.PI;
    // Drawn pointing right from the origin, then rotated into place.
    const shaft = 0.13, head = 0.36, headLength = 0.4, end = length - 0.12;
    const path = `M0 ${-shaft / 2}H${end - headLength}V${-head / 2}L${end} 0L${end - headLength} ${head / 2}V${shaft / 2}H0Z`;
    const arrow = document.createElementNS('http://www.w3.org/2000/svg', 'path');
    arrow.setAttribute('class', 'arrow');
    arrow.setAttribute('d', path);
    arrow.setAttribute('transform', `translate(${x1} ${y1}) rotate(${angle})`);
    arrowsEl.replaceChildren(arrow);
  }

  // ---------------------------------------------------------------- style

  applyStyle() {
    pieceSet = this.style.pieces;
    document.documentElement.dataset.board = this.style.board;
    try {
      localStorage.setItem(STYLE_KEY, JSON.stringify(this.style));
    } catch {
      // Storage unavailable: the choice lasts until the page is closed.
    }
  }

  setStyle(change) {
    Object.assign(this.style, change);
    this.applyStyle();
    this.buildStyleOptions();
    this.render();
    // The options were rebuilt; keep focus on the one just chosen.
    const group = 'pieces' in change ? pieceOptionsEl : boardOptionsEl;
    group.querySelector('[aria-pressed="true"]').focus({ preventScroll: true });
  }

  openStyle() {
    this.buildStyleOptions();
    styleSheetEl.hidden = false;
    styleButton.setAttribute('aria-expanded', 'true');
    pieceOptionsEl.querySelector('[aria-pressed="true"]')?.focus({ preventScroll: true });
  }

  closeStyle() {
    styleSheetEl.hidden = true;
    styleButton.setAttribute('aria-expanded', 'false');
    styleButton.focus({ preventScroll: true });
  }

  // Each option previews a 2 x 2 corner of the board: piece sets on the
  // current board, boards with the current pieces.
  buildStyleOptions() {
    const option = (label, pressed, boardId, setId, onPick) => {
      const button = document.createElement('button');
      button.type = 'button';
      button.className = 'option';
      button.setAttribute('aria-pressed', String(pressed));
      const preview = Object.assign(document.createElement('span'), { className: 'option-preview' });
      preview.dataset.board = boardId;
      for (const [name, dark] of [['bN', false], ['bQ', true], ['wK', true], ['wN', false]]) {
        const square = Object.assign(document.createElement('span'), { className: dark ? 'dark' : '' });
        const piece = Object.assign(document.createElement('span'), { className: 'piece' });
        paintPiece(piece, name, setId);
        square.append(piece);
        preview.append(square);
      }
      button.append(preview, label);
      button.addEventListener('click', onPick);
      return button;
    };
    const { pieces, board } = this.style;
    pieceOptionsEl.replaceChildren(...PIECE_SETS.map((set) =>
      option(set.name, set.id === pieces, board, set.id, () => this.setStyle({ pieces: set.id }))));
    boardOptionsEl.replaceChildren(...BOARDS.map((b) =>
      option(b.name, b.id === board, b.id, pieces, () => this.setStyle({ board: b.id }))));
  }
}

new BoardUI();
