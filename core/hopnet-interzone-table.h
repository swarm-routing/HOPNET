// Copyright (c) 2026 Daniel Joppi (https://github.com/swarm-routing)
// SPDX-License-Identifier: MIT OR GPL-2.0-only
//
// The Interzone Routing Table (InterRT), [W07] p. 27: per destination one
// record of Destination, SeqNo, Path and Expire, a source route kept until it
// expires. One Path per destination (#5 (d)): a newer generation (higher
// SeqNo) replaces it, an older one is ignored. Expire is Config::routeLifetime
// after the last use (#5, the InterRT Expire row). Capped (org rule 7); an
// ordered map, so enumeration is deterministic (org rule 5).
#ifndef HOPNET_CORE_INTERZONE_TABLE_H
#define HOPNET_CORE_INTERZONE_TABLE_H

#include "swarm-types.h"

#include <cstddef>
#include <cstdint>
#include <map>
#include <vector>

namespace hopnet::core {

using swarm::core::NodeAddress;
using swarm::core::Time;

/// One InterRT record.
struct InterRoute {
    std::uint32_t seq = 0;
    /// The Path: this node first, the destination last, the bordercasters
    /// between ([W07] p. 28, Fig. 4.1).
    std::vector<NodeAddress> path;
    /// The record is valid while now < expire.
    Time expire = 0.0;
    /// T(S, D), the path time the discovering ants measured: the local-repair
    /// wait's estimate (fidelity, "Local-repair wait").
    Time delay = 0.0;
    /// A record a local repair stored ([W07] §4.3): the node after this one on
    /// the route the repair replaced (the far end of the broken link, or the
    /// Path node a transit relay was routing to). A packet still bound for it
    /// takes this record instead (#5 (i)). kInvalidAddress otherwise.
    NodeAddress replaces = swarm::core::kInvalidAddress;

    bool operator==(const InterRoute&) const = default;
};

class InterZoneTable {
public:
    /// At most `capacity` records, each valid `lifetime` seconds after its
    /// insertion or last use.
    InterZoneTable(std::size_t capacity, Time lifetime);

    /// Inserts the record for `dest` (path.back()) unless an unexpired one
    /// holds a SeqNo >= `seq`. A full table first drops its expired records,
    /// then refuses a new destination. Returns true when stored. `replaces`:
    /// see InterRoute::replaces.
    bool insert(std::uint32_t seq, std::vector<NodeAddress> path, Time delay, Time now,
                NodeAddress replaces = swarm::core::kInvalidAddress);

    /// The unexpired record for `dest`, or nullptr.
    const InterRoute* find(NodeAddress dest, Time now) const;

    /// A data packet is sent on the record: its expiry moves to now +
    /// lifetime. False when there is no unexpired record.
    bool use(NodeAddress dest, Time now);

    bool erase(NodeAddress dest);
    /// Drops the expired records; returns how many.
    std::size_t purge(Time now);

    std::size_t size() const { return routes_.size(); }
    std::size_t capacity() const { return capacity_; }

private:
    std::size_t capacity_;
    Time lifetime_;
    std::map<NodeAddress, InterRoute> routes_;
};

}  // namespace hopnet::core

#endif  // HOPNET_CORE_INTERZONE_TABLE_H
