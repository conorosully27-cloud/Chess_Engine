#include "uci.h"

#include <algorithm>
#include <chrono>
#include <iostream>
#include <mutex>
#include <sstream>

#include "evaluate.h"
#include "movegen.h"
#include "nnue.h"
#include "position.h"
#include "search.h"

namespace chess {

namespace {

constexpr const char* ENGINE_NAME = "Engine 2.4";
constexpr const char* ENGINE_AUTHOR = "Conor O'Sullivan";

std::mutex output_mutex;

// "position [startpos | fen <fen>] [moves <m1> <m2> ...]"
void set_position(Position& pos, std::istringstream& in) {
  std::string token, fen;
  in >> token;
  if (token == "startpos") {
    fen = START_FEN;
    in >> token;  // "moves", if present
  } else if (token == "fen") {
    while (in >> token && token != "moves") fen += token + " ";
  } else {
    return;
  }
  if (!pos.set(fen)) uci_send("info string invalid FEN, using the start position");
  while (in >> token) {
    Move m = parse_uci_move(pos, token);
    if (!m) {
      uci_send("info string illegal move " + token + ", ignoring the rest");
      break;
    }
    pos.make_move(m);
  }
}

SearchLimits parse_go(std::istringstream& in) {
  SearchLimits limits;
  std::string token;
  while (in >> token) {
    if (token == "wtime") in >> limits.wtime;
    else if (token == "btime") in >> limits.btime;
    else if (token == "winc") in >> limits.winc;
    else if (token == "binc") in >> limits.binc;
    else if (token == "movestogo") in >> limits.movestogo;
    else if (token == "movetime") in >> limits.movetime;
    else if (token == "depth") in >> limits.depth;
    else if (token == "nodes") in >> limits.nodes;
    else if (token == "infinite") limits.infinite = true;
  }
  return limits;
}

// "setoption name <name> value <value>"; names may contain spaces.
void set_option(std::istringstream& in, int& move_overhead, Searcher& searcher) {
  std::string token, name, value;
  in >> token;  // "name"
  while (in >> token && token != "value") name += (name.empty() ? "" : " ") + token;
  while (in >> token) value += (value.empty() ? "" : " ") + token;
  if (name == "EvalFile") {
    // An NNUE network file; empty (or "<empty>") is the built-in network,
    // "classical" the hand-written evaluation.
    if (value.empty() || value == "<empty>") {
      nnue::load_default();
    } else if (value == "classical") {
      nnue::unload();
    } else if (!nnue::load(value)) {
      nnue::load_default();
      uci_send("info string cannot load network " + value + "; using the built-in one");
    }
    clear_eval_cache();
    return;
  }
  int n = 0;
  try {
    n = std::stoi(value);
  } catch (...) {
    uci_send("info string bad value for " + name);
    return;
  }
  if (name == "Move Overhead") {
    move_overhead = std::clamp(n, 0, 5000);
  } else if (name == "Hash") {
    searcher.set_hash_mb(std::size_t(std::clamp(n, 1, 65536)));
  } else {
    uci_send("info string unknown option " + name);
  }
}

void print_board(const Position& pos) {
  std::string out;
  for (int r = 7; r >= 0; r--) {
    out += " +---+---+---+---+---+---+---+---+\n |";
    for (int f = 0; f < 8; f++) {
      Piece pc = pos.piece_on(r * 8 + f);
      out += ' ';
      out += pc == NO_PIECE ? ' ' : "PNBRQKpnbrqk"[pc];
      out += " |";
    }
    out += " " + std::to_string(r + 1) + "\n";
  }
  out += " +---+---+---+---+---+---+---+---+\n   a   b   c   d   e   f   g   h\n\n";
  std::ostringstream key;
  key << std::hex << pos.key();
  out += "Fen: " + pos.fen() + "\nKey: " + key.str() + "\nCheckers:";
  Bitboard c = pos.checkers();
  while (c) {
    Square s = pop_lsb(c);
    out += ' ';
    out += char('a' + file_of(s));
    out += char('1' + rank_of(s));
  }
  uci_send(out);
}

}  // namespace

void uci_send(const std::string& line) {
  std::lock_guard<std::mutex> lock(output_mutex);
  std::cout << line << std::endl;
}

void uci_loop() {
  Position pos;
  Searcher searcher;
  searcher.set_hash_mb(64);
  int move_overhead = 50;
  std::string line;

  while (std::getline(std::cin, line)) {
    std::istringstream in(line);
    std::string cmd;
    in >> cmd;

    if (cmd == "uci") {
      uci_send(std::string("id name ") + ENGINE_NAME);
      uci_send(std::string("id author ") + ENGINE_AUTHOR);
      uci_send("option name Hash type spin default 64 min 1 max 65536");
      uci_send("option name Move Overhead type spin default 50 min 0 max 5000");
      uci_send("option name EvalFile type string default <empty>");
      uci_send("uciok");
    } else if (cmd == "isready") {
      uci_send("readyok");
    } else if (cmd == "ucinewgame") {
      searcher.stop();
      searcher.wait();
      searcher.new_game();
      pos.set(START_FEN);
    } else if (cmd == "setoption") {
      searcher.stop();
      searcher.wait();
      set_option(in, move_overhead, searcher);
    } else if (cmd == "position") {
      searcher.stop();
      searcher.wait();
      set_position(pos, in);
    } else if (cmd == "go") {
      searcher.start(pos, parse_go(in), move_overhead);
    } else if (cmd == "stop") {
      searcher.stop();
      searcher.wait();
    } else if (cmd == "quit") {
      break;
    } else if (cmd == "d") {
      print_board(pos);
    } else if (cmd == "eval") {
      uci_send("Evaluation: " + std::to_string(evaluate(pos)) + " (side to move)");
    } else if (cmd == "bench") {
      searcher.stop();
      searcher.wait();
      int depth = 0;
      in >> depth;
      run_bench(depth);
    } else if (cmd == "perft") {
      int depth = 1;
      in >> depth;
      depth = std::max(depth, 1);
      auto t0 = std::chrono::steady_clock::now();
      std::uint64_t total = 0;
      MoveList list;
      generate_legal(pos, list);
      for (Move m : list) {
        pos.make_move(m);
        std::uint64_t n = depth > 1 ? perft(pos, depth - 1) : 1;
        pos.unmake_move();
        uci_send(m.uci() + ": " + std::to_string(n));
        total += n;
      }
      auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
      uci_send("\nNodes searched: " + std::to_string(total) + " (" + std::to_string(ms) + " ms)");
    } else if (!cmd.empty()) {
      uci_send("info string unknown command " + cmd);
    }
  }
  searcher.stop();
  searcher.wait();
}

}  // namespace chess
