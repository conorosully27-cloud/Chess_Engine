// Draws every piece set and writes it to app/pieces/<set>/. Run
// `npm run pieces` after changing a shape or a color in scripts/pieces/.
//
// Also writes the move list's figurines (app/pieces/fig-*.svg): solid Ink
// silhouettes used as CSS masks, so they take on the text color.

import { mkdirSync, rmSync, writeFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import * as ink from './pieces/ink.js';
import * as soft from './pieces/soft.js';
import * as tokens from './pieces/tokens.js';
import * as bauhaus from './pieces/bauhaus.js';
import * as pixel from './pieces/pixel.js';

const SETS = [ink, soft, tokens, bauhaus, pixel];
const LETTERS = ['K', 'Q', 'R', 'B', 'N', 'P'];
const OUT = fileURLToPath(new URL('../app/pieces/', import.meta.url));

const tidy = (svg) => `${svg.trim().replace(/\n\s*/g, '')}\n`;
const wrap = (content) => `<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 45 45">${content}</svg>`;

rmSync(OUT, { recursive: true, force: true });
for (const set of SETS) {
  mkdirSync(`${OUT}${set.id}`, { recursive: true });
  for (const letter of LETTERS) {
    writeFileSync(`${OUT}${set.id}/w${letter}.svg`, tidy(wrap(set.render(letter, true))));
    writeFileSync(`${OUT}${set.id}/b${letter}.svg`, tidy(wrap(set.render(letter, false))));
  }
}
for (const letter of LETTERS) writeFileSync(`${OUT}fig-${letter}.svg`, tidy(ink.figurine(letter)));

console.log(`Wrote ${SETS.length} piece sets and the figurines to ${OUT}`);
