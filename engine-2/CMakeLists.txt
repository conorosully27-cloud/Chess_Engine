cmake_minimum_required(VERSION 3.16)
project(engine2 CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

option(ENGINE_AVX2 "Compile with AVX2 (vectorizes the NNUE)" ON)

# Flags shared by every target.
add_library(engine2_options INTERFACE)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(engine2_options INTERFACE -Wall -Wextra -Wshadow)
  if(ENGINE_AVX2)
    target_compile_options(engine2_options INTERFACE -mavx2 -mpopcnt)
  endif()
  if(WIN32)
    # MinGW: link the runtime in so engine2.exe runs without extra DLLs.
    target_link_options(engine2_options INTERFACE -static)
  endif()
endif()

# Hidden units per side of the NNUE (src/accumulator.h); empty keeps the
# source's default. The built-in network must have this size.
set(ENGINE_NNUE_HIDDEN "" CACHE STRING "NNUE hidden units per side (empty: the default in src/accumulator.h)")
if(ENGINE_NNUE_HIDDEN)
  target_compile_definitions(engine2_options INTERFACE NNUE_HIDDEN=${ENGINE_NNUE_HIDDEN})
endif()

# Tuning build: the search parameters in src/tune.h become UCI options
# (for scripts/tune/spsa.py). Off for normal builds, where they are constants.
option(ENGINE_TUNE "Expose the search parameters as UCI options, for tuning" OFF)
if(ENGINE_TUNE)
  target_compile_definitions(engine2_options INTERFACE ENGINE_TUNE)
endif()

# The NNUE network built into the engine (nets/, see src/nnue.h).
set(ENGINE_NET ${CMAKE_CURRENT_SOURCE_DIR}/nets/engine-2.9.nnue CACHE FILEPATH "The network built into the engine")
set(DEFAULT_NET_CPP ${CMAKE_CURRENT_BINARY_DIR}/default_net.cpp)
add_custom_command(
  OUTPUT ${DEFAULT_NET_CPP}
  COMMAND ${CMAKE_COMMAND} -DNET=${ENGINE_NET} -DOUT=${DEFAULT_NET_CPP} -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_net.cmake
  DEPENDS ${ENGINE_NET} ${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_net.cmake
  COMMENT "Embedding the NNUE network")

add_library(engine2_core STATIC
  src/bench.cpp
  src/bitboard.cpp
  src/datagen.cpp
  src/endgame.cpp
  src/position.cpp
  src/movegen.cpp
  src/movepick.cpp
  src/nnue.cpp
  src/evaluate.cpp
  src/search.cpp
  src/tt.cpp
  src/uci.cpp
  ${DEFAULT_NET_CPP})
target_include_directories(engine2_core PUBLIC src)
find_package(Threads REQUIRED)
target_link_libraries(engine2_core PUBLIC engine2_options Threads::Threads)

add_executable(engine2 src/main.cpp)
target_link_libraries(engine2 PRIVATE engine2_core)

enable_testing()
add_executable(perft_test tests/perft_test.cpp)
target_link_libraries(perft_test PRIVATE engine2_core)
add_test(NAME perft COMMAND perft_test)

add_executable(position_test tests/position_test.cpp)
target_link_libraries(position_test PRIVATE engine2_core)
add_test(NAME position COMMAND position_test)

add_executable(see_test tests/see_test.cpp)
target_link_libraries(see_test PRIVATE engine2_core)
add_test(NAME see COMMAND see_test)

add_executable(movepick_test tests/movepick_test.cpp)
target_link_libraries(movepick_test PRIVATE engine2_core)
add_test(NAME movepick COMMAND movepick_test)

add_executable(nnue_test tests/nnue_test.cpp)
target_link_libraries(nnue_test PRIVATE engine2_core)
add_test(NAME nnue COMMAND nnue_test)

add_executable(endgame_test tests/endgame_test.cpp)
target_link_libraries(endgame_test PRIVATE engine2_core)
add_test(NAME endgame COMMAND endgame_test)

add_executable(search_test tests/search_test.cpp)
target_link_libraries(search_test PRIVATE engine2_core)
add_test(NAME search COMMAND search_test ${CMAKE_CURRENT_SOURCE_DIR}/tests/eval_reference.tsv)

# Drives the real binary over UCI the way a GUI does. Needs Python 3.
find_package(Python3 COMPONENTS Interpreter)
if(Python3_FOUND)
  add_test(NAME uci COMMAND Python3::Interpreter ${CMAKE_CURRENT_SOURCE_DIR}/tests/uci_test.py $<TARGET_FILE:engine2>)
endif()
