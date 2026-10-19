// Engine 2. With no arguments it speaks UCI on stdin/stdout (what Cute Chess
// and other GUIs expect). With arguments it runs a tool:
//   engine2 perft <depth> [fen]    node count
//   engine2 divide <depth> [fen]   node count per root move
//   engine2 bench [depth]          fixed-depth searches: node signature and speed
//   engine2 perftbench             perft speed on the start position
//   engine2 eval                   static evaluation of each FEN read from stdin
//   engine2 datagen <file> <positions> [nodes] [seed]
//                                  self-play training data for NNUE (datagen.h)
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <string>

#include "datagen.h"
#include "evaluate.h"
#include "movegen.h"
#include "position.h"
#include "uci.h"

using namespace chess;

namespace {

std::string join_fen(int argc, char** argv, int first) {
  std::string fen;
  for (int i = first; i < argc; i++) fen += (i > first ? " " : "") + std::string(argv[i]);
  return fen.empty() ? START_FEN : fen;
}

int usage() {
  std::cerr << "usage: engine2 perft|divide <depth> [fen]\n       engine2 bench [depth] | perftbench | eval\n"
               "       engine2 datagen <file> <positions> [nodes] [seed]\n";
  return 2;
}

}  // namespace

int main(int argc, char** argv) {
  init_all();
  if (argc < 2) {
    uci_loop();
    return 0;
  }
  const std::string cmd = argv[1];

  if (cmd == "bench") {
    run_bench(argc >= 3 ? std::atoi(argv[2]) : 0);
    return 0;
  }

  if (cmd == "datagen") {
    if (argc < 4) return usage();
    run_datagen(argv[2], std::strtoull(argv[3], nullptr, 10), argc >= 5 ? std::atoi(argv[4]) : 5000,
                argc >= 6 ? std::strtoull(argv[5], nullptr, 10) : 1);
    return 0;
  }

  if (cmd == "eval") {
    std::string line;
    Position pos;
    while (std::getline(std::cin, line)) {
      if (line.empty()) continue;
      if (!pos.set(line)) {
        std::cout << "bad FEN\n";
        continue;
      }
      std::cout << evaluate(pos) << "\n";
    }
    return 0;
  }

  if (cmd == "perftbench") {
    Position pos;
    auto t0 = std::chrono::steady_clock::now();
    std::uint64_t nodes = perft(pos, 6);
    double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    std::cout << nodes << " nodes in " << secs << " s, " << std::uint64_t(nodes / secs) << " nps\n";
    return 0;
  }

  if ((cmd == "perft" || cmd == "divide") && argc >= 3) {
    int depth = std::atoi(argv[2]);
    Position pos;
    if (!pos.set(join_fen(argc, argv, 3))) {
      std::cerr << "bad FEN\n";
      return 1;
    }
    auto t0 = std::chrono::steady_clock::now();
    std::uint64_t total = 0;
    if (cmd == "divide" && depth >= 1) {
      MoveList list;
      generate_legal(pos, list);
      for (Move m : list) {
        pos.make_move(m);
        std::uint64_t n = perft(pos, depth - 1);
        pos.unmake_move();
        std::cout << m.uci() << ": " << n << "\n";
        total += n;
      }
    } else {
      total = perft(pos, depth);
    }
    double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    std::cout << "nodes " << total << " (" << secs << " s)\n";
    return 0;
  }
  return usage();
}
