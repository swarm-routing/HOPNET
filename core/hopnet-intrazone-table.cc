// Copyright (c) 2026 Daniel Joppi (https://github.com/swarm-routing)
// SPDX-License-Identifier: MIT OR GPL-2.0-only
#include "hopnet-intrazone-table.h"

#include <cmath>
#include <limits>
#include <set>

namespace hopnet::core {

IntraZoneTable::IntraZoneTable(std::size_t capacity, double evaporation,
                               swarm::core::IRouterObserver* observer)
    : pheromone_(capacity), retain_(1.0 - evaporation), observer_(observer) {}

bool IntraZoneTable::update(NodeAddress column, NodeAddress neighbour, double deposit, int hops,
                            std::uint32_t seq) {
    if (!std::isfinite(deposit) || deposit <= 0.0) return false;
    const Key key{column, neighbour};
    auto it = meta_.find(key);
    if (it != meta_.end() && it->second.seq == seq) return false;
    const bool created = it == meta_.end();
    if (!pheromone_.add(column, neighbour, deposit)) return false;  // full: no new cell
    Cell& c = meta_[key];
    c.hops = hops;
    c.seq = seq;
    ++c.visits;
    for (const auto& e : pheromone_.entries(column)) {  // eq. 4.2
        if (e.neighbor != neighbour) pheromone_.set(column, e.neighbor, e.value * retain_);
    }
    if (created && observer_) observer_->onRouteChanged(column, neighbour, true);
    return true;
}

std::optional<Cell> IntraZoneTable::cell(NodeAddress column, NodeAddress neighbour) const {
    const auto it = meta_.find({column, neighbour});
    if (it == meta_.end()) return std::nullopt;
    Cell c = it->second;
    c.pheromone = pheromone_.get(column, neighbour);
    return c;
}

std::size_t IntraZoneTable::removeNeighbour(NodeAddress neighbour) {
    std::erase_if(meta_, [&](const auto& kv) {
        if (kv.first.second != neighbour) return false;
        if (observer_) observer_->onRouteChanged(kv.first.first, neighbour, false);
        return true;
    });
    return pheromone_.eraseNeighbor(neighbour);
}

std::vector<NodeAddress> IntraZoneTable::neighbours() const {
    std::set<NodeAddress> rows;
    for (const auto& kv : meta_)
        rows.insert(kv.first.second);
    return {rows.begin(), rows.end()};
}

bool IntraZoneTable::hasNeighbour(NodeAddress neighbour) const {
    for (const auto& kv : meta_) {
        if (kv.first.second == neighbour) return true;
    }
    return false;
}

std::vector<NodeAddress> IntraZoneTable::zone() const {
    return pheromone_.destinations();
}

std::optional<int> IntraZoneTable::minHops(NodeAddress column) const {
    std::optional<int> best;
    for (auto it = meta_.lower_bound({column, std::numeric_limits<NodeAddress>::min()});
         it != meta_.end() && it->first.first == column; ++it) {
        if (!best || it->second.hops < *best) best = it->second.hops;
    }
    return best;
}

std::vector<NodeAddress> IntraZoneTable::peripheral(int radius) const {
    std::vector<NodeAddress> out;
    for (NodeAddress z : zone()) {
        if (minHops(z) == radius) out.push_back(z);
    }
    return out;
}

}  // namespace hopnet::core
