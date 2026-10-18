// UCI front end: reads commands from stdin, answers on stdout.
#pragma once

#include <string>

namespace chess {

// Writes one line to stdout and flushes; safe to call from any thread.
void uci_send(const std::string& line);

// Runs the UCI loop until "quit" or end of input.
void uci_loop();

}  // namespace chess
