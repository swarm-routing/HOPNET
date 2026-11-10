# Copyright (c) 2026 Daniel Joppi (https://github.com/swarm-routing)
# SPDX-License-Identifier: MIT OR GPL-2.0-only
#
# HOPNET's simulator-agnostic core on its own: a static library and its tests,
# with no ns-3 tree. The ns-3 module (ns3/, later) compiles the same sources.
#
#   cmake -S core -B build/core && cmake --build build/core && ctest --test-dir build/core
#
# HOPNET_CORE_SANITIZE=ON adds ASan + UBSan, as the sanitized CI leg.
cmake_minimum_required(VERSION 3.20)
project(hopnet_core LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(HOPNET_CORE_SANITIZE "Build with AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
# The strict warnings of the org's ns-3 CI (ns3-containers' swarm-quality.cmake),
# as swarm-core's core/CMakeLists.txt applies them.
add_compile_options(-Wall -Wextra -Wpedantic -Wshadow -Wnon-virtual-dtor -Wold-style-cast
                    -Wcast-align -Woverloaded-virtual -Wnull-dereference -Wformat=2 -Werror)
if(HOPNET_CORE_SANITIZE)
  add_compile_options(-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer)
  add_link_options(-fsanitize=address,undefined)
endif()

add_library(hopnet_core STATIC
  hopnet-ant.cc
  hopnet-intrazone-table.cc
  hopnet-intrazone.cc
  # swarm-core's primitives, vendored at swarm-core/PIN (swarm-core/README.md)
  swarm-core/swarm-dedup-history.cc
  swarm-core/swarm-pheromone-table.cc
  swarm-core/swarm-selection.cc
)
target_include_directories(hopnet_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(hopnet_core_tests
  test/run-core-tests.cc
  test/test-intrazone.cc
  test/test-wire.cc
)
target_link_libraries(hopnet_core_tests PRIVATE hopnet_core)

enable_testing()
add_test(NAME hopnet-core COMMAND hopnet_core_tests)
