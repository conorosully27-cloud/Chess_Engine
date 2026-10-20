cmake_minimum_required(VERSION 3.16)
project(engine2 CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

option(ENGINE_AVX2 "Compile with AVX2 (needed for fast NNUE later)" ON)

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

add_library(engine2_core STATIC
  src/bench.cpp
  src/bitboard.cpp
  src/datagen.cpp
  src/position.cpp
  src/movegen.cpp
  src/nnue.cpp
  src/evaluate.cpp
  src/search.cpp
  src/tt.cpp
  src/uci.cpp)
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

add_executable(search_test tests/search_test.cpp)
target_link_libraries(search_test PRIVATE engine2_core)
add_test(NAME search COMMAND search_test ${CMAKE_CURRENT_SOURCE_DIR}/tests/eval_reference.tsv)

# Drives the real binary over UCI the way a GUI does. Needs Python 3.
find_package(Python3 COMPONENTS Interpreter)
if(Python3_FOUND)
  add_test(NAME uci COMMAND Python3::Interpreter ${CMAKE_CURRENT_SOURCE_DIR}/tests/uci_test.py $<TARGET_FILE:engine2>)
endif()
