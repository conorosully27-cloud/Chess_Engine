#include "position.h"

#include <algorithm>
#include <sstream>

namespace chess {

namespace {

namespace Zobrist {
Key psq[PIECE_NB][64];
Key side;
Key castling[16];
Key ep_file[8];
}  // namespace Zobrist

int CastlingMask[64];

const char PIECE_CHARS[] = "PNBRQKpnbrqk";

}  // namespace

void init_all() {
  init_bitboards();

  std::uint64_t s = 0x9E3779B97F4A7C15ULL;
  auto next = [&s]() {
    s ^= s >> 12;
    s ^= s << 25;
    s ^= s >> 27;
    return s * 2685821657736338717ULL;
  };
  for (int p = 0; p < PIECE_NB; p++)
    for (int sq = 0; sq < 64; sq++) Zobrist::psq[p][sq] = next();
  Zobrist::side = next();
  for (int i = 0; i < 16; i++) Zobrist::castling[i] = next();
  for (int i = 0; i < 8; i++) Zobrist::ep_file[i] = next();

  std::fill(std::begin(CastlingMask), std::end(CastlingMask), 15);
  CastlingMask[0] &= ~WHITE_OOO;
  CastlingMask[4] &= ~(WHITE_OO | WHITE_OOO);
  CastlingMask[7] &= ~WHITE_OO;
  CastlingMask[56] &= ~BLACK_OOO;
  CastlingMask[60] &= ~(BLACK_OO | BLACK_OOO);
  CastlingMask[63] &= ~BLACK_OO;
}

void Position::put_piece(Piece pc, Square s) {
  Bitboard b = square_bb(s);
  board_[s] = pc;
  psq_mg_ += psqt::PSQ.mg[pc][s];
  psq_eg_ += psqt::PSQ.eg[pc][s];
  phase_ += psqt::PHASE_WEIGHT[type_of(pc)];
  by_type_[type_of(pc)] |= b;
  by_color_[color_of(pc)] |= b;
  occ_ |= b;
}

void Position::remove_piece(Square s) {
  Piece pc = board_[s];
  Bitboard b = square_bb(s);
  board_[s] = NO_PIECE;
  psq_mg_ -= psqt::PSQ.mg[pc][s];
  psq_eg_ -= psqt::PSQ.eg[pc][s];
  phase_ -= psqt::PHASE_WEIGHT[type_of(pc)];
  by_type_[type_of(pc)] &= ~b;
  by_color_[color_of(pc)] &= ~b;
  occ_ &= ~b;
}

void Position::move_piece(Square from, Square to) {
  Piece pc = board_[from];
  Bitboard b = square_bb(from) | square_bb(to);
  board_[from] = NO_PIECE;
  board_[to] = pc;
  psq_mg_ += psqt::PSQ.mg[pc][to] - psqt::PSQ.mg[pc][from];
  psq_eg_ += psqt::PSQ.eg[pc][to] - psqt::PSQ.eg[pc][from];
  by_type_[type_of(pc)] ^= b;
  by_color_[color_of(pc)] ^= b;
  occ_ ^= b;
}

bool Position::set(const std::string& fen) {
  std::fill(std::begin(board_), std::end(board_), NO_PIECE);
  std::fill(std::begin(by_type_), std::end(by_type_), 0);
  std::fill(std::begin(by_color_), std::end(by_color_), 0);
  occ_ = 0;
  psq_mg_ = psq_eg_ = phase_ = 0;
  side_ = WHITE;
  states_.clear();
  states_.reserve(1024);

  std::istringstream in(fen);
  std::string placement, side, castling, ep;
  int halfmove = 0, fullmove = 1;
  in >> placement >> side >> castling >> ep;
  if (in) {
    in >> halfmove >> fullmove;
    if (!in) { halfmove = 0; fullmove = 1; }
  }

  bool ok = !placement.empty() && !side.empty();
  int rank = 7, file = 0;
  for (char c : placement) {
    if (!ok) break;
    if (c == '/') {
      ok = file == 8 && rank > 0;
      rank--;
      file = 0;
    } else if (c >= '1' && c <= '8') {
      file += c - '0';
    } else {
      const char* p = std::find(std::begin(PIECE_CHARS), std::end(PIECE_CHARS) - 1, c);
      if (p == std::end(PIECE_CHARS) - 1 || file > 7) { ok = false; break; }
      put_piece(Piece(p - PIECE_CHARS), rank * 8 + file);
      file++;
    }
  }
  ok = ok && rank == 0 && file == 8;
  ok = ok && popcount(pieces(WHITE, KING)) == 1 && popcount(pieces(BLACK, KING)) == 1;
  ok = ok && (side == "w" || side == "b");

  StateInfo s;
  s.halfmove = halfmove;
  s.fullmove = std::max(fullmove, 1);
  if (ok) {
    side_ = side == "w" ? WHITE : BLACK;
    for (char c : castling) {
      if (c == 'K') s.castling |= WHITE_OO;
      else if (c == 'Q') s.castling |= WHITE_OOO;
      else if (c == 'k') s.castling |= BLACK_OO;
      else if (c == 'q') s.castling |= BLACK_OOO;
    }
    // Drop rights whose king or rook is not on its home square.
    if (board_[4] != W_KING) s.castling &= ~(WHITE_OO | WHITE_OOO);
    if (board_[7] != W_ROOK) s.castling &= ~WHITE_OO;
    if (board_[0] != W_ROOK) s.castling &= ~WHITE_OOO;
    if (board_[60] != B_KING) s.castling &= ~(BLACK_OO | BLACK_OOO);
    if (board_[63] != B_ROOK) s.castling &= ~BLACK_OO;
    if (board_[56] != B_ROOK) s.castling &= ~BLACK_OOO;
    if (ep.size() == 2 && ep[0] >= 'a' && ep[0] <= 'h' && (ep[1] == '3' || ep[1] == '6')) {
      s.ep = (ep[1] - '1') * 8 + (ep[0] - 'a');
    }
  }
  states_.push_back(s);
  if (!ok) {
    // Leave a valid position behind rather than a half-parsed one.
    if (fen != START_FEN) set(START_FEN);
    return false;
  }
  states_.back().key = compute_key();
  update_check_info(states_.back());
  return true;
}

std::string Position::fen() const {
  std::string out;
  for (int r = 7; r >= 0; r--) {
    int empty = 0;
    for (int f = 0; f < 8; f++) {
      Piece pc = board_[r * 8 + f];
      if (pc == NO_PIECE) {
        empty++;
        continue;
      }
      if (empty) out += char('0' + empty);
      empty = 0;
      out += PIECE_CHARS[pc];
    }
    if (empty) out += char('0' + empty);
    if (r) out += '/';
  }
  out += side_ == WHITE ? " w " : " b ";
  int c = castling_rights();
  if (!c) out += '-';
  if (c & WHITE_OO) out += 'K';
  if (c & WHITE_OOO) out += 'Q';
  if (c & BLACK_OO) out += 'k';
  if (c & BLACK_OOO) out += 'q';
  out += ' ';
  if (ep_square() == NO_SQUARE) {
    out += '-';
  } else {
    out += char('a' + file_of(ep_square()));
    out += char('1' + rank_of(ep_square()));
  }
  out += ' ' + std::to_string(halfmove_clock()) + ' ' + std::to_string(fullmove_number());
  return out;
}

// The en passant square only counts toward the hash when a pawn can actually
// take there, so that transpositions reached by different move orders match.
bool Position::ep_capturable(Square ep, Color stm) const {
  return ep != NO_SQUARE && (PawnAttacks[~stm][ep] & pieces(stm, PAWN));
}

Key Position::compute_key() const {
  Key k = 0;
  for (Square s = 0; s < 64; s++)
    if (board_[s] != NO_PIECE) k ^= Zobrist::psq[board_[s]][s];
  if (side_ == BLACK) k ^= Zobrist::side;
  k ^= Zobrist::castling[st().castling];
  if (ep_capturable(st().ep, side_)) k ^= Zobrist::ep_file[file_of(st().ep)];
  return k;
}

Bitboard Position::attackers_to(Square s, Bitboard occ) const {
  return (PawnAttacks[BLACK][s] & pieces(WHITE, PAWN)) | (PawnAttacks[WHITE][s] & pieces(BLACK, PAWN)) |
         (KnightAttacks[s] & pieces(KNIGHT)) | (KingAttacks[s] & pieces(KING)) |
         (rook_attacks(s, occ) & (pieces(ROOK) | pieces(QUEEN))) |
         (bishop_attacks(s, occ) & (pieces(BISHOP) | pieces(QUEEN)));
}

void Position::update_check_info(StateInfo& s) const {
  const Color us = side_, them = ~us;
  const Square ksq = king_square(us);
  s.checkers = attackers_to(ksq, occ_) & pieces(them);
  s.pinned = 0;
  Bitboard snipers = (rook_attacks(ksq, 0) & pieces(them, ROOK, QUEEN)) |
                     (bishop_attacks(ksq, 0) & pieces(them, BISHOP, QUEEN));
  while (snipers) {
    Bitboard between = BetweenBB[ksq][pop_lsb(snipers)] & occ_;
    if (between && !more_than_one(between) && (between & pieces(us))) s.pinned |= between;
  }
}

void Position::make_move(Move m) {
  StateInfo s = states_.back();
  const Color us = side_;
  const Square from = m.from(), to = m.to();
  const int flags = m.flags();
  const Piece pc = board_[from];

  Key k = s.key;
  if (ep_capturable(s.ep, us)) k ^= Zobrist::ep_file[file_of(s.ep)];
  s.move = m;
  s.captured = NO_PIECE;
  s.ep = NO_SQUARE;
  s.halfmove++;
  if (us == BLACK) s.fullmove++;

  if (m.is_capture()) {
    Square capsq = to;
    if (m.is_ep()) capsq = us == WHITE ? to - 8 : to + 8;
    s.captured = board_[capsq];
    k ^= Zobrist::psq[s.captured][capsq];
    remove_piece(capsq);
    s.halfmove = 0;
  }

  if (flags == Move::KING_CASTLE || flags == Move::QUEEN_CASTLE) {
    Square rook_from = flags == Move::KING_CASTLE ? from + 3 : from - 4;
    Square rook_to = flags == Move::KING_CASTLE ? from + 1 : from - 1;
    Piece rook = board_[rook_from];
    k ^= Zobrist::psq[rook][rook_from] ^ Zobrist::psq[rook][rook_to];
    move_piece(rook_from, rook_to);
  }

  if (m.is_promotion()) {
    Piece promo = make_piece(us, m.promotion_type());
    remove_piece(from);
    put_piece(promo, to);
    k ^= Zobrist::psq[pc][from] ^ Zobrist::psq[promo][to];
  } else {
    move_piece(from, to);
    k ^= Zobrist::psq[pc][from] ^ Zobrist::psq[pc][to];
  }

  if (type_of(pc) == PAWN) {
    s.halfmove = 0;
    if (flags == Move::DOUBLE_PUSH) s.ep = (from + to) / 2;
  }

  int rights = s.castling & CastlingMask[from] & CastlingMask[to];
  k ^= Zobrist::castling[s.castling] ^ Zobrist::castling[rights];
  s.castling = rights;

  side_ = ~us;
  k ^= Zobrist::side;
  if (ep_capturable(s.ep, side_)) k ^= Zobrist::ep_file[file_of(s.ep)];
  s.key = k;

  update_check_info(s);
  states_.push_back(s);
}

void Position::unmake_move() {
  const Move m = states_.back().move;
  const Piece captured = states_.back().captured;
  states_.pop_back();
  side_ = ~side_;
  const Color us = side_;
  const Square from = m.from(), to = m.to();
  const int flags = m.flags();

  if (m.is_promotion()) {
    remove_piece(to);
    put_piece(make_piece(us, PAWN), from);
  } else {
    move_piece(to, from);
  }
  if (m.is_capture()) {
    Square capsq = to;
    if (m.is_ep()) capsq = us == WHITE ? to - 8 : to + 8;
    put_piece(captured, capsq);
  }
  if (flags == Move::KING_CASTLE) move_piece(from + 1, from + 3);
  else if (flags == Move::QUEEN_CASTLE) move_piece(from - 1, from - 4);
}

void Position::make_null_move() {
  StateInfo s = states_.back();
  Key k = s.key;
  if (ep_capturable(s.ep, side_)) k ^= Zobrist::ep_file[file_of(s.ep)];
  s.move = Move();
  s.captured = NO_PIECE;
  s.ep = NO_SQUARE;
  s.halfmove++;
  if (side_ == BLACK) s.fullmove++;
  side_ = ~side_;
  s.key = k ^ Zobrist::side;
  update_check_info(s);
  states_.push_back(s);
}

void Position::unmake_null_move() {
  states_.pop_back();
  side_ = ~side_;
}

bool Position::legal(Move m) const {
  const Color us = side_, them = ~us;
  const Square from = m.from(), to = m.to();
  const Square ksq = king_square(us);

  if (m.is_castle()) return true;  // the path was checked when the move was generated

  if (m.is_ep()) {
    // Take the captured pawn off the board and see whether the king is exposed.
    Square capsq = us == WHITE ? to - 8 : to + 8;
    Bitboard occ = (occ_ ^ square_bb(from) ^ square_bb(capsq)) | square_bb(to);
    return !(attackers_to(ksq, occ) & pieces(them) & ~square_bb(capsq));
  }

  if (from == ksq) return !(attackers_to(to, occ_ ^ square_bb(from)) & pieces(them));

  Bitboard checkers = st().checkers;
  if (checkers) {
    if (more_than_one(checkers)) return false;
    Square checker = lsb(checkers);
    if (!((BetweenBB[ksq][checker] | square_bb(checker)) & square_bb(to))) return false;
  }
  if (st().pinned & square_bb(from)) return aligned(from, to, ksq);
  return true;
}

int Position::repetitions() const {
  const int n = int(states_.size());
  const int limit = std::min(st().halfmove, n - 1);
  int count = 0;
  for (int i = 4; i <= limit; i += 2)
    if (states_[n - 1 - i].key == st().key) count++;
  return count;
}

bool Position::insufficient_material() const {
  if (pieces(PAWN) | pieces(ROOK) | pieces(QUEEN)) return false;
  int knights = popcount(pieces(KNIGHT));
  Bitboard bishops = pieces(BISHOP);
  int nb = popcount(bishops);
  if (knights + nb <= 1) return true;
  if (knights == 0) {
    // Bishops all on one square color cannot mate.
    constexpr Bitboard LIGHT = 0x55AA55AA55AA55AAULL;
    return !(bishops & LIGHT) || !(bishops & ~LIGHT);
  }
  return false;
}

bool Position::is_consistent() const {
  Bitboard occ = 0;
  int mg = 0, eg = 0, phase = 0;
  for (Square s = 0; s < 64; s++) {
    Piece pc = board_[s];
    if (pc == NO_PIECE) continue;
    Bitboard b = square_bb(s);
    occ |= b;
    mg += psqt::PSQ.mg[pc][s];
    eg += psqt::PSQ.eg[pc][s];
    phase += psqt::PHASE_WEIGHT[type_of(pc)];
    if (!(by_type_[type_of(pc)] & b) || !(by_color_[color_of(pc)] & b)) return false;
  }
  if (occ != occ_) return false;
  if (mg != psq_mg_ || eg != psq_eg_ || phase != phase_) return false;
  if (by_color_[WHITE] & by_color_[BLACK]) return false;
  if ((by_color_[WHITE] | by_color_[BLACK]) != occ_) return false;
  Bitboard types = 0;
  for (int t = 0; t < PIECE_TYPE_NB; t++) {
    if (types & by_type_[t]) return false;
    types |= by_type_[t];
  }
  return types == occ_ && popcount(pieces(WHITE, KING)) == 1 && popcount(pieces(BLACK, KING)) == 1;
}

}  // namespace chess
