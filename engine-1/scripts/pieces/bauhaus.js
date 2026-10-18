// Bauhaus: geometric blocks after Josef Hartwig's 1924 Bauhaus chess set,
// where each piece's shape describes its move. The rook is a cube (straight
// lines), the bishop an X (diagonals), the knight an L, the queen a cube
// carrying a sphere, the king a cube carrying a cube turned on its corner, and
// the pawn a small cube. Blocks are drawn in oblique projection, lit from the
// top left.

export const id = 'bauhaus';

const PALETTES = {
  white: { front: '#f0e4c8', top: '#faf4e4', side: '#d2bf95', outline: '#3a3226', shine: '#ffffff' },
  black: { front: '#35343c', top: '#4d4c56', side: '#1e1d23', outline: '#0b0b0e', shine: '#6a6975' },
};

const DEPTH = 3.5; // how far the oblique top and side recede, up and to the right

// A box: front face, then its top and right-hand side receding.
function block(p, x, y, w, h, { side = true } = {}) {
  const d = DEPTH;
  const front = `<rect x="${x}" y="${y}" width="${w}" height="${h}" fill="${p.front}"/>`;
  const top = `<path d="M${x} ${y}l${d} ${-d}h${w}l${-d} ${d}z" fill="${p.top}"/>`;
  const right = side ? `<path d="M${x + w} ${y}l${d} ${-d}v${h}l${-d} ${d}z" fill="${p.side}"/>` : '';
  return top + right + front;
}

const SHAPES = {
  P: (p) => block(p, 15.5, 25, 13, 14),
  R: (p) => block(p, 12.5, 18, 18, 21),
  // An L: a tall column with a head block jutting out to the left at the top.
  N: (p) => block(p, 10, 13.5, 9.5, 8, { side: false }) + block(p, 19.5, 13.5, 9, 25.5),
  B: (p) => `
    <path d="M28.6 12 35 17.5 16.4 39 10 33.5z" fill="${p.side}"/>
    <path d="M16.4 12 35 33.5 28.6 39 10 17.5z" fill="${p.front}"/>`,
  Q: (p) => `
    ${block(p, 13, 25, 18.5, 14)}
    <defs><radialGradient id="ball" cx=".35" cy=".3" r=".8">
      <stop offset="0" stop-color="${p.shine}"/><stop offset=".45" stop-color="${p.front}"/><stop offset="1" stop-color="${p.side}"/>
    </radialGradient></defs>
    <circle cx="23.7" cy="15.2" r="7.4" fill="url(#ball)"/>`,
  K: (p) => `
    ${block(p, 13, 25, 18.5, 14)}
    <path d="M23.7 5.5v18.5L14.5 14.75z" fill="${p.front}"/>
    <path d="M23.7 5.5l9.2 9.25-9.2 9.25z" fill="${p.side}"/>`,
};

export function render(letter, white) {
  const p = PALETTES[white ? 'white' : 'black'];
  // One outline around every face keeps the blocks crisp on any square color.
  return `<g stroke="${p.outline}" stroke-width="1.1" stroke-linejoin="round">${SHAPES[letter](p)}</g>`;
}
