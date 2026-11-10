// Copyright (c) 2026 Daniel Joppi (https://github.com/swarm-routing)
// SPDX-License-Identifier: MIT OR GPL-2.0-only
#include "hopnet-intrazone.h"

#include "swarm-core/swarm-selection.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace hopnet::core {

namespace {

bool positiveFinite(double v) {
    return std::isfinite(v) && v > 0.0;
}

}  // namespace

IntraZone::IntraZone(const Config& config, NodeAddress self, const IClock& clock,
                     const ILinkState& link, IRng& rng)
    : config_(config), self_(self), clock_(clock), link_(link), rng_(rng),
      table_(config.intraRtCapacity, config.evaporation), forwarded_(config.historyCapacity) {}

Decision IntraZone::onInternalAntTimer() {
    purgeStaleNeighbours();
    if (config_.zoneRadius < 1) return Decision::none();
    Ant ant{.type = AntType::InternalForward,
            .source = self_,
            .destination = kInvalidAddress,  // blank, [W07] p. 25
            .seq = ++seq_,
            .hops = static_cast<std::uint16_t>(
                std::min(config_.zoneRadius, int{std::numeric_limits<std::uint16_t>::max()})),
            .time = edgeTime(kInvalidAddress),
            .path = VisitedPath(config_.maxPathLength)};
    ant.path.push(self_, ant.time);
    return Decision::broadcast(ant);
}

Decision IntraZone::onReceive(const Ant& ant, NodeAddress from) {
    if (from == self_ || from == kInvalidAddress) return Decision::drop();
    switch (ant.type) {
    case AntType::InternalForward:
        return onInternalForward(ant, from);
    case AntType::Backward:
        return onInternalBackward(ant, from);
    default:
        return Decision::drop();  // the inter-zone half, a later slice
    }
}

Decision IntraZone::onInternalForward(const Ant& ant, NodeAddress from) {
    // A copy that already passed here (the source included) is a loop, and a
    // malformed ant would deposit nonsense: both are dropped untouched.
    if (ant.source == kInvalidAddress || ant.path.empty() || ant.path.back().node != from ||
        ant.path.hops().front().node != ant.source || ant.path.contains(self_) || ant.hops < 1 ||
        !positiveFinite(ant.time)) {
        return Decision::drop();
    }
    // Eq. 4.1 at v_j = self from v_i = from: cell (from, S) gains
    // eps / (T(S, v_i) + w(v_i, v_j)), the Time the ant carries.
    table_.update(ant.source, from, config_.epsilon / ant.time, static_cast<int>(ant.path.size()),
                  ant.seq);
    heard(from);
    if (!forwarded_.record(ant.source, ant.seq)) return Decision::none();  // a later copy

    if (ant.hops > 1) {  // the zone-limited flood (#5 (f))
        Ant next = ant;
        next.hops = static_cast<std::uint16_t>(ant.hops - 1);
        next.time = ant.time + edgeTime(kInvalidAddress);
        if (!next.path.push(self_, next.time)) return Decision::drop();  // Path cap
        return Decision::broadcast(next);
    }
    // The hop budget ends here: the first copy becomes the internal backward
    // ant, back along Path to its source ([W07] p. 26).
    Ant back{.type = AntType::Backward,
             .source = self_,
             .destination = ant.source,
             .seq = ++seq_,
             .hops = 1,
             .time = ant.time,  // T(S, D)
             .path = ant.path};
    return Decision::unicast(from, back);
}

Decision IntraZone::onInternalBackward(const Ant& ant, NodeAddress from) {
    if (ant.source == kInvalidAddress || ant.source == self_ || ant.path.empty() ||
        ant.path.back().node != self_ || ant.hops < 1 || !positiveFinite(ant.time)) {
        return Decision::drop();
    }
    // Eq. 4.3 with T' = T(S, D) - T(S, v_k) (#5 (a)): the time from here to
    // D through `from`. T(S, v_k) is when the forward ant left the node before
    // this one on Path (0 at the source).
    const auto hops = ant.path.hops();
    const double arrival = hops.size() >= 2 ? hops[hops.size() - 2].time : 0.0;
    const double tPrime = ant.time - arrival;
    if (!positiveFinite(tPrime)) return Decision::drop();
    table_.update(ant.source, from, config_.epsilon / tPrime, ant.hops, ant.seq);
    heard(from);

    if (hops.size() == 1) {
        return ant.destination == self_ ? Decision::none() : Decision::drop();
    }
    Ant next = ant;
    next.path.pop();
    next.hops = static_cast<std::uint16_t>(ant.hops + 1);
    const NodeAddress to = next.path.back().node;
    return Decision::unicast(to, next);
}

void IntraZone::heard(NodeAddress neighbour) {
    if (table_.hasNeighbour(neighbour)) lastHeard_[neighbour] = clock_.now();
}

void IntraZone::onLinkFailure(NodeAddress neighbour) {
    table_.removeNeighbour(neighbour);
    lastHeard_.erase(neighbour);
}

void IntraZone::purgeStaleNeighbours() {
    const Time now = clock_.now();
    for (auto it = lastHeard_.begin(); it != lastHeard_.end();) {
        if (now - it->second >= config_.neighbourTimeout()) {
            table_.removeNeighbour(it->first);
            it = lastHeard_.erase(it);
        } else {
            ++it;
        }
    }
}

NodeAddress IntraZone::selectNextHop(NodeAddress dest, NodeAddress exclude) {
    return swarm::core::selectNextHop(table_.pheromone(), dest, config_.beta, rng_, exclude);
}

double IntraZone::edgeTime(NodeAddress nextHop) const {
    const double service = link_.macServiceTime(nextHop);
    if (!positiveFinite(service)) return config_.defaultHopTime;
    return (std::max(link_.macQueueLength(nextHop), 0) + 1) * service;
}

}  // namespace hopnet::core
