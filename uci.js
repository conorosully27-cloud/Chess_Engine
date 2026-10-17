// UCI front end, so the engine can run in Cute Chess, Arena, etc.
//
//   node uci.js
//
// Speaks the UCI protocol on stdin/stdout. Searches are synchronous, so
// `stop` is only noticed after the current search ends; every search is
// time-limited, so that is fine for tournament play.

import { createInterface } from 'node:readline';
import { Engine, ENGINE_VERSION, chooseMove } from './src/engine/index.js';
import { Position, START_FEN } from './src/index.js';

const engine = new Engine({ ttBits: 20 });
let pos = new Position();
let history = []; // UCI moves since the start position, or null after `position fen`
let useBook = false;

const send = (line) => process.stdout.write(line + '\n');

function setPosition(tokens) {
  let i = 0;
  let fen = START_FEN;
  if (tokens[i] === 'startpos') {
    i++;
  } else if (tokens[i] === 'fen') {
    const end = tokens.indexOf('moves');
    fen = tokens.slice(1, end === -1 ? tokens.length : end).join(' ');
    i = end === -1 ? tokens.length : end;
  }
  const next = new Position(fen);
  const played = [];
  if (tokens[i] === 'moves') {
    for (const text of tokens.slice(i + 1)) {
      const move = next.parseMove(text);
      if (move === null) throw new Error(`illegal move ${text}`);
      next.makeMove(move);
      played.push(text);
    }
  }
  pos = next;
  history = fen === START_FEN ? played : null;
}

function timeBudget(args, side) {
  if (args.movetime !== undefined) return Math.max(1, args.movetime - 20);
  const left = args[side === 'w' ? 'wtime' : 'btime'];
  if (left === undefined) return args.depth !== undefined || args.nodes !== undefined ? Infinity : 1000;
  const inc = args[side === 'w' ? 'winc' : 'binc'] ?? 0;
  const togo = args.movestogo ?? 30;
  const budget = left / togo + inc * 0.8;
  return Math.max(5, Math.min(budget, left - 50));
}

function go(tokens) {
  const args = {};
  for (let i = 0; i < tokens.length; i++) {
    if (['wtime', 'btime', 'winc', 'binc', 'movestogo', 'movetime', 'depth', 'nodes'].includes(tokens[i])) {
      args[tokens[i]] = Number(tokens[++i]);
    } else if (tokens[i] === 'infinite') {
      args.infinite = true;
    }
  }
  const timeMs = args.infinite ? 5000 : timeBudget(args, pos.fen().split(' ')[1]);
  const onInfo = (r) => {
    const score = r.mate !== null ? `mate ${r.mate}` : `cp ${r.score}`;
    send(`info depth ${r.depth} score ${score} nodes ${r.nodes} nps ${r.nps} time ${r.timeMs} pv ${r.pv.join(' ')}`);
  };
  const result = history && useBook
    ? chooseMove(engine, pos, history, { timeMs, onInfo })
    : engine.search(pos, { timeMs, depth: args.depth, nodes: args.nodes, onInfo });
  send(`bestmove ${result.bestMove ?? '0000'}`);
}

function handle(line) {
  const [cmd, ...tokens] = line.trim().split(/\s+/);
  switch (cmd) {
    case 'uci':
      send(`id name ChessEngine ${ENGINE_VERSION}`);
      send('id author Conor O\'Sully');
      send('option name OwnBook type check default false');
      send('uciok');
      break;
    case 'isready':
      send('readyok');
      break;
    case 'ucinewgame':
      engine.newGame();
      break;
    case 'setoption': {
      const name = tokens.slice(tokens.indexOf('name') + 1, tokens.indexOf('value')).join(' ');
      const value = tokens.slice(tokens.indexOf('value') + 1).join(' ');
      if (name.toLowerCase() === 'ownbook') useBook = value === 'true';
      break;
    }
    case 'position':
      setPosition(tokens);
      break;
    case 'go':
      go(tokens);
      break;
    case 'quit':
      process.exit(0);
  }
}

createInterface({ input: process.stdin }).on('line', (line) => {
  try {
    handle(line);
  } catch (err) {
    send(`info string error: ${err.message}`);
  }
});
