// Converts a PGN opening book (e.g. Stockfish's 8moves_v3.pgn) into one line
// of UCI moves per opening, for `engine2 datagen ... <book>`. Uses Engine 1's
// move parser (../../../engine-1/src). Openings with an illegal or unreadable
// move are skipped and counted.
//   node scripts/nnue/book_to_uci.mjs 8moves_v3.pgn > 8moves_v3.uci
import { readFileSync } from 'node:fs';
import { Position, parseSan, moveToUci } from '../../../engine-1/src/index.js';

const text = readFileSync(process.argv[2], 'utf8');
let kept = 0, skipped = 0;
const out = [];
// Movetext is everything outside the [Tag "..."] lines, one game per blank-line-separated block after its tags.
for (const game of text.split(/\n\s*\n(?=\[)/)) {
  const movetext = game.split('\n').filter((l) => !l.startsWith('[')).join(' ')
    .replace(/\{[^}]*\}/g, ' ').replace(/\d+\.(\.\.)?/g, ' ').replace(/(1-0|0-1|1\/2-1\/2|\*)\s*$/, ' ');
  const sans = movetext.split(/\s+/).filter(Boolean);
  if (!sans.length) continue;
  const pos = new Position();
  const ucis = [];
  for (const san of sans) {
    const move = parseSan(pos, san);
    if (move === null || move === undefined) { ucis.length = 0; break; }
    ucis.push(moveToUci(move));
    pos.makeMove(move);
  }
  if (ucis.length) { out.push(ucis.join(' ')); kept++; } else skipped++;
}
process.stdout.write(out.join('\n') + '\n');
process.stderr.write(`${kept} openings, ${skipped} skipped\n`);
