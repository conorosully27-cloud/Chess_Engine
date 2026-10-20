// WebAssembly entry points for the browser app (engine-1/app/engine2-worker.js),
// built by scripts/build_wasm.sh instead of main.cpp. The search runs on the
// calling thread, which in the browser is the app's Web Worker: the page ends
// the worker to stop a search early, as it does for Engine 1.
#include <emscripten/emscripten.h>

#include <sstream>
#include <string>

#include "movegen.h"
#include "nnue.h"
#include "search.h"
#include "uci.h"

using namespace chess;

namespace {

TranspositionTable* tt = nullptr;
SearchWorker* worker = nullptr;
std::string reply;  // returned to JavaScript; valid until the next call

}  // namespace

// Each UCI "info" line goes to the page as soon as a depth is done.
EM_JS(void, post_info, (const char* line), {
  if (Module.onInfo) Module.onInfo(UTF8ToString(line));
});

extern "C" {

EMSCRIPTEN_KEEPALIVE void engine_init(int hash_mb) {
  init_all();
  nnue::load_default();
  tt = new TranspositionTable(hash_mb);
  worker = new SearchWorker(*tt);
}

EMSCRIPTEN_KEEPALIVE void engine_new_game() {
  tt->clear();
  worker->new_game();
}

// Searches the position reached by `moves` (UCI, space-separated) from the
// start, or from a FEN if `moves` is "fen <FEN> moves <moves>", for at most
// movetime_ms milliseconds, and to at most `depth` plies if depth > 0.
// Returns "bestmove <move>" ("0000" if there is no legal move).
EMSCRIPTEN_KEEPALIVE const char* engine_go(const char* moves, int movetime_ms, int depth) {
  Position pos;
  std::istringstream in(moves);
  std::string text;
  if (in >> text && text == "fen") {
    std::string fen, field;
    while (in >> field && field != "moves") fen += (fen.empty() ? "" : " ") + field;
    pos.set(fen);
  } else {
    in.clear();
    in.seekg(0);
  }
  while (in >> text) {
    const Move m = parse_uci_move(pos, text);
    if (!m) break;
    pos.make_move(m);
  }
  SearchLimits limits;
  limits.movetime = movetime_ms;
  limits.depth = depth;
  const TimeBudget budget = allocate_time(limits, pos.side_to_move(), 0);
  const SearchResult r = worker->search(pos, limits, budget, nullptr,
                                        [](const std::string& line) { post_info(line.c_str()); });
  reply = "bestmove " + r.best.uci();
  return reply.c_str();
}

// The same fixed-depth searches as "engine2 bench", printed to the console:
// the node count must match the native build's.
EMSCRIPTEN_KEEPALIVE void engine_bench() { run_bench(0); }

}  // extern "C"
