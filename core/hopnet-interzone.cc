// Copyright (c) 2026 Daniel Joppi (https://github.com/swarm-routing)
// SPDX-License-Identifier: MIT OR GPL-2.0-only
#include "hopnet-interzone.h"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <utility>

namespace hopnet::core {

using swarm::core::RouteAction;

namespace {

bool positiveFinite(double v) {
    return std::isfinite(v) && v > 0.0;
}

/// The position of `node` on `path`, or -1.
long indexOf(const VisitedPath& path, NodeAddress node) {
    const auto hops = path.hops();
    for (std::size_t i = 0; i < hops.size(); ++i) {
        if (hops[i].node == node) return static_cast<long>(i);
    }
    return -1;
}

Outcome one(Decision d) {
    Outcome out;
    out.decisions.push_back(std::move(d));
    return out;
}

}  // namespace

InterZone::InterZone(const Config& config, IntraZone& intra, const IClock& clock)
    : config_(config), intra_(intra), clock_(clock),
      table_(config.interRtCapacity, config.routeLifetime), bordercast_(config.historyCapacity) {}

// ---- data ----------------------------------------------------------------

Outcome InterZone::routeData(DataHeader& header, NodeAddress from) {
    const NodeAddress self = intra_.self();
    if (header.destination == kInvalidAddress || from == self) return one(Decision::drop());
    if (header.destination == self) return one(Decision::deliver());

    if (from == kInvalidAddress) {  // from the local transport
        if (header.source != self) return one(Decision::drop());
        header.travelled = 0;
        if (intra_.table().minHops(header.destination)) {  // inside the zone, [W07] p. 25
            header.route.clear();
            header.target = header.destination;
            return one(sendData(header));
        }
        const Time now = clock_.now();
        if (const InterRoute* r = table_.find(header.destination, now)) {  // [W07] p. 27
            header.route = r->path;
            header.target = r->path[1];
            Decision d = sendData(header);
            if (d.action == RouteAction::Unicast) table_.use(header.destination, now);  // #5
            return one(std::move(d));
        }
        Outcome out;
        out.decisions = discover(header.destination);
        out.decisions.push_back(Decision::queue(header.destination));
        return out;
    }

    if (header.target == self) {  // a Path node: on toward the next one
        const auto it = std::find(header.route.begin(), header.route.end(), self);
        if (it == header.route.end() || std::next(it) == header.route.end()) {
            return one(Decision::drop());
        }
        header.target = *std::next(it);
    }
    if (header.target == kInvalidAddress) return one(Decision::drop());
    return one(sendData(header));
}

Decision InterZone::sendData(DataHeader& header, NodeAddress exclude) {
    if (header.travelled >= config_.maxHopsTravelled()) return Decision::drop();
    const NodeAddress next = intra_.selectNextHop(header.target, exclude);
    if (next == kInvalidAddress || next == exclude) return Decision::drop();  // [W07] p. 34
    ++header.travelled;
    return {RouteAction::Unicast, next, std::nullopt};
}

Outcome InterZone::onDataSendFailure(DataHeader& header, NodeAddress nextHop) {
    intra_.onLinkFailure(nextHop);                 // #5 (e): the row goes at once
    if (header.travelled > 0) --header.travelled;  // the failed hop was not travelled
    return one(sendData(header));                  // another neighbour toward the target, or Drop
}

// ---- discovery -----------------------------------------------------------

std::vector<Decision> InterZone::discover(NodeAddress dest) {
    const NodeAddress self = intra_.self();
    if (dest == kInvalidAddress || dest == self) return {};
    const Time now = clock_.now();
    const Time retry = config_.discoveryRetryInterval;
    const auto it = discoveries_.find(dest);
    if (it != discoveries_.end() && now - it->second < retry) return {};  // fidelity, retry row
    if (it == discoveries_.end() && discoveries_.size() >= config_.interRtCapacity) {
        std::erase_if(discoveries_, [&](const auto& kv) { return now - kv.second >= retry; });
        if (discoveries_.size() >= config_.interRtCapacity) return {};  // bounded (org rule 7)
    }
    discoveries_[dest] = now;
    Ant ant{.type = AntType::ExternalForward,
            .source = self,
            .destination = dest,
            .seq = intra_.nextSeq(),
            .hops = 0,  // blank: no hop limit, [W07] p. 25
            .time = 0.0,
            .path = VisitedPath(config_.maxPathLength)};
    return bordercast(ant, kInvalidAddress);
}

std::vector<Decision> InterZone::bordercast(const Ant& ant, NodeAddress from) {
    // [W07] p. 27-28 with the query control of #5 (b): from the neighbour c
    // the ant came from (none at the source), skip a peripheral node p that is
    // on Path or whose best IntraRT neighbour is c (ties: the lowest id), and
    // never send through c.
    std::vector<Decision> out;
    const auto& table = intra_.table();
    for (NodeAddress p : table.peripheral(config_.zoneRadius)) {
        if (p == ant.source || ant.path.contains(p)) continue;
        if (from != kInvalidAddress) {
            NodeAddress best = kInvalidAddress;
            double bestValue = 0.0;
            for (const auto& e : table.pheromone().entries(p)) {  // ascending neighbours
                if (e.value > bestValue) best = e.neighbor, bestValue = e.value;
            }
            if (best == from) continue;
        }
        Ant copy = ant;
        copy.target = p;
        Decision d = relay(std::move(copy), true, from);
        if (d.action == RouteAction::Unicast) out.push_back(std::move(d));
    }
    if (out.empty()) out.push_back(Decision::drop());  // no peripheral left: the ant dies, p. 28
    return out;
}

Decision InterZone::relay(Ant ant, bool appendSelf, NodeAddress exclude) {
    if (ant.travelled >= config_.maxHopsTravelled()) return Decision::drop();
    const NodeAddress next = intra_.selectNextHop(ant.target, exclude);
    if (next == kInvalidAddress || next == exclude) return Decision::drop();      // [W07] p. 34
    if (ant.type == AntType::ExternalForward) ant.time += intra_.edgeTime(next);  // p. 26
    if (appendSelf && !ant.path.push(intra_.self(), ant.time)) return Decision::drop();
    ++ant.travelled;
    return Decision::unicast(next, std::move(ant));
}

// ---- ants ----------------------------------------------------------------

Outcome InterZone::onReceive(const Ant& ant, NodeAddress from) {
    if (from == intra_.self() || from == kInvalidAddress) return one(Decision::drop());
    switch (ant.type) {
    case AntType::ExternalForward:
        return onExternalForward(ant, from);
    case AntType::Backward:
        if (ant.target != kInvalidAddress) return onExternalBackward(ant, from);
        return one(intra_.onReceive(ant, from));
    case AntType::Notification:
    case AntType::Error:
        return one(Decision::drop());  // §4.3's local repair: the next slice
    default:
        return one(intra_.onReceive(ant, from));
    }
}

Outcome InterZone::onExternalForward(const Ant& ant, NodeAddress from) {
    if (ant.source == kInvalidAddress || ant.destination == kInvalidAddress ||
        ant.source == ant.destination || ant.target == kInvalidAddress || ant.path.empty() ||
        ant.path.hops().front().node != ant.source || !positiveFinite(ant.time)) {
        return one(Decision::drop());
    }
    intra_.heard(from);
    const NodeAddress self = intra_.self();
    if (self == ant.destination) return atDestination(ant);
    if (self != ant.target) return one(relay(ant, false));  // a transit relay
    return atBordercaster(ant, from);
}

Outcome InterZone::atBordercaster(const Ant& ant, NodeAddress from) {
    const NodeAddress self = intra_.self();
    const NodeAddress dest = ant.destination;
    const long at = indexOf(ant.path, self);
    if (at >= 0) {  // a node of a cached InterRT route the ant follows (below)
        Ant next = ant;
        next.target = static_cast<std::size_t>(at) + 1 < ant.path.size()
                          ? ant.path.hops()[static_cast<std::size_t>(at) + 1].node
                          : dest;
        return one(relay(std::move(next), false));
    }
    // Duplicates are dropped here, at the bordercasters, not at transit
    // relays (#5 (b)).
    if (!bordercast_.record(ant.source, ant.seq)) return one(Decision::drop());

    if (intra_.table().minHops(dest)) {  // the destination is in this zone, [W07] p. 27
        Ant next = ant;
        next.target = dest;
        return one(relay(std::move(next), true));
    }
    // Then the InterRT, [W07] p. 27: an unexpired route to the destination is
    // followed. Its bordercasters join Path ahead of the ant (each at the time
    // the ant leaves here, a lower bound; nothing reads external Path times),
    // unless one of them is on Path already or Path would overflow.
    if (const InterRoute* r = table_.find(dest, clock_.now())) {
        const auto& rp = r->path;  // self, ..., dest
        bool usable = rp.size() >= 2 && rp.front() == self &&
                      ant.path.size() + rp.size() - 1 <= ant.path.capacity();
        for (std::size_t i = 1; usable && i + 1 < rp.size(); ++i)
            usable = !ant.path.contains(rp[i]);
        if (usable) {
            Ant next = ant;
            next.target = rp[1];
            Decision d = relay(std::move(next), true);
            if (d.message) {
                for (std::size_t i = 1; i + 1 < rp.size(); ++i)
                    d.message->path.push(rp[i], d.message->time);
                return one(std::move(d));
            }
        }
    }
    return Outcome{bordercast(ant, from), kInvalidAddress};
}

Outcome InterZone::atDestination(const Ant& ant) {
    // #5 (c): against the highest SeqNo accepted from the ant's source;
    // strictly greater turns the ant around (type 1 -> 2), else it dies.
    if (!acceptGeneration(ant.source, ant.seq)) return one(Decision::drop());
    Ant back{.type = AntType::Backward,
             .source = intra_.self(),
             .destination = ant.source,
             .seq = intra_.nextSeq(),
             .hops = 0,
             .time = ant.time,  // T(S, D): the record's delay estimate
             .path = ant.path,
             .target = ant.path.back().node,  // the bordercaster that found us
             .travelled = 0};
    if (!back.path.push(intra_.self(), ant.time)) return one(Decision::drop());
    return one(relay(std::move(back), false));
}

Outcome InterZone::onExternalBackward(const Ant& ant, NodeAddress from) {
    const auto hops = ant.path.hops();
    if (ant.source == kInvalidAddress || ant.destination == kInvalidAddress ||
        ant.source == ant.destination || hops.size() < 2 || hops.front().node != ant.destination ||
        hops.back().node != ant.source || !positiveFinite(ant.time)) {
        return one(Decision::drop());
    }
    intra_.heard(from);
    const NodeAddress self = intra_.self();
    if (self == ant.destination) {
        // The source of the discovery stores the Path, [W07] p. 27; the
        // backward ant deposits no pheromone (#5 (d)).
        std::vector<NodeAddress> path;
        for (const auto& h : hops)
            path.push_back(h.node);
        Outcome out = one(Decision::none());
        if (table_.insert(ant.seq, std::move(path), ant.time, clock_.now())) {
            out.routeReady = ant.source;
        }
        return out;
    }
    if (self != ant.target) return one(relay(ant, false));  // a transit relay
    const long at = indexOf(ant.path, self);
    if (at <= 0) return one(Decision::drop());
    Ant next = ant;
    next.target = hops[static_cast<std::size_t>(at) - 1].node;
    return one(relay(std::move(next), false));
}

bool InterZone::acceptGeneration(NodeAddress source, std::uint32_t seq) {
    const auto it = accepted_.find(source);
    if (it != accepted_.end()) {
        if (seq <= it->second) return false;
        it->second = seq;
        return true;
    }
    if (accepted_.size() >= config_.historyCapacity && !acceptedOrder_.empty()) {
        accepted_.erase(acceptedOrder_.front());  // bounded, oldest source first (org rule 7)
        acceptedOrder_.pop_front();
    }
    accepted_[source] = seq;
    acceptedOrder_.push_back(source);
    return true;
}

std::uint32_t InterZone::acceptedSeq(NodeAddress source) const {
    const auto it = accepted_.find(source);
    return it == accepted_.end() ? 0 : it->second;
}

}  // namespace hopnet::core
