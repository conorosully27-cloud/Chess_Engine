# Writes a C++ file holding an NNUE network file's bytes, so the engine
# carries its network with it (src/nnue.cpp, load_default()).
#   cmake -DNET=<network file> -DOUT=<.cpp to write> -P cmake/embed_net.cmake
# Used by CMakeLists.txt and scripts/build_wasm.sh.
file(READ "${NET}" hex HEX)
string(LENGTH "${hex}" hex_length)
math(EXPR size "${hex_length} / 2")
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," bytes "${hex}")
file(WRITE "${OUT}.tmp"
  "// Generated from ${NET} by cmake/embed_net.cmake. Do not edit.\n"
  "#include <cstddef>\n"
  "namespace chess::nnue {\n"
  "extern const unsigned char default_net_data[] = {${bytes}};\n"
  "extern const std::size_t default_net_size = ${size};\n"
  "}  // namespace chess::nnue\n")
# Only touch OUT when it changes, so builds don't recompile it needlessly.
execute_process(COMMAND ${CMAKE_COMMAND} -E copy_if_different "${OUT}.tmp" "${OUT}")
file(REMOVE "${OUT}.tmp")
