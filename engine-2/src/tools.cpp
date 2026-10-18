// Tools for building the opening suite used in engine matches
// (engine-1/test/openings.epd). Lines look like "<fen> ; <uci moves from the start>".
//
//   engine2 genopenings <count> <plies> <seed> <depth> <max cp> < existing.epd > new.epd
//     Random openings: <plies> random legal moves from the start, an equal
//     share for each White first move, none repeating a position already in
//     the existing file (read from stdin) or in each other, kept only when a
//     <depth> search finds neither side more than <max cp> ahead.
//
//   engine2 scoreepd <depth> < suite.epd
//     Searches each line's position to <depth> and prints "<score>\t<line>",
//     the score in centipawns from White's point of view.
#include "tools.h"

#include <cstdlib>
#include <iostream>
#include <random>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "movegen.h"
#include "search.h"

namespace chess {

namespace {

// FEN without the move counters, and with the en passant square only when a
// pawn can actually take there, so the same position always gives the same key.
std::string position_key(const Position& pos) {
  std::istringstream in(pos.fen());
  std::string placement, side, castling, ep;
  in >> placement >> side >> castling >> ep;
  const Square s = pos.ep_square();
  const Color us = pos.side_to_move();
  if (s == NO_SQUARE || !(PawnAttacks[~us][s] & pos.pieces(us, PAWN))) ep = "-";
  return placement + " " + side + " " + castling + " " + ep;
}

// The FEN as the existing suite writes it: key plus "0 <move number>".
std::string suite_fen(const Position& pos) {
  return position_key(pos) + " " + std::to_string(pos.halfmove_clock()) + " " +
         std::to_string(pos.fullmove_number());
}

int white_score(Position& pos, int depth, TranspositionTable& tt, SearchWorker& worker) {
  tt.clear();
  worker.new_game();
  SearchLimits limits;
  limits.depth = depth;
  const SearchResult r = worker.search(pos, limits, TimeBudget());
  return pos.side_to_move() == WHITE ? r.score : -r.score;
}

}  // namespace

int gen_openings(int count, int plies, std::uint64_t seed, int depth, int max_cp) {
  std::set<std::string> seen;
  std::string line;
  while (std::getline(std::cin, line)) {
    if (line.empty() || line[0] == '#') continue;
    Position pos;
    if (pos.set(line.substr(0, line.find(';')))) seen.insert(position_key(pos));
  }

  std::mt19937_64 rng(seed);
  TranspositionTable tt(16);
  SearchWorker worker(tt);
  MoveList first_moves;
  Position start;
  generate_legal(start, first_moves);
  const int per_first_move = (count + first_moves.size() - 1) / first_moves.size();
  int written = 0;

  for (Move first : first_moves) {
    int found = 0;
    for (int tries = 0; found < per_first_move && written < count && tries < per_first_move * 200; tries++) {
      Position pos;
      std::string moves = first.uci();
      pos.make_move(first);
      bool ok = true;
      for (int ply = 1; ply < plies; ply++) {
        MoveList legal;
        generate_legal(pos, legal);
        if (legal.size() == 0) {
          ok = false;
          break;
        }
        const Move m = legal[int(rng() % legal.size())];
        moves += " " + m.uci();
        pos.make_move(m);
      }
      MoveList replies;
      generate_legal(pos, replies);
      if (!ok || replies.size() == 0 || pos.in_check()) continue;
      const std::string key = position_key(pos);
      if (seen.count(key)) continue;
      if (std::abs(white_score(pos, depth, tt, worker)) > max_cp) continue;
      seen.insert(key);
      std::cout << suite_fen(pos) << " ; " << moves << "\n";
      found++;
      written++;
    }
  }
  std::cerr << written << " openings written\n";
  return written == count ? 0 : 1;
}

int score_epd(int depth) {
  TranspositionTable tt(16);
  SearchWorker worker(tt);
  std::string line;
  while (std::getline(std::cin, line)) {
    if (line.empty() || line[0] == '#') continue;
    Position pos;
    if (!pos.set(line.substr(0, line.find(';')))) {
      std::cerr << "bad FEN: " << line << "\n";
      return 1;
    }
    std::cout << white_score(pos, depth, tt, worker) << "\t" << line << std::endl;
  }
  return 0;
}

}  // namespace chess
