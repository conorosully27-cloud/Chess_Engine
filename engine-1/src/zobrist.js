// Zobrist hashing keys. Every (piece, square), castling-rights combination,
// en passant file and side to move gets a fixed random 64-bit key, and a
// position's hash is the XOR of the keys of its features, so a move updates
// the hash with a handful of XORs. JS bitwise operators work on 32 bits, so
// each 64-bit key is stored as a lo/hi pair of 32-bit halves.
//
// The hash identifies positions for the repetition rule now, and will index
// the engine's transposition table later.

// mulberry32: small, fast PRNG. A fixed seed keeps hashes identical across runs.
function mulberry32(seed) {
  return () => {
    seed = (seed + 0x6d2b79f5) | 0;
    let t = Math.imul(seed ^ (seed >>> 15), seed | 1);
    t ^= t + Math.imul(t ^ (t >>> 7), t | 61);
    return (t ^ (t >>> 14)) | 0;
  };
}

const random = mulberry32(0x2545f491);

function randomKeys(count) {
  const keys = new Int32Array(count);
  for (let i = 0; i < count; i++) keys[i] = random();
  return keys;
}

// Indexed by piece * 128 + square.
export const Z_PIECE_LO = randomKeys(16 * 128);
export const Z_PIECE_HI = randomKeys(16 * 128);
// Indexed by the 4-bit castling rights value.
export const Z_CASTLING_LO = randomKeys(16);
export const Z_CASTLING_HI = randomKeys(16);
// Indexed by the en passant square's file.
export const Z_EP_LO = randomKeys(8);
export const Z_EP_HI = randomKeys(8);
// XOR'd in when Black is to move.
export const Z_SIDE_LO = random();
export const Z_SIDE_HI = random();
