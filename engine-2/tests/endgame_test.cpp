// Endgame scaling rules (src/endgame.h): the known draws get their scale,
// winning material keeps the full score, and Black is treated like White.
#include <cctype>
#include <cstdio>
#include <string>

#include "endgame.h"
#include "test_util.h"

using namespace chess;

namespace {

// The FEN with colours swapped and the board flipped, the side to move too.
std::string mirror(const std::string& fen) {
  std::string placement = fen.substr(0, fen.find(' ')), out;
  for (std::size_t end = placement.size(); end != std::string::npos;) {
    const std::size_t start = placement.rfind('/', end - 1);
    const std::string row = placement.substr(start == std::string::npos ? 0 : start + 1,
                                             end - (start == std::string::npos ? 0 : start + 1));
    if (!out.empty()) out += '/';
    for (char c : row) out += char(std::isalpha(c) ? (std::isupper(c) ? std::tolower(c) : std::toupper(c)) : c);
    end = start;
  }
  const char side = fen[fen.find(' ') + 1];
  return out + (side == 'w' ? " b" : " w") + " - - 0 1";
}

struct Case {
  const char* name;
  const char* fen;  // White to move, White ahead
  int scale;
};

const Case CASES[] = {
    {"lone bishop", "8/8/8/4k3/8/8/8/2B1K3 w - - 0 1", 0},
    {"bishop v pawns", "8/5p2/8/4k3/8/8/8/2B1K3 w - - 0 1", 0},
    {"two knights v king", "8/8/8/4k3/8/8/8/3NKN2 w - - 0 1", 0},
    {"rook v bishop", "8/8/8/4k3/2b5/8/8/R3K3 w - - 0 1", 4},
    {"rook v knight", "8/8/8/4k3/2n5/8/8/R3K3 w - - 0 1", 4},
    {"rook and bishop v rook", "8/8/8/4k3/2r5/8/8/R1B1K3 w - - 0 1", 14},
    {"opposite bishops, a pawn up", "8/5k2/3b4/8/4PK2/8/4B3/8 w - - 0 1", 16},
    {"opposite bishops, two pawns up", "8/5k2/3b4/8/3PPK2/8/4B3/8 w - - 0 1", 32},
    {"wrong bishop, h-pawn", "7k/8/5K2/7P/8/8/4B3/8 w - - 0 1", 0},
    {"wrong bishop, a-pawns", "k7/8/P7/P7/8/8/1K6/4B3 w - - 0 1", 0},
    {"right bishop, a-pawns", "k7/8/P7/P7/8/8/1K6/5B2 w - - 0 1", 64},
    {"wrong bishop, rook and knight pawns", "k7/8/PP6/8/8/8/1K6/4B3 w - - 0 1", 64},
    {"rook pawn, king in corner", "7k/8/6K1/7P/8/8/8/8 w - - 0 1", 0},
    {"right bishop, h-pawn", "7k/8/5K2/7P/8/8/3B4/8 w - - 0 1", 64},
    {"rook pawn, king far", "8/8/8/7P/8/2k5/8/6K1 w - - 0 1", 64},
    {"queen v rook", "8/8/8/4k3/8/2r5/8/3QK3 w - - 0 1", 64},
    {"rook v king", "8/8/8/4k3/8/8/8/R3K3 w - - 0 1", 64},
    {"bishop and knight v king", "8/8/8/4k3/8/8/8/4KBN1 w - - 0 1", 64},
    {"same-colour bishops, a pawn up", "8/5k2/4b3/8/4PK2/8/4B3/8 w - - 0 1", 64},
    {"opposite bishops with rooks", "r7/5k2/3b4/8/4PK2/8/4B3/R7 w - - 0 1", 64},
};

}  // namespace

int main() {
  init_all();
  for (const Case& c : CASES) {
    for (const std::string& fen : {std::string(c.fen), mirror(c.fen)}) {
      Position pos;
      // The side to move is the one ahead, in the mirror too.
      if (!pos.set(fen)) std::printf("bad FEN: %s\n", fen.c_str());
      const int got = endgame_scale(pos, 100);
      if (got != c.scale) {
        std::printf("%s (%s): scale %d, expected %d\n", c.name, fen.c_str(), got, c.scale);
        g_failures++;
      }
    }
  }
  std::printf(g_failures ? "%d FAILED\n" : "all endgame tests pass\n", g_failures);
  return g_failures ? 1 : 0;
}
