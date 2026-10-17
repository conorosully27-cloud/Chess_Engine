// Basic chess types: colors, pieces, squares and the 16-bit move encoding.
#pragma once

#include <cstdint>
#include <string>

namespace chess {

using Bitboard = std::uint64_t;
using Key = std::uint64_t;

// Squares are 0..63, a1 = 0, b1 = 1, ..., h1 = 7, a2 = 8, ..., h8 = 63.
using Square = int;
constexpr Square NO_SQUARE = 64;

constexpr int file_of(Square s) { return s & 7; }
constexpr int rank_of(Square s) { return s >> 3; }

enum Color : int { WHITE, BLACK, COLOR_NB };
constexpr Color operator~(Color c) { return Color(c ^ 1); }

enum PieceType : int { PAWN, KNIGHT, BISHOP, ROOK, QUEEN, KING, PIECE_TYPE_NB };

// A piece is color * 6 + type, so white pieces are 0..5 and black 6..11.
enum Piece : int {
  W_PAWN, W_KNIGHT, W_BISHOP, W_ROOK, W_QUEEN, W_KING,
  B_PAWN, B_KNIGHT, B_BISHOP, B_ROOK, B_QUEEN, B_KING,
  NO_PIECE,
};
constexpr int PIECE_NB = 12;

constexpr Piece make_piece(Color c, PieceType pt) { return Piece(c * 6 + pt); }
constexpr PieceType type_of(Piece p) { return PieceType(p % 6); }
constexpr Color color_of(Piece p) { return Color(p / 6); }

constexpr Square relative_square(Color c, Square s) { return c == WHITE ? s : s ^ 56; }

enum CastlingRight : int {
  WHITE_OO = 1,
  WHITE_OOO = 2,
  BLACK_OO = 4,
  BLACK_OOO = 8,
};

// A move packs from (6 bits), to (6 bits) and a 4-bit flag:
//   bit 2 (value 4) set: capture; bit 3 (value 8) set: promotion, with the
//   low two bits choosing the piece (0 knight, 1 bishop, 2 rook, 3 queen).
class Move {
 public:
  enum Flag : int {
    QUIET = 0,
    DOUBLE_PUSH = 1,
    KING_CASTLE = 2,
    QUEEN_CASTLE = 3,
    CAPTURE = 4,
    EP = 5,
    PROMO = 8,  // + piece index, + CAPTURE for a capturing promotion
  };

  constexpr Move() = default;
  constexpr Move(Square from, Square to, int flags)
      : data_(static_cast<std::uint16_t>(from | (to << 6) | (flags << 12))) {}

  constexpr Square from() const { return data_ & 63; }
  constexpr Square to() const { return (data_ >> 6) & 63; }
  constexpr int flags() const { return data_ >> 12; }
  constexpr bool is_capture() const { return flags() & 4; }
  constexpr bool is_promotion() const { return flags() & 8; }
  constexpr bool is_ep() const { return flags() == EP; }
  constexpr bool is_castle() const { return flags() == KING_CASTLE || flags() == QUEEN_CASTLE; }
  constexpr PieceType promotion_type() const { return PieceType(KNIGHT + (flags() & 3)); }

  constexpr bool operator==(Move o) const { return data_ == o.data_; }
  constexpr bool operator!=(Move o) const { return data_ != o.data_; }
  constexpr explicit operator bool() const { return data_ != 0; }
  constexpr std::uint16_t raw() const { return data_; }

  std::string uci() const {
    if (!*this) return "0000";
    std::string s;
    s += char('a' + file_of(from()));
    s += char('1' + rank_of(from()));
    s += char('a' + file_of(to()));
    s += char('1' + rank_of(to()));
    if (is_promotion()) s += "nbrq"[flags() & 3];
    return s;
  }

 private:
  std::uint16_t data_ = 0;
};

}  // namespace chess
