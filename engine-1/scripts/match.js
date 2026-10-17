// Engine-vs-engine matches, to measure whether a new engine version is
// stronger than an old one.
//
//   npm run match -- --b ../engine-1.0                     # this checkout vs Engine 1.0
//   npm run match -- --b ../engine-1.0 --sprt 0,10         # stop once SPRT decides
//   npm run match -- --a ../new --b ../old --games 400 --time 100
//   npm run match -- --b ../engine-1.1 --fens test/endgames.epd   # endgames only
//
// --a / --b are checkouts of this repository (A defaults to this one). Set
// one up with: git worktree add ../engine-1.0 engine-1.0
//
// Each game starts from an opening-book line (or, with --fens, from a position
// in a file of FENs), and every start is played twice with colors swapped, so
// neither side profits from a lucky opening.
// Both engines get the same thinking time per move (or --time-a / --time-b
// for deliberate time odds). Games run in parallel on worker threads.
//
// With --sprt elo0,elo1 the match runs a sequential probability ratio test:
// it keeps playing until the results show, with error rates --alpha and
// --beta, that A is at least elo1 stronger than B (H1 accepted) or no more
// than elo0 stronger (H0 accepted), or until --games run out. This is how
// Stockfish tests changes; "--sprt 0,10" asks "is A at least 10 Elo better?".
//
// Engines must export, from src/engine/index.js: Engine (with
// search(pos, { timeMs, nodes })), positionFromHistory(uciMoves), BOOK_LINES.

import { Worker, isMainThread, parentPort, workerData } from 'node:worker_threads';
import { availableParallelism } from 'node:os';
import { resolve } from 'node:path';
import { readFileSync } from 'node:fs';
import { pathToFileURL, fileURLToPath } from 'node:url';
import { eloEstimate, parseSprt, sprtLlr } from './sprt.js';

const OPENING_PLIES = 6; // how much of each book line to play before the engines take over
const MAX_PLIES = 400; // games longer than this are scored as draws
const ADJUDICATE_SCORE = 1000; // centipawns: both engines agreeing on this...
const ADJUDICATE_PLIES = 8; // ...for this many plies in a row ends the game

if (isMainThread) {
  await main();
} else {
  await playGames();
}

// ---------------------------------------------------------------- main thread

