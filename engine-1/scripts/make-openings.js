// Builds a set of varied, roughly balanced starting positions for engine
// matches, so long matches don't keep replaying the same few book openings.
//
//   node scripts/make-openings.js [--count 1000] [--plies 6] [--out test/openings.epd]
//
// Each position is reached by random legal moves from the start (6 plies =
// 3 moves each by default). Every White first move gets an equal share and
// the positions are all different. Imbalances up to about two pawns are
// kept on purpose (gambits, and small edges the engine should convert);
// only positions a short search already sees as clearly lost for one side
// (a hung piece, say) are skipped. Seeded, so the output is repeatable.

import { writeFileSync } from 'node:fs';
import { resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import { Position } from '../src/index.js';
import { Engine } from '../src/engine/index.js';

const options = {};
const args = process.argv.slice(2);
for (let i = 0; i < args.length; i += 2) options[args[i].replace(/^--/, '')] = args[i + 1];
const count = Number(options.count ?? 1000);
const plies = Number(options.plies ?? 6);
const maxScore = Number(options['max-score'] ?? 200); // centipawns: up to about two pawns
const out = resolve(options.out ?? `${fileURLToPath(new URL('..', import.meta.url))}/test/openings.epd`);

let seed = 20261015;
const random = () => ((seed = (seed * 1103515245 + 12345) >>> 0) / 2 ** 32);

const engine = new Engine({ ttBits: 16 });
const firstMoves = new Position().legalMoves().map((m) => new Position().toUci(m));
const perFirstMove = Math.ceil(count / firstMoves.length);
const seen = new Set();
const lines = [];

for (const first of firstMoves) {
  let found = 0;
  for (let tries = 0; found < perFirstMove && lines.length < count && tries < perFirstMove * 50; tries++) {
    const pos = new Position();
    const moves = [first];
    pos.play(first);
    for (let ply = 1; ply < plies && pos.hasLegalMove(); ply++) {
      const legal = pos.legalMoves();
      const move = legal[Math.floor(random() * legal.length)];
      moves.push(pos.toUci(move));
      pos.makeMove(move);
    }
    if (!pos.hasLegalMove() || pos.inCheck()) continue;
    // Unique by placement, side to move, castling and en passant.
    const key = pos.fen().split(' ').slice(0, 4).join(' ');
    if (seen.has(key)) continue;
    engine.newGame();
    const { score } = engine.search(pos, { nodes: 20000 });
    if (Math.abs(score) > maxScore) continue;
    seen.add(key);
    lines.push(`${pos.fen()} ; ${moves.join(' ')}`);
    found++;
  }
}

const header = `# ${lines.length} starting positions for engine matches, ${plies} random plies from the
# start (an equal share for each White first move), all different, with a
# short search seeing neither side more than ${maxScore} centipawns ahead.
# Made by scripts/make-openings.js. Use: npm run match -- --fens test/openings.epd
`;
writeFileSync(out, header + lines.join('\n') + '\n');
console.log(`${lines.length} positions written to ${out}`);
