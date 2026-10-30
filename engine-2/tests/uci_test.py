"""Drives the engine binary over UCI like a GUI would.

Usage: python uci_test.py path/to/engine2
"""
import subprocess
import sys
import time

failures = 0


def check(cond, what):
    global failures
    if not cond:
        print("FAIL:", what)
        failures += 1


class Engine:
    def __init__(self, path):
        self.p = subprocess.Popen([path], stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True, bufsize=1)

    def send(self, line):
        self.p.stdin.write(line + "\n")
        self.p.stdin.flush()

    def read_until(self, prefix, timeout=10.0):
        """Returns all lines up to and including the first starting with prefix."""
        lines = []
        deadline = time.time() + timeout
        while time.time() < deadline:
            line = self.p.stdout.readline()
            if not line:
                break
            line = line.rstrip("\n")
            lines.append(line)
            if line.startswith(prefix):
                return lines
        raise RuntimeError(f"no '{prefix}' line; got {lines}")


def main():
    e = Engine(sys.argv[1])

    e.send("uci")
    lines = e.read_until("uciok")
    check(any(l.startswith("id name") for l in lines), "id name sent")
    check(any("Move Overhead" in l for l in lines), "Move Overhead option declared")

    e.send("isready")
    e.read_until("readyok")

    e.send("setoption name Move Overhead value 30")
    e.send("ucinewgame")
    e.send("position startpos moves e2e4 e7e5 g1f3")
    e.send("go movetime 100")
    best = e.read_until("bestmove")[-1].split()[1]
    check(best in {"b8c6", "g8f6", "d7d6", "f7f6", "d8f6", "d8e7", "d8g5", "d8h4", "f8e7", "f8d6",
                   "f8c5", "f8b4", "f8a3", "a7a6", "a7a5", "b7b6", "b7b5", "c7c6", "c7c5", "d7d5",
                   "f7f5", "g7g6", "g7g5", "h7h6", "h7h5", "b8a6", "g8e7", "g8h6", "e8e7"},
          f"bestmove {best} is a legal black reply")

    # The board command shows the position the moves led to.
    e.send("d")
    lines = e.read_until("Checkers")
    check(any("Fen: rnbqkbnr/pppp1ppp/8/4p3/4P3/5N2/PPPP1PPP/RNBQKB1R b KQkq - 1 2" in l for l in lines),
          "position after the moves")

    # go infinite must not answer until stop, and must answer after it.
    e.send("position fen 4k3/8/8/8/8/8/8/4K2R w K - 0 1")
    e.send("go infinite")
    e.read_until("info")
    e.send("isready")
    lines = e.read_until("readyok")
    check(not any(l.startswith("bestmove") for l in lines), "no bestmove before stop in infinite mode")
    e.send("stop")
    e.read_until("bestmove")

    # Clock-based go.
    e.send("position startpos")
    e.send("go wtime 60000 btime 60000 winc 1000 binc 1000")
    e.read_until("bestmove")

    # Checkmated side: no legal move.
    e.send("position fen rnb1kbnr/pppp1ppp/8/4p3/6Pq/5P2/PPPPP2P/RNBQKBNR w KQkq - 1 3")
    e.send("go movetime 50")
    check(e.read_until("bestmove")[-1] == "bestmove 0000", "bestmove 0000 when mated")

    # Bad input is reported, not fatal.
    e.send("position startpos moves e2e5")
    e.send("isready")
    lines = e.read_until("readyok")
    check(any("illegal move" in l for l in lines), "illegal move reported")
    e.send("position startpos")
    e.send("perft 3")
    check(e.read_until("Nodes searched")[-1].startswith("Nodes searched: 8902"), "perft from UCI")

    e.send("quit")
    try:
        e.p.wait(timeout=5)
    except subprocess.TimeoutExpired:
        check(False, "quit exits")
        e.p.kill()
    check(e.p.returncode == 0, "clean exit")

    print("all UCI tests pass" if not failures else f"{failures} FAILED")
    sys.exit(1 if failures else 0)


if __name__ == "__main__":
    main()
