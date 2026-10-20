#!/usr/bin/env bash
# Builds Engine 2 as WebAssembly for the browser app:
#   engine-1/app/engine2/engine2.js, engine2.wasm and version.js
# Needs the Emscripten SDK (https://emscripten.org), e.g. in ~/tools/emsdk:
#   git clone https://github.com/emscripten-core/emsdk ~/tools/emsdk
#   ~/tools/emsdk/emsdk install latest && ~/tools/emsdk/emsdk activate latest
# Check it with: node scripts/wasm_bench.mjs (bench node count must match
# the native build's).
set -euo pipefail

here="$(cd "$(dirname "$0")/.." && pwd)"
out="$here/../engine-1/app/engine2"
emsdk="${EMSDK:-$HOME/tools/emsdk}"
# shellcheck disable=SC1091
command -v em++ > /dev/null || source "$emsdk/emsdk_env.sh" > /dev/null 2>&1
mkdir -p "$out"

# Every source except main.cpp (the native program); wasm.cpp is the entry.
# Plus the built-in NNUE network, as the native build embeds it.
gen="$(mktemp -d)"
trap 'rm -rf "$gen"' EXIT
cmake -DNET="$here/nets/engine-2.5.nnue" -DOUT="$gen/default_net.cpp" -P "$here/cmake/embed_net.cmake"
sources=("$gen/default_net.cpp")
for f in "$here"/src/*.cpp; do
  [ "$(basename "$f")" = "main.cpp" ] || sources+=("$f")
done

# No AVX2 (desktop CPUs only) and no threads: the app's Web Worker runs the
# search. WebAssembly SIMD (-msimd128; Chrome/Android WebView 91+, Safari
# 16.4+) vectorizes the NNUE: 1.5x the speed of plain WebAssembly. The
# search recurses up to 100 plies with move lists on the stack, far past
# Emscripten's 64 KB default stack.
em++ -std=c++20 -O3 -DNDEBUG -msimd128 -I"$here/src" "${sources[@]}" \
  -sMODULARIZE=1 -sEXPORT_ES6=1 -sEXPORT_NAME=createEngine2 \
  -sENVIRONMENT=web,worker,node \
  -sALLOW_MEMORY_GROWTH=1 -sSTACK_SIZE=8MB \
  -sEXPORTED_FUNCTIONS=_engine_init,_engine_new_game,_engine_go,_engine_bench \
  -sEXPORTED_RUNTIME_METHODS=ccall,cwrap,UTF8ToString \
  -o "$out/engine2.js"

# The engine's name and version (src/uci.cpp), for the app's labels.
name="$(sed -n 's/.*ENGINE_NAME = "\(.*\)";.*/\1/p' "$here/src/uci.cpp")"
printf '// Written by engine-2/scripts/build_wasm.sh: the name of the build in engine2.wasm.\nexport const ENGINE2_NAME = %s;\n' "'$name'" > "$out/version.js"
ls -l "$out"
