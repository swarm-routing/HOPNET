// Copyright (c) 2026 Daniel Joppi (https://github.com/swarm-routing)
// SPDX-License-Identifier: MIT OR GPL-2.0-only
//
// The Intrazone Routing Table (IntraRT), [W07] §4.1, p. 25: rows are the
// node's one-hop neighbours, columns the nodes of its zone, and each cell holds
// <pheromone, times visited, hops, SeqNo>. The pheromone lives in swarm-core's
// PheromoneTable (destination = column, neighbour = row), so next-hop
// selection is swarm-core's selectNextHop; the other three fields sit beside
// it under the same key and the same cap (org rule 7).
//
// Ordered maps throughout, so every enumeration is deterministic (org rule 5).
#ifndef HOPNET_CORE_INTRAZONE_TABLE_H
#define HOPNET_CORE_INTRAZONE_TABLE_H

#include "swarm-observer.h"
#include "swarm-pheromone-table.h"
#include "swarm-types.h"

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <utility>
#include <vector>

namespace hopnet::core {

using swarm::core::NodeAddress;
using swarm::core::PheromoneTable;

/// One IntraRT cell.
struct Cell {
    double pheromone = 0.0;
    /// [W07] p. 25 defines it; no rule in ch. 4 reads it (fidelity, "times
    /// visited" row). Counted, read by nothing.
    std::uint32_t visits = 0;
    /// Hops from this node to the column's node through the row's neighbour.
    int hops = 0;
    /// The last ant generation that updated the cell ([W07] p. 25).
    std::uint32_t seq = 0;

    bool operator==(const Cell&) const = default;
};

class IntraZoneTable {
public:
    /// At most `capacity` cells; `evaporation` is E of eq. 4.2. `observer`
    /// (optional, null: none) hears each cell added or removed as a route
    /// change: destination = the zone node (column), next hop = the neighbour
    /// (row).
    IntraZoneTable(std::size_t capacity, double evaporation,
                   swarm::core::IRouterObserver* observer = nullptr);

    /// One ant update of cell (neighbour, column): unless the cell already
    /// holds `seq`, adds `deposit` to its pheromone (eq. 4.1 or 4.3), sets its
    /// hops and seq, counts the visit, and multiplies every other neighbour's
    /// pheromone in the same column by (1 - E) (eq. 4.2). A cell is created by
    /// its first deposit (fidelity "Initial pheromone": none). Returns false,
    /// changing nothing, on a repeated seq, a deposit that is not finite and
    /// positive, or a new cell in a full table.
    bool update(NodeAddress column, NodeAddress neighbour, double deposit, int hops,
                std::uint32_t seq);

    std::optional<Cell> cell(NodeAddress column, NodeAddress neighbour) const;

    /// Removes the neighbour's row (a lost link); returns how many cells.
    std::size_t removeNeighbour(NodeAddress neighbour);

    /// The rows: neighbours holding at least one cell, ascending.
    std::vector<NodeAddress> neighbours() const;
    bool hasNeighbour(NodeAddress neighbour) const;
    /// The columns: the zone's nodes, ascending.
    std::vector<NodeAddress> zone() const;
    /// The fewest hops over the column's cells; nullopt when there is none.
    std::optional<int> minHops(NodeAddress column) const;
    /// The zone's peripheral nodes: minimum hops over the column equal to
    /// `radius` (fidelity, peripheral row; #5 (f)). Ascending.
    std::vector<NodeAddress> peripheral(int radius) const;

    /// The pheromone, for swarm-core's selection functions.
    const PheromoneTable& pheromone() const { return pheromone_; }
    std::size_t size() const { return meta_.size(); }
    std::size_t capacity() const { return pheromone_.capacity(); }
    /// What the cells hold, in bytes: per cell its two addresses and the
    /// fields of Cell (payload, not allocator overhead).
    std::size_t bytes() const { return meta_.size() * (2 * sizeof(NodeAddress) + sizeof(Cell)); }

private:
    using Key = std::pair<NodeAddress, NodeAddress>;  // (column, neighbour), as PheromoneTable

    PheromoneTable pheromone_;
    std::map<Key, Cell> meta_;  // every field but the pheromone
    double retain_;             // 1 - E
    swarm::core::IRouterObserver* observer_;
};

}  // namespace hopnet::core

#endif  // HOPNET_CORE_INTRAZONE_TABLE_H
