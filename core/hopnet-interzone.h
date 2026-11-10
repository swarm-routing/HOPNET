// Copyright (c) 2026 Daniel Joppi (https://github.com/swarm-routing)
// SPDX-License-Identifier: MIT OR GPL-2.0-only
//
// HOPNET's inter-zone (reactive) half, [W07] §4.2: route discovery by
// bordercasting external forward ants from peripheral node to peripheral node,
// the query control that steers them away from the source, the destination's
// generation check, the external backward ant, the InterRT, and data sent
// inside the zone by the IntraRT or between zones as a loose source route over
// an InterRT Path. Every reading the thesis leaves open follows the maintainer
// decision of 2026-11-10 on #5 (docs/fidelity.md cites each).
//
// Not in this slice: §4.3's local repair with notification and error ants
// (types 3 and 4 are dropped here); a failed data transmission with no other
// neighbour toward its target is dropped until that slice lands.
//
// The core is pure (HOPNET golden rule 10): the adapter calls these for every
// data packet and ant, carries out the decisions in order, and owns the queue
// of packets waiting for a route. A Unicast decision with no message sends the
// data packet, its header as routeData() left it.
#ifndef HOPNET_CORE_INTERZONE_H
#define HOPNET_CORE_INTERZONE_H

#include "hopnet-ant.h"
#include "hopnet-config.h"
#include "hopnet-data.h"
#include "hopnet-interzone-table.h"
#include "hopnet-intrazone.h"
#include "swarm-dedup-history.h"

#include <cstdint>
#include <deque>
#include <map>
#include <vector>

namespace hopnet::core {

/// What one call asks the adapter to do.
struct Outcome {
    /// Carried out in order.
    std::vector<Decision> decisions;
    /// An InterRT route to this node was just stored: offer the packets queued
    /// for it to routeData() again.
    NodeAddress routeReady = kInvalidAddress;
};

class InterZone {
public:
    /// `intra` is this node's intra-zone half (its IntraRT, selection, edge
    /// time and SeqNo counter); it and the clock must outlive the object.
    InterZone(const Config& config, IntraZone& intra, const IClock& clock);

    /// A data packet at this node: from the local transport (`from` =
    /// kInvalidAddress, header.source = self) or from neighbour `from`. Fills
    /// in the header at the source; Deliver, Unicast (the packet to
    /// nextHop), Queue (a discovery runs: hold it, re-offer on routeReady) or
    /// Drop (no pheromone toward the target, [W07] p. 34).
    Outcome routeData(DataHeader& header, NodeAddress from);

    /// Every ant this node receives, from neighbour `from`. The intra-zone ants
    /// go to IntraZone::onReceive; external forward ants and external backward
    /// ants (target set) are handled here.
    Outcome onReceive(const Ant& ant, NodeAddress from);

    /// The unicast of a data packet to `nextHop` failed (the adapter reports
    /// MAC failures): the neighbour's row goes at once, and the packet takes
    /// another neighbour with pheromone toward its target, if there is one
    /// (#5 (e)). Otherwise it is dropped: the local repair is the next slice.
    Outcome onDataSendFailure(DataHeader& header, NodeAddress nextHop);

    /// Starts a route discovery for `dest` unless one started less than
    /// Config::discoveryRetryInterval ago: one external forward ant per
    /// peripheral node, [W07] p. 27. Empty when none starts or there is no
    /// peripheral node to send to (never a Drop: nothing was received).
    std::vector<Decision> discover(NodeAddress dest);

    const InterZoneTable& table() const { return table_; }
    /// The highest SeqNo this node, as a destination, accepted from `source`.
    std::uint32_t acceptedSeq(NodeAddress source) const;

private:
    Outcome onExternalForward(const Ant& ant, NodeAddress from);
    Outcome onExternalBackward(const Ant& ant, NodeAddress from);
    Outcome atDestination(const Ant& ant);
    Outcome atBordercaster(const Ant& ant, NodeAddress from);
    std::vector<Decision> bordercast(const Ant& ant, NodeAddress from);
    bool acceptGeneration(NodeAddress source, std::uint32_t seq);
    /// The ant forwarded one hop toward its target by the selection rule,
    /// never through `exclude`; `appendSelf` adds this node to Path (a
    /// bordercaster). Unicast, or Drop with no next hop ([W07] p. 34), a full
    /// Path or the travelled bound.
    Decision relay(Ant ant, bool appendSelf, NodeAddress exclude = kInvalidAddress);
    Decision sendData(DataHeader& header, NodeAddress exclude = kInvalidAddress);

    Config config_;
    IntraZone& intra_;
    const IClock& clock_;
    InterZoneTable table_;
    swarm::core::DedupHistory bordercast_;           // external forward ants seen as a bordercaster
    std::map<NodeAddress, std::uint32_t> accepted_;  // per source, as a destination
    std::deque<NodeAddress> acceptedOrder_;          // FIFO eviction, historyCapacity
    std::map<NodeAddress, Time> discoveries_;        // last discovery per destination
};

}  // namespace hopnet::core

#endif  // HOPNET_CORE_INTERZONE_H
