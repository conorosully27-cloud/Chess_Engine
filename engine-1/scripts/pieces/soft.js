// Soft: rounded, turned-looking pieces. Each part is shaded left to right like
// a lathe-turned cylinder, which gives the set some volume without detail.

export const id = 'soft';

const PALETTES = {
  white: { stops: ['#ffffff', '#f1ece2', '#cfc6b3'], stroke: '#5b5447', detail: '#5b5447' },
  black: { stops: ['#62636d', '#37383f', '#1b1c20'], stroke: '#0b0b0d', detail: '#d9d5cb' },
};

const BASE = '<rect x="11.5" y="34.5" width="22" height="5" rx="2.5"/>';
const BELL = '<path d="M16 35c.5-5.5 3-8.5 3.5-11h6c.5 2.5 3 5.5 3.5 11z"/>';

const SHAPES = {
  P: {
    body: `
      <path d="M16.5 35c.5-6 3.5-9.5 3.8-14h4.4c.3 4.5 3.3 8 3.8 14z"/>
      <ellipse cx="22.5" cy="21" rx="5.8" ry="1.9"/>
      <circle cx="22.5" cy="14" r="5.4"/>
      ${BASE}`,
    details: '',
  },
  R: {
    body: `
      <path d="M16 35l1.2-17h10.6L29 35z"/>
      <rect x="14" y="12.5" width="17" height="6.5" rx="1.6"/>
      <rect x="14" y="8" width="4.6" height="6" rx="1.3"/>
      <rect x="20.2" y="8" width="4.6" height="6" rx="1.3"/>
      <rect x="26.4" y="8" width="4.6" height="6" rx="1.3"/>
      ${BASE}`,
    details: '',
  },
  N: {
    body: `
      <path d="M17 35c.4-4.6 1.4-7.2 2.6-9.6-2.6 1-5.2 1.9-6.9.9-2-1.2-2.2-3.5-1.2-5.1l2.3-3.6c1.7-3 4.2-5.2 6.8-6.6l1.4-4.2c.3-.8 1.2-1 1.7-.4l2.3 3c4.5 1.9 6.5 7.4 6 13.4-.4 4.5-1.5 8-2 12.2z"/>
      ${BASE}`,
    details: '<circle cx="20.2" cy="15.8" r="1.1"/><path d="M27.8 12.8c2.2 3.5 2.6 9.6 1.6 16" fill="none" stroke-width="1.1" stroke-linecap="round"/>',
  },
  B: {
    body: `
      ${BELL}
      <ellipse cx="22.5" cy="24" rx="5.6" ry="1.7"/>
      <path d="M22.5 8.5c5.5 3.5 7 10 4 15.5h-8c-3-5.5-1.5-12 4-15.5z"/>
      <circle cx="22.5" cy="7" r="2.1"/>
      ${BASE}`,
    details: '<path d="M24.8 13.6l-3.8 4.2" fill="none" stroke-width="1.3" stroke-linecap="round"/>',
  },
  Q: {
    body: `
      ${BELL}
      <ellipse cx="22.5" cy="24" rx="6.6" ry="1.9"/>
      <path d="M14.5 13.5c2.5 2.5 3.5 6.5 3.5 10.5h9c0-4 1-8 3.5-10.5-3 1.5-5.5 1.5-8-1-2.5 2.5-5 2.5-8 1z"/>
      <circle cx="14.5" cy="12.6" r="2"/>
      <circle cx="18.6" cy="11.6" r="1.6"/>
      <circle cx="22.5" cy="10.4" r="2.1"/>
      <circle cx="26.4" cy="11.6" r="1.6"/>
      <circle cx="30.5" cy="12.6" r="2"/>
      ${BASE}`,
    details: '',
  },
  K: {
    body: `
      ${BELL}
      <ellipse cx="22.5" cy="24" rx="6.6" ry="1.9"/>
      <path d="M15 15.5c0-3 4-4 7.5-4s7.5 1 7.5 4c0 3.5-2.5 6-3 8.5h-9c-.5-2.5-3-5-3-8.5z"/>
      <rect x="21.3" y="3.5" width="2.4" height="8.6" rx="1"/>
      <rect x="18.8" y="5.6" width="7.4" height="2.4" rx="1"/>
      ${BASE}`,
    details: '',
  },
};

export function render(letter, white) {
  const p = PALETTES[white ? 'white' : 'black'];
  const { body, details } = SHAPES[letter];
  return `
    <defs><linearGradient id="turn" x1="0" x2="1">
      <stop offset="0" stop-color="${p.stops[0]}"/><stop offset=".55" stop-color="${p.stops[1]}"/><stop offset="1" stop-color="${p.stops[2]}"/>
    </linearGradient></defs>
    <g fill="url(#turn)" stroke="${p.stroke}" stroke-width="1.3" stroke-linejoin="round">${body}</g>
    <g fill="${p.detail}" stroke="${p.detail}">${details}</g>`;
}
