// Usage: npm run perft -- <depth> [fen]
// Prints the per-move breakdown, the total and the speed. Compare a breakdown
// against another engine's to pinpoint which move a bug hides under.
import { Position, START_FEN } from '../src/index.js';

const depth = Number(process.argv[2] ?? 5);
const fen = process.argv.slice(3).join(' ') || START_FEN;
const pos = new Position(fen);

const start = performance.now();
const divide = pos.perftDivide(depth);
const seconds = (performance.now() - start) / 1000;
const total = Object.values(divide).reduce((a, b) => a + b, 0);

for (const [move, nodes] of Object.entries(divide).sort()) console.log(`${move}: ${nodes}`);
console.log(`\nNodes: ${total}  Time: ${seconds.toFixed(2)}s  Speed: ${Math.round(total / seconds).toLocaleString()} nodes/s`);
