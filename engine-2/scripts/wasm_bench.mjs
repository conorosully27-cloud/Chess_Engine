// Runs "bench" in the WebAssembly build (scripts/build_wasm.sh) and one short
// search through the same entry points the app uses. The bench node count
// must match the native build's (engine2 bench).
//   node scripts/wasm_bench.mjs
import createEngine2 from '../../engine-1/app/engine2/engine2.js';

const infos = [];
const engine = await createEngine2({ onInfo: (line) => infos.push(line) });
engine._engine_init(16);
engine._engine_bench();

engine._engine_new_game();
const go = engine.cwrap('engine_go', 'string', ['string', 'number', 'number']);
const reply = go('e2e4 e7e5 g1f3 b8c6', 1000, 0);
console.log(`${infos.length} info lines, last: ${infos.at(-1)}`);
console.log(reply);
