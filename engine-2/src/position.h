// The chess position: pieces, side to move, castling, en passant, plus the
// history needed to unmake moves and detect repetitions.
#pragma once

#include <string>
#include <vector>

#include "bitboard.h"
#include "types.h"

namespace chess {

constexpr const char* START_FEN = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";

// Everything that changes with a move and cannot be recomputed on unmake.
struct StateInfo {
  Key key = 0;
  Move move;                  // the move that led here (empty for the root or a null move)
  Piece captured = NO_PIECE;
  int castling = 0;           // CastlingRight bits
  Square ep = NO_SQUARE;      // square a pawn could capture onto en passant
  int halfmove = 0;           // plies since the last capture or pawn move
  int fullmove = 1;
  Bitboard checkers = 0;      // enemy pieces giving check to the side to move
  Bitboard pinned = 0;        // pieces of the side to move pinned to their king
};

class Position {
 public:
  Position() { set(START_FEN); }
  explicit Position(const std::string& fen) { set(fen); }

  // Returns false (leaving a valid start position) if the FEN cannot be read.
  bool set(const std::string& fen);
  std::string fen() const;

  void make_move(Move m);  // m must be legal
  void unmake_move();
  void make_null_move();   // side to move passes; not allowed while in check
  void unmake_null_move();

  Color side_to_move() const { return side_; }
  Piece piece_on(Square s) const { return board_[s]; }
  Bitboard pieces() const { return occ_; }
  Bitboard pieces(Color c) const { return by_color_[c]; }
  Bitboard pieces(PieceType pt) const { return by_type_[pt]; }
  Bitboard pieces(Color c, PieceType pt) const { return by_color_[c] & by_type_[pt]; }
  Bitboard pieces(Color c, PieceType a, PieceType b) const { return by_color_[c] & (by_type_[a] | by_type_[b]); }
  Square king_square(Color c) const { return lsb(pieces(c, KING)); }

  int castling_rights() const { return st().castling; }
  Square ep_square() const { return st().ep; }
  int halfmove_clock() const { return st().halfmove; }
  int fullmove_number() const { return st().fullmove; }
  Key key() const { return st().key; }
  Bitboard checkers() const { return st().checkers; }
  Bitboard pinned() const { return st().pinned; }
  bool in_check() const { return st().checkers != 0; }
  Move last_move() const { return st().move; }
  int game_ply() const { return int(states_.size()) - 1; }

  // All pieces of both colors that attack s, given an occupancy.
  Bitboard attackers_to(Square s, Bitboard occ) const;
  bool is_attacked(Square s, Color by) const { return attackers_to(s, occ_) & pieces(by); }

  // Full legality test for a pseudo-legal move (see movegen.h).
  bool legal(Move m) const;

  // Rules of draw.
  int repetitions() const;  // earlier occurrences of the current position
  bool is_repetition() const { return repetitions() >= 1; }
  bool is_threefold() const { return repetitions() >= 2; }
  bool is_fifty_move() const { return st().halfmove >= 100; }
  bool insufficient_material() const;

  // Recomputes the hash from scratch; used by tests to check the incremental one.
  Key compute_key() const;
  // Checks that the bitboards, mailbox and king counts agree; used by tests.
  bool is_consistent() const;

 private:
  const StateInfo& st() const { return states_.back(); }
  void put_piece(Piece pc, Square s);
  void remove_piece(Square s);
  void move_piece(Square from, Square to);
  void update_check_info(StateInfo& s) const;
  bool ep_capturable(Square ep, Color stm) const;

  Piece board_[64];
  Bitboard by_type_[PIECE_TYPE_NB];
  Bitboard by_color_[COLOR_NB];
  Bitboard occ_;
  Color side_;
  std::vector<StateInfo> states_;
};

// Initializes attack tables and hash keys. Call once at startup.
void init_all();

}  // namespace chess
