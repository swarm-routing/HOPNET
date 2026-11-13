// Copyright (c) 2026 Daniel Joppi (https://github.com/swarm-routing)
// SPDX-License-Identifier: MIT OR GPL-2.0-only
#include "hopnet-interzone.h"

#include "hopnet-observe.h"

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

/// The bordercaster a copy of an external forward ant was sent from: the Path
/// node before its target when the target is on Path (an ant following a
/// cached InterRT route, #5 (h)), else Path's last node.
NodeAddress previousBordercaster(const Ant& ant) {
    const long at = indexOf(ant.path, ant.target);
    if (at > 0) return ant.path.hops()[static_cast<std::size_t>(at) - 1].node;
    return ant.path.empty() ? kInvalidAddress : ant.path.back().node;
}

}  // namespace

InterZone::InterZone(const Config& config, IntraZone& intra, const IClock& clock,
                     swarm::core::IRouterObserver* observer)
    : config_(config), intra_(intra), clock_(clock), observer_(observer),
      table_(config.interRtCapacity, config.routeLifetime, observer),
      bordercast_(config.historyCapacity) {
    intra_.trackInterRt(&table_);
}

// ---- observation ---------------------------------------------------------

Outcome InterZone::observed(Outcome o, NodeAddress dest, bool data) {
    if (observer_) {
        for (const Decision& d : o.decisions) {
            observeSent(observer_, d);
            if (data) observeData(observer_, dest, d);
        }
    }
    intra_.reportState();
    return o;
}

Outcome InterZone::observedAnt(Outcome o, const Ant& received) {
    observeReceived(observer_, received);
    return observed(std::move(o), kInvalidAddress, false);
}

// ---- data ----------------------------------------------------------------

Outcome InterZone::routeData(DataHeader& header, NodeAddress from) {
    return observed(routeDataInner(header, from), header.destination, true);
}

Outcome InterZone::routeDataInner(DataHeader& header, NodeAddress from) {
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
        out.decisions = discoverInner(header.destination);
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
    return forward(header);
}

Decision InterZone::sendData(DataHeader& header, NodeAddress exclude) {
    if (header.travelled >= config_.maxHopsTravelled()) return Decision::drop();
    const NodeAddress next = intra_.selectNextHop(header.target, exclude);
    if (next == kInvalidAddress || next == exclude) return Decision::drop();  // [W07] p. 34
    ++header.travelled;
    return {RouteAction::Unicast, next, std::nullopt};
}

Outcome InterZone::onDataSendFailure(DataHeader& header, NodeAddress nextHop) {
    return observed(onDataSendFailureInner(header, nextHop), header.destination, true);
}

Outcome InterZone::onDataSendFailureInner(DataHeader& header, NodeAddress nextHop) {
    intra_.onLinkFailure(nextHop);                 // #5 (e): the row goes at once
    if (header.travelled > 0) --header.travelled;  // the failed hop was not travelled
    if (header.travelled >= config_.maxHopsTravelled()) return one(Decision::drop());
    Decision d = sendData(header);  // another neighbour toward the target
    if (d.action != RouteAction::Drop) return one(std::move(d));
    // No other neighbour toward the target (#5 (e)). Inside the zone the
    // IntraRT heals by itself ([W07] §4.3, first case).
    if (header.route.empty() || header.destination == kInvalidAddress) return one(d);
    if (header.source != intra_.self()) return startRepair(header);
    // At the source the repair is its own discovery, under the retry policy;
    // nobody is notified (#5 (i)). Its record through the lost link goes.
    const NodeAddress dest = header.destination;
    if (const InterRoute* r = table_.find(dest, clock_.now());
        r && r->path.size() >= 2 && r->path[1] == header.target) {
        table_.erase(dest);
    }
    Outcome out;
    out.decisions = discoverInner(dest);
    out.decisions.push_back(Decision::queue(dest));
    return out;
}

// ---- discovery -----------------------------------------------------------

std::vector<Decision> InterZone::discover(NodeAddress dest) {
    std::vector<Decision> out = discoverInner(dest);
    for (const Decision& d : out)
        observeSent(observer_, d);
    return out;
}

std::vector<Decision> InterZone::discoverInner(NodeAddress dest) {
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
    detect(ant, self);  // the source's own query (#5 (q))
    // With no peripheral node to send to, bordercast() says Drop: the fate of
    // a received ant. Nothing was received here, so the caller gets no
    // decision, and a data packet waiting for the route is not mistaken for it.
    std::vector<Decision> out = bordercast(ant, kInvalidAddress);
    std::erase_if(out, [](const Decision& d) { return d.action == RouteAction::Drop; });
    return out;
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
    // The intra-zone ants are reported by IntraZone::onReceive itself.
    switch (ant.type) {
    case AntType::ExternalForward:
        return observedAnt(onExternalForward(ant, from), ant);
    case AntType::Backward:
        if (ant.target != kInvalidAddress) return observedAnt(onExternalBackward(ant, from), ant);
        return one(intra_.onReceive(ant, from));
    case AntType::Notification:
    case AntType::Error:
        return observedAnt(onRepairAnt(ant, from), ant);
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
    if (self != ant.target) {  // a transit relay
        // Early termination (#5 (q)): a query this node already carried for
        // another bordercaster ends here; one bordercast's branches go on.
        if (!detect(ant, previousBordercaster(ant))) return one(Decision::drop());
        return one(relay(ant, false));
    }
    return atBordercaster(ant, from);
}

Outcome InterZone::atBordercaster(const Ant& ant, NodeAddress from) {
    const NodeAddress self = intra_.self();
    const NodeAddress dest = ant.destination;
    const long at = indexOf(ant.path, self);
    // Query detection (#5 (q)): at the origin of a query (a repair ant) this
    // node is its bordercaster; a node that detected the query from another
    // bordercaster is covered and does not bordercast it (below).
    const bool covered = !detect(ant, from == kInvalidAddress ? self : previousBordercaster(ant));
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
    if (covered) return one(Decision::drop());  // its zone is covered (#5 (q))
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
        // A backward ant from a destination this node is repairing toward ends
        // the repair (#5 (i)): the record remembers the route it replaces.
        std::vector<NodeAddress> path;
        for (const auto& h : hops)
            path.push_back(h.node);
        const auto rep = repairs_.find(ant.source);
        const NodeAddress replaces = rep != repairs_.end() ? rep->second.lost : kInvalidAddress;
        Outcome out = one(Decision::none());
        const Time now = clock_.now();
        if (table_.insert(ant.seq, std::move(path), ant.time, now, replaces)) {
            out.routeReady = ant.source;
            if (rep != repairs_.end()) {
                std::vector<Decision> ants =
                    repairSucceeded(rep->second, *table_.find(ant.source, now), ant);
                repairs_.erase(rep);
                if (!ants.empty()) out.decisions = std::move(ants);
            }
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

bool InterZone::detect(const Ant& ant, NodeAddress prev) {
    if (!config_.queryDetection) return true;
    const std::pair<NodeAddress, std::uint32_t> key{ant.source, ant.seq};
    if (const auto it = detected_.find(key); it != detected_.end()) return it->second == prev;
    if (detected_.size() >= config_.historyCapacity && !detectedOrder_.empty()) {
        detected_.erase(detectedOrder_.front());  // bounded, oldest query first (org rule 7)
        detectedOrder_.pop_front();
    }
    if (config_.historyCapacity == 0) return true;
    detected_.emplace(key, prev);
    detectedOrder_.push_back(key);
    return true;
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
