# Copyright (c) 2026 Daniel Joppi (https://github.com/swarm-routing)
# SPDX-License-Identifier: GPL-2.0-only
build_lib_example(
  NAME hopnet-example
  SOURCE_FILES hopnet-example.cc
  LIBRARIES_TO_LINK ${libhopnet} ${libswarm-core} ${libwifi} ${libmobility} ${libapplications}
                    ${libflow-monitor}
)
