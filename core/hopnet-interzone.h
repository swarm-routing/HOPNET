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
// §4.3's local repair follows the maintainer decision of 2026-11-11 on #5,
// readings (i)-(l): a data transmission that fails with no other neighbour
// toward its target starts a repair at the node it failed at (M). M sends an
// ordinary external forward ant of its own to the destination (i) and buffers
// the packets for it; on the backward ant, the buffered packets take M's new
// record, spliced onto their own route prefix, and each source they came from
// gets a notification ant (type 3) carrying the joined, loop-erased route and
// the record's SeqNo (j, l); with no backward ant within the repair wait, each
// gets an error ant (type 4) instead. Both travel back over the source's own
// prefix, and the nodes they pass remove their invalid InterRT routes (k).
//
// The core is pure (HOPNET golden rule 10): the adapter calls these for every
// data packet and ant, carries out the decisions in order, and owns the queue
// of packets waiting for a route (Config::repairBufferCapacity packets, each
// at most Config::repairBufferTimeout) and the repair timer. A Unicast
// decision with no message sends the data packet, its header as the call
// left it.
#ifndef HOPNET_CORE_INTERZONE_H
#define HOPNET_CORE_INTERZONE_H

#include "hopnet-ant.h"
#include "hopnet-config.h"
#include "hopnet-data.h"
#include "hopnet-interzone-table.h"
#include "hopnet-intrazone.h"
#include "swarm-dedup-history.h"

#include <cstddef>
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
    /// for it to resume().
    NodeAddress routeReady = kInvalidAddress;
    /// A local repair toward this destination started: call
    /// onRepairTimeout(repairFor) after repairWait seconds.
    NodeAddress repairFor = kInvalidAddress;
    Time repairWait = 0.0;
};

/// A source's route prefix <S ... M> joined to a repaired route <M ... D>,
/// loop-erased: when a node recurs, everything between its two visits is
/// dropped (#5 (j)). `junction` is M's index in `route`, or, when the loop
/// erased went through M, the index of the node where it closed (the node M
/// sends a packet or a notification ant toward).
struct Splice {
    std::vector<NodeAddress> route;
    std::size_t junction = 0;
};
Splice spliceRoute(const std::vector<NodeAddress>& prefix,
                   const std::vector<NodeAddress>& repaired);

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

    /// A packet the adapter held on a Queue decision, offered again on
    /// routeReady: this node's own packets as from the transport; a packet a
    /// local repair buffered goes on, on the repaired route (#5 (i)).
    Outcome resume(DataHeader& header);

    /// Every ant this node receives, from neighbour `from`. The intra-zone ants
    /// go to IntraZone::onReceive; external forward ants and external backward
    /// ants (target set) are handled here.
    Outcome onReceive(const Ant& ant, NodeAddress from);

    /// The unicast of a data packet to `nextHop` failed (the adapter reports
    /// MAC failures): the neighbour's row goes at once, and the packet takes
    /// another neighbour with pheromone toward its target, if there is one
    /// (#5 (e)). Otherwise an intra-zone packet is dropped (the IntraRT heals,
    /// [W07] §4.3); at its source an inter-zone packet's record goes and a
    /// discovery starts (#5 (i)); elsewhere a local repair starts, or the
    /// packet joins the one running (Queue).
    Outcome onDataSendFailure(DataHeader& header, NodeAddress nextHop);

    /// The repair wait for `dest` is over: if no backward ant came, every
    /// source of the packets buffered for it gets an error ant, and the packets
    /// are discarded (DiscardPending). Nothing when the repair already ended
    /// or its wait is not over (a timer from an earlier repair).
    Outcome onRepairTimeout(NodeAddress dest);

    /// A local repair toward `dest` is running here.
    bool repairing(NodeAddress dest) const { return repairs_.count(dest) > 0; }

    /// Starts a route discovery for `dest` unless one started less than
    /// Config::discoveryRetryInterval ago: one external forward ant per
    /// peripheral node, [W07] p. 27. Empty when none starts or there is no
    /// peripheral node to send to (never a Drop: nothing was received).
    std::vector<Decision> discover(NodeAddress dest);

    const InterZoneTable& table() const { return table_; }
    /// The highest SeqNo this node, as a destination, accepted from `source`.
    std::uint32_t acceptedSeq(NodeAddress source) const;

private:
    /// A running local repair ([W07] §4.3).
    struct Repair {
        Time deadline = 0.0;
        NodeAddress lost = kInvalidAddress;  // the target the failed packet was routed to
        /// Per source, the newest packet buffered from it (#5 (l)); at most
        /// Config::repairBufferCapacity sources.
        std::map<NodeAddress, DataHeader> sources;
    };

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

    // Local repair (hopnet-repair.cc).
    /// A packet this node forwards, not its source: a packet still bound for a
    /// repaired segment takes the repair's record; one that cannot be sent
    /// while a repair for its destination runs is buffered.
    Outcome forward(DataHeader& header);
    Outcome startRepair(DataHeader& header);
    void buffer(Repair& repair, const DataHeader& header) const;
    std::vector<Decision> repairSucceeded(const Repair& repair, const InterRoute& record,
                                          const Ant& backward);
    /// The route prefix <S ... self> of a packet here: up to self when self is
    /// on its route, else up to the Path node before its target, then self (a
    /// transit relay).
    std::vector<NodeAddress> prefixHere(const DataHeader& header) const;
    bool rewrite(DataHeader& header, const InterRoute& record) const;
    Decision errorAnt(const DataHeader& header);
    Outcome onRepairAnt(const Ant& ant, NodeAddress from);
    bool invalidated(const InterRoute& record, const Ant& ant) const;

    Config config_;
    IntraZone& intra_;
    const IClock& clock_;
    InterZoneTable table_;
    swarm::core::DedupHistory bordercast_;           // external forward ants seen as a bordercaster
    std::map<NodeAddress, std::uint32_t> accepted_;  // per source, as a destination
    std::deque<NodeAddress> acceptedOrder_;          // FIFO eviction, historyCapacity
    std::map<NodeAddress, Time> discoveries_;        // last discovery per destination
    std::map<NodeAddress, Repair> repairs_;          // per destination, interRtCapacity
};

}  // namespace hopnet::core

#endif  // HOPNET_CORE_INTERZONE_H
