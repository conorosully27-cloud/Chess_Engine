// Ink: drawn like the pieces in a printed chess book. White in outline,
// Black in solid ink, all six standing on the same plinth. Its shapes are
// also used for the Tokens emblems and the move list's figurines.

export const id = 'ink';

const INK = '#15171c';
const PAPER = '#fafaf5';

const PLINTH = '<path d="M11 35.5h23v4H11z"/>';
const SMALL_PLINTH = '<path d="M13 35.5h19v4H13z"/>';

// `body` is the silhouette (filled and outlined); `details` are lines drawn on
// top in the contrasting color (ink on White, paper on Black).
export const SHAPES = {
  P: {
    body: `
      <path d="M18.5 23.5h8l3.5 12H15z"/>
      <path d="M17.5 21h10v2.5h-10z"/>
      <circle cx="22.5" cy="15" r="5.5"/>
      ${SMALL_PLINTH}`,
    details: '<path d="M18.6 23.5h7.8M14.6 35.5h15.8"/>',
  },
  R: {
    body: `
      <path d="M15.5 17h14l1 15.5h-16z"/>
      <path d="M13 8.5h4.5v3h3v-3h4v3h3v-3H32V17H13z"/>
      <path d="M13 32.5h19v3H13z"/>
      ${PLINTH}`,
    details: '<path d="M14.4 17h16.2M14.4 32.5h16.2M12.4 35.5h20.2"/>',
  },
  N: {
    body: `
      <path d="M17 32.5l1.5-8-5 2c-2 .5-3.5-1-3-3l1.5-4 9-9 2.5-5 2.5 4c4.5 2 6.5 7.5 5.5 23z"/>
      <path d="M14 32.5h18v3H14z"/>
      ${PLINTH}`,
    details: `
      <circle cx="20.3" cy="15.6" r=".7" stroke-width="1.6"/>
      <path d="M27.6 13.4c1.9 3.6 2.4 10 2 17M15.4 32.5h15.2M12.4 35.5h20.2"/>`,
  },
  B: {
    body: `
      <path d="M18.5 27.5h8l3.5 8H15z"/>
      <path d="M17 25h11v2.5H17z"/>
      <path d="M22.5 9.5c5 4 7 9.5 4.5 15.5h-9c-2.5-6-.5-11.5 4.5-15.5z"/>
      <circle cx="22.5" cy="7" r="2.2"/>
      ${PLINTH}`,
    details: '<path d="M24.6 14.6 20.3 19M18.4 25h8.2M18.4 27.5h8.2M12.4 35.5h20.2"/>',
  },
  Q: {
    body: `
      <path d="M17.5 28.5h10l3 7h-16z"/>
      <path d="M15.5 26h14v2.5h-14z"/>
      <path d="M16 26l-3.5-13 4 7 .5-10 3.5 9 2-11 2 11 3.5-9 .5 10 4-7-3.5 13z"/>
      <circle cx="12.5" cy="12.2" r="1.9"/>
      <circle cx="17" cy="9.2" r="1.9"/>
      <circle cx="22.5" cy="7.2" r="1.9"/>
      <circle cx="28" cy="9.2" r="1.9"/>
      <circle cx="32.5" cy="12.2" r="1.9"/>
      ${PLINTH}`,
    details: '<path d="M16.9 26h11.2M16.9 28.5h11.2M12.4 35.5h20.2"/>',
  },
  K: {
    body: `
      <path d="M17.5 28.5h10l3 7h-16z"/>
      <path d="M15.5 26h14v2.5h-14z"/>
      <path d="M13.5 16.5c0-3 3-4.5 9-4.5s9 1.5 9 4.5l-3 9.5h-12z"/>
      <path d="M21.5 3h2v3h3v2h-3v4h-2V8h-3V6h3z"/>
      ${PLINTH}`,
    details: '<path d="M15.6 19.5h13.8M16.9 26h11.2M16.9 28.5h11.2M12.4 35.5h20.2"/>',
  },
};

export function render(letter, white) {
  const { body, details } = SHAPES[letter];
  return `
    <g fill="${white ? PAPER : INK}" stroke="${INK}" stroke-width="1.5" stroke-linejoin="round">${body}</g>
    <g fill="none" stroke="${white ? INK : PAPER}" stroke-width="1.2" stroke-linecap="round">${details}</g>`;
}

// Solid silhouette with the details cut out, used as a CSS mask for the move
// list. The view box is cropped to the outline so the symbol sits tight
// against the square name that follows it.
export function figurine(letter) {
  const { body, details } = SHAPES[letter];
  return `<svg xmlns="http://www.w3.org/2000/svg" viewBox="10 2 25 38.5">
    <mask id="cut"><rect x="0" y="0" width="45" height="45" fill="#fff"/><g fill="none" stroke="#000" stroke-width="1.2" stroke-linecap="round">${details}</g></mask>
    <g mask="url(#cut)" fill="#000" stroke="#000" stroke-width="1.5" stroke-linejoin="round">${body}</g>
  </svg>`;
}
