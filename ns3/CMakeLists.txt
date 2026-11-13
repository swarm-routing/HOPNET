# Copyright (c) 2026 Daniel Joppi (https://github.com/swarm-routing)
# SPDX-License-Identifier: GPL-2.0-only
#
# The `hopnet` ns-3 contrib module (#5): HOPNET's simulator-free core/ (one
# directory up, MIT OR GPL-2.0-only) compiled into ns-3 on swarm-core, with the
# adapter that carries out its decisions (model/), a helper, the harness arm
# (helper/hopnet-bench-arms.cc, compiled only when routing-bench is installed
# beside it), an example and the test suite. Install it next to swarm-core:
#
#   ns3-build-contrib --test swarm-core=/src/ns3-swarm-core hopnet=/src/hopnet/ns3
#
# ns-3 reaches this directory through contrib/hopnet, a link to the
# checkout's ns3/: core/ is found beside the link's target, not beside the link.
get_filename_component(hopnet_ns3_dir "${CMAKE_CURRENT_SOURCE_DIR}" REALPATH)  # CMake < 3.19 too
get_filename_component(hopnet_core_dir "${hopnet_ns3_dir}/../core" ABSOLUTE)
# core/ includes swarm-core's primitives without the ns3/ prefix (they are the
# same files as ns3/swarm-*.h), so it builds alone as well.
set(hopnet_swarm_core_dir "${PROJECT_SOURCE_DIR}/contrib/swarm-core/core")
foreach(d "${hopnet_core_dir}" "${hopnet_swarm_core_dir}")
  if(NOT EXISTS "${d}")
    message(FATAL_ERROR "hopnet: ${d} not found (install swarm-core as contrib/swarm-core)")
  endif()
endforeach()
include_directories("${hopnet_core_dir}" "${hopnet_swarm_core_dir}")

# The harness arm, only where swarm-routing/ns3-tools' routing-bench is
# installed and enabled. Its library is named through a variable: ns-3 scans
# module CMakeLists for library tokens, and `hopnet` must also build alone
# (routing-bench README, "Registering an arm").
set(hopnet_bench_sources)
set(hopnet_bench_libs)
set(hopnet_bench_module routing-bench)
if(EXISTS ${PROJECT_SOURCE_DIR}/contrib/${hopnet_bench_module}/CMakeLists.txt
   AND hopnet_bench_module IN_LIST ns3-all-enabled-modules)
  set(hopnet_bench_sources helper/hopnet-bench-arms.cc)
  set(hopnet_bench_lib lib${hopnet_bench_module})
  set(hopnet_bench_libs ${${hopnet_bench_lib}})
endif()

build_lib(
  LIBNAME hopnet
  SOURCE_FILES
    ${hopnet_core_dir}/hopnet-ant.cc
    ${hopnet_core_dir}/hopnet-data.cc
    ${hopnet_core_dir}/hopnet-interzone-table.cc
    ${hopnet_core_dir}/hopnet-interzone.cc
    ${hopnet_core_dir}/hopnet-intrazone-table.cc
    ${hopnet_core_dir}/hopnet-intrazone.cc
    ${hopnet_core_dir}/hopnet-repair.cc
    model/hopnet-header.cc
    model/hopnet-routing-protocol.cc
    helper/hopnet-helper.cc
    ${hopnet_bench_sources}
  HEADER_FILES
    model/hopnet-routing-protocol.h
    helper/hopnet-helper.h
  LIBRARIES_TO_LINK ${libswarm-core} ${libcore} ${libnetwork} ${libinternet} ${hopnet_bench_libs}
  TEST_SOURCES
    ${hopnet_core_dir}/test/test-interzone.cc
    ${hopnet_core_dir}/test/test-intrazone.cc
    ${hopnet_core_dir}/test/test-observer.cc
    ${hopnet_core_dir}/test/test-repair.cc
    ${hopnet_core_dir}/test/test-wire.cc
    test/hopnet-test-suite.cc
)
