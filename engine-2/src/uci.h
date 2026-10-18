// UCI front end: reads commands from stdin, answers on stdout.
#pragma once

#include <string>

namespace chess {

// Writes one line to stdout and flushes; safe to call from any thread.
void uci_send(const std::string& line);

// Runs the UCI loop until "quit" or end of input.
void uci_loop();

// Fixed-depth searches over a set of positions; prints the total node count
// (a signature: it changes only when the search's behavior changes) and speed.
void run_bench(int depth = 0);

}  // namespace chess