async function main() {
  const options = parseArgs(process.argv.slice(2));
  if (options.help || !options.b) {
    console.log(`Usage: npm run match -- --b <checkout> [options]

  --a <dir>          engine A (default: this checkout)
  --b <dir>          engine B, e.g. ../engine-1.0
  --games <n>        maximum number of games (default 200; rounded up to pairs)
  --time <ms>        thinking time per move for both (default 100)
  --time-a <ms>      thinking time for A only (time odds)
  --time-b <ms>      thinking time for B only
  --nodes <n>        search a fixed number of positions per move instead of time
  --fens <file>      start games from these positions (one FEN or EPD per line)
                     instead of the opening book, e.g. to test endgame play
  --sprt <e0>,<e1>   run an SPRT: H0 "A is at most e0 Elo better" vs H1 "at least e1"
  --alpha <p>        SPRT false-positive rate (default 0.05)
  --beta <p>         SPRT false-negative rate (default 0.05)
  --threads <n>      games played at once (default: CPU cores - 1)`);
    process.exit(options.help ? 0 : 1);
  }

  const here = resolve(fileURLToPath(new URL('..', import.meta.url)));
  const a = resolve(options.a ?? here);
  const b = resolve(options.b);
  const timeA = Number(options['time-a'] ?? options.time ?? 100);
  const timeB = Number(options['time-b'] ?? options.time ?? 100);
  const nodes = options.nodes ? Number(options.nodes) : undefined;
  const pairs = Math.ceil(Number(options.games ?? 200) / 2);
  const threads = Math.max(1, Number(options.threads ?? Math.max(1, availableParallelism() - 1)));
  const sprt = options.sprt ? parseSprt(options.sprt, Number(options.alpha ?? 0.05), Number(options.beta ?? 0.05)) : null;

  let openings;
  if (options.fens) {
    // FEN or EPD lines; '#' starts a comment.
    openings = readFileSync(resolve(options.fens), 'utf8').split('\n')
      .map((line) => line.replace(/#.*/, '').trim())
      .filter(Boolean)
      .map((line) => {
        const fields = line.split(/\s+/);
        const counters = /^\d+$/.test(fields[4] ?? '') && /^\d+$/.test(fields[5] ?? '') ? fields.slice(4, 6) : ['0', '1'];
        return { fen: [...fields.slice(0, 4), ...counters].join(' ') };
      });
    if (openings.length === 0) throw new Error(`no positions in ${options.fens}`);
  } else {
    const { BOOK_LINES } = await import(pathToFileURL(`${a}/src/engine/index.js`).href);
    openings = BOOK_LINES.map((line) => line.slice(0, OPENING_PLIES));
  }

  console.log(`A: ${a}\nB: ${b}`);
  if (options.fens) console.log(`Starting positions: ${openings.length} from ${options.fens}`);
  console.log(`${nodes ? `${nodes} nodes` : `A ${timeA} ms, B ${timeB} ms`} per move, up to ${pairs * 2} games, ${threads} at a time`);
  if (sprt) console.log(`SPRT: H0 elo <= ${sprt.elo0}, H1 elo >= ${sprt.elo1}, alpha ${sprt.alpha}, beta ${sprt.beta} (LLR bounds ${sprt.lower.toFixed(2)}, ${sprt.upper.toFixed(2)})`);

  // Game i plays opening floor(i / 2) with A as White when i is even.
  const queue = [];
  for (let i = 0; i < pairs * 2; i++) queue.push({ index: i, opening: openings[Math.floor(i / 2) % openings.length], aIsWhite: i % 2 === 0 });

  const tally = { win: 0, draw: 0, loss: 0 };
  let verdict = null;
  let finished = 0;
  const started = Date.now();

  await new Promise((resolveAll) => {
    const workers = [];
    const stopAll = () => {
      for (const w of workers) w.terminate();
      resolveAll();
    };
    const feed = (worker) => {
      const job = queue.shift();
      if (job && !verdict) worker.postMessage(job);
      else if (workers.every((w) => w.idle || w === worker)) stopAll();
      else worker.idle = true;
    };
    for (let t = 0; t < Math.min(threads, queue.length); t++) {
      const worker = new Worker(fileURLToPath(import.meta.url), { workerData: { a, b, timeA, timeB, nodes } });
      worker.idle = false;
      worker.on('message', ({ score }) => {
        finished++;
        tally[score === 1 ? 'win' : score === 0 ? 'loss' : 'draw']++;
        if (sprt) {
          const llr = sprtLlr(tally, sprt);
          if (llr >= sprt.upper) verdict = 'H1 accepted: A is stronger';
          else if (llr <= sprt.lower) verdict = 'H0 accepted: A is not stronger by the margin tested';
        }
        report(tally, finished, sprt, started, false);
        if (verdict) {
          stopAll();
          return;
        }
        feed(worker);
      });
      worker.on('error', (error) => {
        console.error(error);
        process.exitCode = 1;
        stopAll();
      });
      workers.push(worker);
      feed(worker);
    }
  });

  console.log('');
  report(tally, finished, sprt, started, true);
  if (sprt) console.log(verdict ?? 'SPRT inconclusive: the game limit ran out before a decision');
}

function report(tally, games, sprt, started, final) {
  const { elo, low, high } = eloEstimate(tally);
  const fmt = (x) => (Number.isFinite(x) ? `${x > 0 ? '+' : ''}${x.toFixed(0)}` : x > 0 ? '+inf' : '-inf');
  let line = `Games ${games}: +${tally.win} -${tally.loss} =${tally.draw}  score ${((tally.win + tally.draw / 2) / games * 100).toFixed(1)}%  Elo ${fmt(elo)} (95% ${fmt(low)} to ${fmt(high)})`;
  if (sprt) line += `  LLR ${sprtLlr(tally, sprt).toFixed(2)} [${sprt.lower.toFixed(2)}, ${sprt.upper.toFixed(2)}]`;
  if (final) line += `  in ${Math.round((Date.now() - started) / 1000)}s`;
  if (final || !process.stdout.isTTY) console.log(line);
  else process.stdout.write(`\r${line}`);
}

function parseArgs(args) {
  const options = {};
  for (let i = 0; i < args.length; i++) {
    const arg = args[i];
    if (arg === '--help' || arg === '-h') options.help = true;
    else if (arg.startsWith('--')) options[arg.slice(2)] = args[++i];
  }
  return options;
}

// ---------------------------------------------------------------- worker threads

async function playGames() {
  const { a, b, timeA, timeB, nodes } = workerData;
  const load = async (dir) => ({
    engine: await import(pathToFileURL(`${dir}/src/engine/index.js`).href),
    rules: await import(pathToFileURL(`${dir}/src/index.js`).href),
  });
  const [A, B] = [await load(a), await load(b)];
  const players = [
    { api: A.engine, rules: A.rules, engine: new A.engine.Engine({ ttBits: 18 }), limits: nodes ? { nodes } : { timeMs: timeA } },
    { api: B.engine, rules: B.rules, engine: new B.engine.Engine({ ttBits: 18 }), limits: nodes ? { nodes } : { timeMs: timeB } },
  ];
  // The position after `history`, built with that player's own rules code.
  const positionFor = (player, start, history) => {
    if (!start) return player.api.positionFromHistory(history);
    const pos = new player.rules.Position(start);
    for (const uci of history) pos.play(uci);
    return pos;
  };

  parentPort.on('message', ({ opening, aIsWhite }) => {
    for (const p of players) p.engine.newGame?.();
    // The referee uses A's rules code; both engines see the same moves.
    // `opening` is a list of book moves or { fen } for a set position.
    const start = Array.isArray(opening) ? null : opening.fen;
    const referee = start ? new A.rules.Position(start) : new A.rules.Position();
    const history = start ? [] : [...opening];
    for (const uci of history) referee.play(uci);

    let result = null;
    let streak = 0; // consecutive plies both engines agreed one side is winning
    let lastSign = 0;
    for (let ply = history.length; ply < MAX_PLIES && !result; ply++) {
      const status = referee.status();
      if (status.isOver) {
        result = status.result;
        break;
      }
      const whiteToMove = referee.turn === 0;
      const player = players[whiteToMove === aIsWhite ? 0 : 1];
      const pos = positionFor(player, start, history);
      const { bestMove, score } = player.engine.search(pos, player.limits);
      referee.play(bestMove);
      history.push(bestMove);

      // Adjudicate a lopsided game: both sides' last searches agree on a big edge.
      const whiteScore = whiteToMove ? score : -score;
      const sign = Math.abs(whiteScore) >= ADJUDICATE_SCORE ? Math.sign(whiteScore) : 0;
      streak = sign !== 0 && sign === lastSign ? streak + 1 : sign !== 0 ? 1 : 0;
      lastSign = sign;
      if (streak >= ADJUDICATE_PLIES) result = sign > 0 ? '1-0' : '0-1';
    }
    result ??= '1/2-1/2';
    const score = result === '1/2-1/2' ? 0.5 : (result === '1-0') === aIsWhite ? 1 : 0;
    parentPort.postMessage({ score });
  });
}
