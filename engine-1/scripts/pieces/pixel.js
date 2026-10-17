// Pixel: 8-bit sprites on a 15 x 15 grid, three units per pixel.
//   K outline   F fill   H highlight   S shade   (space) transparent

export const id = 'pixel';

const PALETTES = {
  white: { K: '#1b1b20', F: '#f1ece0', H: '#ffffff', S: '#c4bca9' },
  black: { K: '#0a0a0c', F: '#3d3d47', H: '#6e6e7c', S: '#25252b' },
};

const BASE = [
  '  KKKKKKKKKKK  ',
  '  KHFFFFFFFSK  ',
  '  KKKKKKKKKKK  ',
];

const SPRITES = {
  P: [
    '               ',
    '               ',
    '      KKK      ',
    '     KHFFK     ',
    '    KHFFFSK    ',
    '    KFFFFSK    ',
    '     KFFSK     ',
    '    KKFFSKK    ',
    '     KFFSK     ',
    '    KHFFFSK    ',
    '   KHFFFFFSK   ',
    ...BASE,
    '               ',
  ],
  R: [
    '               ',
    '  KKK KKK KKK  ',
    '  KHK KFK KSK  ',
    '  KHKKKFKKKSK  ',
    '  KHFFFFFFFSK  ',
    '  KKKKKKKKKKK  ',
    '   KHFFFFFSK   ',
    '   KHFFFFFSK   ',
    '   KHFFFFFSK   ',
    '   KHFFFFFSK   ',
    '   KHFFFFFSK   ',
    ...BASE,
    '               ',
  ],
  N: [
    '               ',
    '      KK       ',
    '     KHFK      ',
    '    KHFFFKK    ',
    '   KHFFFFFFK   ',
    '  KHFKFFFFFSK  ',
    ' KHFFFFFFFFSK  ',
    ' KFFFKKFFFFSK  ',
    '  KKK KFFFFSK  ',
    '     KHFFFFSK  ',
    '    KHFFFFFSK  ',
    ...BASE,
    '               ',
  ],
  B: [
    '               ',
    '       K       ',
    '      KHK      ',
    '     KHFFK     ',
    '    KHFFKSK    ',
    '    KHFKFSK    ',
    '    KFKFFSK    ',
    '     KFFSK     ',
    '    KKKKKKK    ',
    '     KHFSK     ',
    '    KHFFFSK    ',
    ...BASE,
    '               ',
  ],
  Q: [
    '               ',
    '  K    K    K  ',
    '  KK  KHK  KK  ',
    '  KHK KHK KHK  ',
    '  KHFKFFFKFSK  ',
    '  KHFFFFFFFSK  ',
    '   KHFFFFFSK   ',
    '    KKKKKKK    ',
    '     KHFSK     ',
    '     KHFSK     ',
    '    KHFFFSK    ',
    ...BASE,
    '               ',
  ],
  K: [
    '      KKK      ',
    '     KKHKK     ',
    '     KHHHK     ',
    '     KKHKK     ',
    '    KKKKKKK    ',
    '   KHFFFFFSK   ',
    '   KHFFFFFSK   ',
    '    KHFFFSK    ',
    '    KKKKKKK    ',
    '     KHFSK     ',
    '    KHFFFSK    ',
    ...BASE,
    '               ',
  ],
};

export function render(letter, white) {
  const palette = PALETTES[white ? 'white' : 'black'];
  let rects = '';
  SPRITES[letter].forEach((row, y) => {
    if (row.length !== 15) throw new Error(`pixel ${letter}: row ${y} is ${row.length} wide`);
    // Merge runs of the same color into one rectangle.
    for (let x = 0; x < 15;) {
      const ch = row[x];
      let end = x + 1;
      while (end < 15 && row[end] === ch) end++;
      if (ch !== ' ') rects += `<rect x="${x * 3}" y="${y * 3}" width="${(end - x) * 3}" height="3" fill="${palette[ch]}"/>`;
      x = end;
    }
  });
  return `<g shape-rendering="crispEdges">${rects}</g>`;
}
