// Copyright (c) 2026 Daniel Joppi (https://github.com/swarm-routing)
// SPDX-License-Identifier: MIT OR GPL-2.0-only
//
// HOPNET's intra-zone (proactive) half, [W07] §4.1: the periodic internal
// forward ants, their zone-limited flood, the internal backward ants, the
// IntraRT updates of eqs. 4.1-4.3, neighbour discovery and loss, and next-hop
// selection inside the zone. Every reading the thesis leaves open follows the
// maintainer decision of 2026-11-10 on #5 (docs/fidelity.md cites each).
//
// The core is pure: it never sends or schedules anything itself. The adapter
// calls onInternalAntTimer() every Config::internalAntPeriod and onReceive()
// for each ant, then carries out the RouteDecision returned (HOPNET golden rule
// 10). Time comes through swarm-core's IClock, the edge time through its
// ILinkState, randomness through its IRng (org rule 5).
//
// Observation (#40): an optional swarm::core::IRouterObserver* (null: none)
// hears the internal and backward ants this half sends and receives, the
// IntraRT cells added and removed, and the footprint of the node's two
// tables. The observer only reads: the decisions are the same with and
// without one (hopnet-observe.h).
#ifndef HOPNET_CORE_INTRAZONE_H
#define HOPNET_CORE_INTRAZONE_H

#include "hopnet-ant.h"
#include "hopnet-config.h"
#include "hopnet-interzone-table.h"
#include "hopnet-intrazone-table.h"
#include "swarm-observer.h"
#include "swarm-dedup-history.h"
#include "swarm-ports.h"
#include "swarm-route-decision.h"

#include <cstdint>
#include <map>

namespace hopnet::core {

using swarm::core::IClock;
using swarm::core::ILinkState;
using swarm::core::IRng;
using Decision = swarm::core::RouteDecision<Ant>;

class IntraZone {
public:
    /// The ports (and `observer`, when given) must outlive the object.
    IntraZone(const Config& config, NodeAddress self, const IClock& clock, const ILinkState& link,
              IRng& rng, swarm::core::IRouterObserver* observer = nullptr);

    /// Every Config::internalAntPeriod: drops neighbours silent for
    /// Config::neighbourTimeout() and cells no ant refreshed for as long
    /// (purgeStaleNeighbours), then returns the broadcast of a new internal
    /// forward ant (Hops = zone radius, [W07] p. 25).
    Decision onInternalAntTimer();

    /// An ant received from neighbour `from`. Internal forward ants: eq. 4.1
    /// into cell (from, source), the first copy of (source, seq) re-broadcast
    /// while Hops is left, else turned into a backward ant. Backward ants:
    /// eq. 4.3 into cell (from, source), then on along Path. Other types are
    /// the inter-zone half's (InterZone::onReceive, which calls this): Drop.
    /// Only the ants this half handles are reported to the observer.
    Decision onReceive(const Ant& ant, NodeAddress from);

    /// A unicast transmission to `neighbour` failed (the adapter reports MAC
    /// failures): its row goes at once (fidelity, neighbour discovery row).
    void onLinkFailure(NodeAddress neighbour);

    /// Removes the rows of neighbours with no ant for neighbourTimeout(), then
    /// every cell no ant has updated (eq. 4.1 or 4.3) for neighbourTimeout():
    /// a zone node that is no longer reachable through a neighbour that stays
    /// (fidelity, stale IntraRT entry row; #93).
    void purgeStaleNeighbours();

    /// Next hop toward zone node `dest` by roulette over its IntraRT column
    /// (swarm-core selectNextHop, weights (phi / phi_best)^beta); `exclude` is
    /// left out unless it is the only candidate. kInvalidAddress when no
    /// neighbour holds pheromone: the caller drops the packet ([W07] p. 34).
    NodeAddress selectNextHop(NodeAddress dest, NodeAddress exclude = kInvalidAddress);

    /// The edge time w(self, nextHop): (Q_mac + 1) * T_mac from ILinkState,
    /// or Config::defaultHopTime before the first MAC sample (fidelity, edge
    /// time row; #5).
    double edgeTime(NodeAddress nextHop) const;

    /// Reports the node's routing-state footprint (IntraRT cells plus the
    /// InterRT records of the table registered by trackInterRt()) to the
    /// observer, if it changed since the last report. Called after every
    /// entry point that can change a table; "on demand" for the adapter.
    void reportState();
    /// The InterRT of this node's InterZone, so the footprint covers both
    /// tables (InterZone's constructor calls it; the table must outlive this).
    void trackInterRt(const InterZoneTable* table) { interTable_ = table; }

    const IntraZoneTable& table() const { return table_; }
    NodeAddress self() const { return self_; }
    /// The sequence number of the last ant this node originated.
    std::uint32_t lastSeq() const { return seq_; }
    /// A fresh sequence number for an ant this node sends: one counter for
    /// every forward and backward ant, internal or external ([W07] p. 25).
    std::uint32_t nextSeq() { return ++seq_; }

    /// An ant arrived from `neighbour`: its row's loss timer restarts (any ant
    /// counts, the inter-zone ones too; fidelity, neighbour discovery row).
    void heard(NodeAddress neighbour);

private:
    Decision onInternalForward(const Ant& ant, NodeAddress from);
    Decision onInternalBackward(const Ant& ant, NodeAddress from);

    Config config_;
    NodeAddress self_;
    const IClock& clock_;
    const ILinkState& link_;
    IRng& rng_;
    IntraZoneTable table_;
    swarm::core::DedupHistory forwarded_;    // first copies of internal forward ants
    std::map<NodeAddress, Time> lastHeard_;  // rows only, so bounded by the table
    std::uint32_t seq_ = 0;
    swarm::core::IRouterObserver* observer_;
    const InterZoneTable* interTable_ = nullptr;
    std::uint32_t reportedEntries_ = 0;
    std::uint32_t reportedBytes_ = 0;  // both start at 0: an empty footprint is not news
};

}  // namespace hopnet::core

#endif  // HOPNET_CORE_INTRAZONE_H
