# Copyright (c) 2026 Daniel Joppi (https://github.com/swarm-routing)
# SPDX-License-Identifier: MIT OR GPL-2.0-only
#
# A scratch consumer of the installed package hopnet-core (core.yml's package
# check): find_package finds it and its swarm-core dependency, hopnet::core links.
cmake_minimum_required(VERSION 3.20)
project(hopnet_core_package_consumer LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
find_package(hopnet-core CONFIG REQUIRED)
message(STATUS "hopnet-core ${hopnet-core_VERSION} from ${hopnet-core_DIR}")
add_executable(consumer consumer.cc)
target_link_libraries(consumer PRIVATE hopnet::core)
