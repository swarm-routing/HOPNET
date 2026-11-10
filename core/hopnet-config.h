// Copyright (c) 2026 Daniel Joppi (https://github.com/swarm-routing)
// SPDX-License-Identifier: MIT OR GPL-2.0-only
//
// HOPNET's parameters. Every default cites its row in docs/fidelity.md
// (Parameters), which gives the source: [W07] (Wang, 2007 thesis) where the
// thesis states it, otherwise the maintainer decisions of 2026-11-10 on #4 and
// #5 that made it a repo choice (HOPNET golden rule 11).
#ifndef HOPNET_CORE_CONFIG_H
#define HOPNET_CORE_CONFIG_H

#include "swarm-types.h"

#include <cstddef>

namespace hopnet::core {

using swarm::core::Time;

struct Config {
    /// Zone radius in hops; also the internal forward ant's hop limit.
    /// Fidelity "Zone radius (hops)": 2, [W07] p. 36-37; "Internal forward
    /// ant hop limit": the zone radius, [W07] p. 25.
    int zoneRadius = 2;

    /// Period of a node's internal forward ants, seconds. Fidelity "Internal
    /// forward ant period": 1 s, repo choice (#4).
    Time internalAntPeriod = 1.0;

    /// A neighbour is dropped after this many internal-ant periods without an
    /// ant from it (2 x 1 s = 2 s). Fidelity Mechanisms, neighbour discovery
    /// row: repo choice (#5 (e)).
    int allowedAntLoss = 2;

    /// Deposit scale of eqs. 4.1 and 4.3. Fidelity "epsilon": 1, repo choice
    /// (#4); [W07] p. 26 leaves it to the user.
    double epsilon = 1.0;

    /// Evaporation rate E of eq. 4.2. Fidelity "Evaporation rate E, this
    /// implementation": 0.20, repo choice (#4) within [W07] p. 33's 20-25 %.
    double evaporation = 0.20;

    /// Next-hop selection exponent for ants and data. Fidelity "Next-hop
    /// selection exponent beta": 20, repo choice (#4; a declared deviation).
    double beta = 20.0;

    /// Edge time w before the first MAC sample, seconds. Fidelity "Edge time
    /// w(v_i, v_j)": 3 ms, repo choice (#5).
    Time defaultHopTime = 0.003;

    /// (source, seq) duplicate history. Fidelity "Duplicate-suppression
    /// history size": 4096, repo choice (#4); bounded (org rule 7).
    std::size_t historyCapacity = 4096;

    /// Nodes an ant's Path may hold. Fidelity "Path cap of an ant": 100,
    /// repo choice (#4); bounded (org rule 7).
    std::size_t maxPathLength = 100;

    /// IntraRT cells, (neighbour, zone node) pairs. Fidelity "IntraRT
    /// capacity": 4096, repo choice; bounded (org rule 7).
    std::size_t intraRtCapacity = 4096;

    /// Seconds without an ant after which a neighbour is dropped.
    Time neighbourTimeout() const { return internalAntPeriod * allowedAntLoss; }
};

}  // namespace hopnet::core

#endif  // HOPNET_CORE_CONFIG_H
