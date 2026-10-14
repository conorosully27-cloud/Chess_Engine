// Tokens: board-game counters. A thick disc with a rim, stamped with the Ink
// silhouette, so the pieces read clearly even at small sizes.

import { SHAPES } from './ink.js';

export const id = 'tokens';

const PALETTES = {
  white: { face: '#f6f1e6', rim: '#d6cdb9', edge: '#b3a78e', outline: '#4d4739', emblem: '#2b2c31' },
  black: { face: '#2e2f35', rim: '#4a4b54', edge: '#141519', outline: '#08090b', emblem: '#efe9db' },
};

export function render(letter, white) {
  const p = PALETTES[white ? 'white' : 'black'];
  const { body, details } = SHAPES[letter];
  // Ink pieces span roughly x 10-35, y 2-40.5; shrink the emblem into the disc.
  return `
    <mask id="cut"><rect x="0" y="0" width="45" height="45" fill="#fff"/><g fill="none" stroke="#000" stroke-width="1.4" stroke-linecap="round">${details}</g></mask>
    <circle cx="22.5" cy="24.6" r="18" fill="${p.edge}" stroke="${p.outline}" stroke-width="1.2"/>
    <circle cx="22.5" cy="22.4" r="18" fill="${p.face}" stroke="${p.outline}" stroke-width="1.2"/>
    <circle cx="22.5" cy="22.4" r="14.6" fill="none" stroke="${p.rim}" stroke-width="1.6"/>
    <g transform="translate(22.5 22.6) scale(.56) translate(-22.5 -21.25)">
      <g mask="url(#cut)" fill="${p.emblem}" stroke="${p.emblem}" stroke-width="1.5" stroke-linejoin="round">${body}</g>
    </g>`;
}
