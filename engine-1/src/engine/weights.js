// Every tunable weight of the static evaluation, in centipawns.
//
// Each pair or { mg, eg } holds a middlegame and an endgame value, blended by
// evaluate() according to how much material is left. Piece-square tables
// are written from White's side with rank 8 at the top, as the board is
// printed; Black uses them mirrored.
//
// scripts/tune.js rewrites this file (Texel tuning), so keep it plain data.

// prettier-ignore
export const WEIGHTS = {
  material: {
    mg: [0, 115, 472, 503, 685, 1438, 0],
    eg: [0, 85, 298, 307, 538, 990, 0],
  },
  pst: {
    pawn: {
      mg: [
           0,    0,    0,    0,    0,    0,    0,    0,
          14,    8,   -9,   53,  124,  105,   32,  -32,
          -3,   -2,   40,   16,   56,   93,   24,   -7,
           4,   10,   11,   26,   39,   16,    3,  -18,
         -19,  -14,    9,   29,   24,   19,   -3,  -23,
         -14,  -17,   12,    2,   21,   13,   28,    0,
         -21,   -7,   -8,  -12,   -1,   31,   37,  -13,
           0,    0,    0,    0,    0,    0,    0,    0,
      ],
      eg: [
           0,    0,    0,    0,    0,    0,    0,    0,
          68,   88,  106,   75,   52,   66,  107,   78,
          32,   31,   27,   20,   10,    4,   26,   28,
          27,   21,   12,   -2,   -3,    5,   17,   16,
          22,   20,    2,   -5,   -2,    0,    7,    7,
          12,   11,    4,    9,    9,    8,   -2,   -2,
          18,   12,   19,   18,   15,   10,   -5,   -3,
           0,    0,    0,    0,    0,    0,    0,    0,
      ],
    },
    knight: {
      mg: [
        -270,  -46,  -42,  -72,   23, -157, -157, -154,
         -55,    4,  122,   41,  125,   97,   31,   -6,
          29,   59,   62,  100,  123,  160,   82,    9,
          51,   56,   51,   79,   39,   47,   24,   52,
          35,   40,   64,   53,   57,   58,   25,    9,
          17,   41,   58,   66,   73,   58,   68,   17,
          21,    7,   43,   58,   58,   59,   44,   34,
          -5,   18,    6,   21,   23,   38,   13,  -43,
      ],
      eg: [
         -15,  -52,  -18,   -5,  -19,  -10,  -11,  -90,
         -20,   -9,  -25,    2,  -27,  -24,  -24,  -39,
         -30,  -10,   22,   12,   -8,   -9,  -23,  -18,
          -7,   15,   23,   39,   40,   30,   18,  -21,
         -16,    6,   19,   25,   31,    8,   -1,  -11,
         -26,   -3,   -8,   12,   15,   -8,  -23,  -30,
         -34,  -18,  -13,  -12,  -15,  -12,  -19,  -31,
         -68,  -46,  -10,   -9,  -17,  -14,  -43,  -12,
      ],
    },
    bishop: {
      mg: [
         -27,  -41,  -40, -104, -127,  -32,  -12,  -66,
         -17,   25,   -2,    8,   20,   67,  -13,   29,
          12,   19,   81,   24,   87,   67,   67,   44,
          -2,   18,   24,   41,   37,   -7,   22,    7,
          24,   23,   21,   49,   49,   18,   29,    6,
          46,   49,   49,   25,   36,   53,   38,   39,
          10,   61,   38,   33,   41,   61,   78,   36,
          43,   17,   29,   26,   18,   11,   12,   36,
      ],
      eg: [
           7,   -1,    9,   21,   25,    4,    8,   21,
           2,    1,    7,   -5,    2,   -7,   13,  -23,
          11,    7,   -8,   10,   -7,    6,    0,    2,
          18,   14,    8,    8,    5,    8,   -1,    6,
           0,   -1,   12,   -3,   -4,    9,   -6,   -7,
         -17,    3,   -2,    9,    7,   -4,   -9,  -11,
           4,  -21,  -12,   -5,   -1,   -9,  -13,  -16,
         -22,    3,  -17,    2,    5,    2,   -7,  -11,
      ],
    },
    rook: {
      mg: [
          47,   86,   69,   84,  103,  171,  104,   20,
          93,   66,  105,  136,   97,  149,  171,  134,
          22,   11,   -4,   25,   41,   61,   94,   32,
         -10,    3,   24,   41,    7,   26,   33,   37,
          -3,  -17,  -21,   -1,    6,    4,   12,    2,
          -5,   -2,    6,   27,   18,   18,   26,   13,
          -4,    0,    0,   15,   21,   23,   37,  -39,
          18,   21,   32,   37,   43,   43,   -1,   27,
      ],
      eg: [
          24,    7,   12,    6,    1,  -14,   12,   24,
          -6,   13,    0,  -12,   -4,   -5,  -14,  -12,
          28,   28,   31,   23,   18,   12,    7,   17,
          31,   25,   26,   17,   21,   20,   16,   15,
          23,   28,   32,   28,   25,   20,   13,   10,
          15,   18,   15,   13,   16,   10,    2,    2,
          14,   17,   26,   17,   13,   12,    8,   21,
          14,   15,   13,   12,    3,    5,   15,  -15,
      ],
    },
    queen: {
      mg: [
         -15,  -11,   21,   78,  111,   64,   11,   -8,
          -4,   -8,  -10,  -54,  -35,   79,   10,  141,
           7,    9,   37,   43,   54,   54,  117,   60,
           7,   26,   21,    5,   10,    5,    4,   31,
          23,   23,   26,   14,   37,   32,   42,   28,
          32,   48,   39,   42,   43,   43,   50,   36,
          39,   52,   57,   62,   68,   79,   87,   44,
          56,   45,   57,   66,   51,   44,   20,   54,
      ],
      eg: [
          56,   62,   49,   30,    8,   62,   39,   93,
          69,   52,   62,  131,   91,   99,   81,  -45,
          27,   40,   42,   28,   77,   28,   -3,    9,
          51,   60,   41,   70,   64,   70,   57,   28,
          41,   52,   42,   65,   42,   27,   37,   26,
          -7,   -8,   33,   20,   31,   31,   46,   22,
          22,   14,   -1,    8,    6,  -33,  -51,   12,
          -9,   -3,   -5,  -25,   26,  -15,    0,  -22,
      ],
    },
    king: {
      mg: [
           7,  -21,   14, -100,   91,   -2,  -85, -104,
         -66,   26,   10,   19,  -53,   32, -138, -148,
         -57,  117,   42,   73,  -45,   49,   42, -110,
        -101,  -16,   -4,   -9,  -18,  -35,   -2, -119,
         -63,   56,   22,  -28,    3,  -50,  -20, -149,
         -23,   42,  -12,  -25,  -10,   -2,   20,  -61,
          42,   26,   36,  -15,   -1,   18,   57,   26,
         -52,   48,   35,  -32,   25,  -14,   47,   26,
      ],
      eg: [
         -53,  -16,  -17,   13,  -28,   -4,    8,  -19,
         -21,    0,   -2,   -3,   11,   17,   50,   21,
         -14,  -13,   -8,   -9,   15,   19,   23,   23,
         -20,   -2,    5,    6,    8,   21,   13,   13,
         -28,  -23,   -3,    8,    6,   15,    3,   13,
         -30,  -19,    2,    8,    7,    6,   -6,   -3,
         -39,  -16,   -6,    9,    9,    5,  -10,  -26,
         -34,  -37,  -15,   -8,  -22,   -9,  -39,  -67,
      ],
    },
  },
  bishopPair: [36, 54],
  doubledPawn: [-5, -11],
  isolatedPawn: [-22, -10],
  // By how far the pawn has advanced (relative rank 0-7; 1-6 are used).
  passedPawn: {
    mg: [0, 0, -7, -11, 10, 17, 53, 0],
    eg: [0, 24, 17, 38, 64, 100, 80, 0],
  },
  rookOpenFile: [48, -9],
  rookSemiOpenFile: [20, -1],
  // Per square a piece can move to, beyond a typical count (see evaluate.js).
  mobility: {
    knight: [2, -1],
    bishop: [5, 4],
    rook: [3, 3],
    queen: [2, 3],
  },
  // Middlegame only: a pawn right in front of the king, one square further,
  // and no pawn on that file near the king.
  kingShelter: [1, -1, -12],
  // Endgame, per rank of a passed pawn (0-7): per step of distance from our
  // king and from theirs to the square in front of it, and when that square
  // is occupied.
  passedOwnKing: [0, -3, -4, -11, -19, -23, -20, 0],
  passedEnemyKing: [0, -1, 3, 11, 24, 43, 54, 0],
  passedBlocked: [0, -9, -5, -13, -16, -47, -81, 0],
  rookBehindPasser: [19, 20],
  rookBehindEnemyPasser: [3, 50],
  rookOnSeventh: [-89, 48],
  // A pawn ending passer the enemy king can no longer catch.
  unstoppablePasser: 235,
  // King danger, in attack units (see DANGER in evaluate.js): attackers on
  // the king zone (knight, bishop, rook, queen), zone squares hit, weak zone
  // squares, safe checks (knight, bishop, rook, queen), no enemy queen, and
  // a constant. Costs units² / 720 in the middlegame, units / 20 endgame.
  kingDanger: [45, 21, 25, 12, 23, 33, 124, 90, 110, 38, -275, -8],
  // Middlegame, per rank of the enemy pawn nearest our king on its file or
  // the next (counted from the enemy's side): free, or blocked by our pawn.
  pawnStorm: [0, 3, 8, 0, -12, -15, 22, 0],
  pawnStormBlocked: [0, 8, 7, 8, 1, -13, 0, 0],
  // Middlegame, per file on or next to our king without our pawns:
  // half-open (they have one there) and fully open.
  kingOpenFile: [-12, -33],
  tempo: 32, // having the move is worth a little
};
