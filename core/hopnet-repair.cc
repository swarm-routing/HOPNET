// Copyright (c) 2026 Daniel Joppi (https://github.com/swarm-routing)
// SPDX-License-Identifier: MIT OR GPL-2.0-only
//
// [W07] §4.3's local repair, the InterZone members declared in
// hopnet-interzone.h. Readings (i)-(l): maintainer decision of 2026-11-11 on
// #5 (https://github.com/swarm-routing/HOPNET/issues/5#issuecomment-6783813122),
// as proposed in comment 6782460458. A repair at a transit relay (between two
// Path nodes) is reading (m), proposed on #5 (comment 6784087133) and accepted
// the maintainer.
#include "hopnet-interzone.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace hopnet::core {

using swarm::core::RouteAction;

namespace {

/// The position of `node` in `v`, or -1.
long indexOf(const std::vector<NodeAddress>& v, NodeAddress node) {
    const auto it = std::find(v.begin(), v.end(), node);
    return it == v.end() ? -1 : static_cast<long>(it - v.begin());
}

std::vector<NodeAddress> nodesOf(const VisitedPath& p) {
    std::vector<NodeAddress> out;
    for (const auto& h : p.hops())
        out.push_back(h.node);
    return out;
}

Outcome one(Decision d) {
    Outcome out;
    out.decisions.push_back(std::move(d));
    return out;
}

}  // namespace

Splice spliceRoute(const std::vector<NodeAddress>& prefix,
                   const std::vector<NodeAddress>& repaired) {
    // Chronological loop erasure over prefix + repaired[1..]: a node already
    // kept cuts the route back to its first visit.
    Splice out;
    auto visit = [&](NodeAddress n) {
        const long at = indexOf(out.route, n);
        if (at < 0) {
            out.route.push_back(n);
            return;
        }
        out.route.resize(static_cast<std::size_t>(at) + 1);
        out.junction = std::min(out.junction, static_cast<std::size_t>(at));
    };
    for (NodeAddress n : prefix)
        visit(n);
    out.junction = out.route.empty() ? 0 : out.route.size() - 1;  // M
    for (std::size_t i = 1; i < repaired.size(); ++i)
        visit(repaired[i]);
    return out;
}

// ---- data ----------------------------------------------------------------

std::vector<NodeAddress> InterZone::prefixHere(const DataHeader& header) const {
    const NodeAddress self = intra_.self();
    const auto& route = header.route;
    if (const long at = indexOf(route, self); at >= 0) {
        return {route.begin(), route.begin() + at + 1};
    }
    // A transit relay, between the Path node before the target and the
    // target: (m), accepted on #5 (2026-11-11).
    const long target = indexOf(route, header.target);
    if (target <= 0) return {};
    std::vector<NodeAddress> out(route.begin(), route.begin() + target);
    out.push_back(self);
    return out;
}

bool InterZone::rewrite(DataHeader& header, const InterRoute& record) const {
    // #5 (i): the packet's own prefix <S ... M> spliced onto M's record,
    // loop-erased (j), within the Path cap; its target reset.
    const NodeAddress self = intra_.self();
    if (record.path.size() < 2 || record.path.front() != self) return false;
    const std::vector<NodeAddress> prefix = prefixHere(header);
    if (prefix.empty()) return false;
    Splice s = spliceRoute(prefix, record.path);
    if (s.route.size() > config_.maxPathLength || s.junction + 1 >= s.route.size()) return false;
    header.target = s.route[s.junction] == self ? s.route[s.junction + 1] : s.route[s.junction];
    header.route = std::move(s.route);
    return true;
}

Outcome InterZone::forward(DataHeader& header) {
    const NodeAddress dest = header.destination;
    const Time now = clock_.now();
    // After a repair here, a packet still bound for the replaced segment takes
    // the repair's record (#5 (i)), which each such use refreshes.
    if (const InterRoute* r = table_.find(dest, now); r && !header.route.empty() &&
                                                      r->replaces != kInvalidAddress &&
                                                      header.target == r->replaces) {
        if (!rewrite(header, *r)) return one(Decision::drop());
        table_.use(dest, now);
    }
    Decision d = sendData(header);
    if (d.action == RouteAction::Drop && !header.route.empty() &&
        header.travelled < config_.maxHopsTravelled()) {
        // No next hop toward the target while a repair for the destination
        // runs here: buffered with it (#5 (i), one repair per (M, D)).
        if (const auto it = repairs_.find(dest); it != repairs_.end()) {
            buffer(it->second, header);
            return one(Decision::queue(dest));
        }
    }
    return one(std::move(d));
}

Outcome InterZone::resume(DataHeader& header) {
    const NodeAddress self = intra_.self();
    if (header.source == self) return routeData(header, kInvalidAddress);
    if (header.destination == kInvalidAddress || header.destination == self ||
        header.target == kInvalidAddress || header.target == self || prefixHere(header).empty()) {
        return one(Decision::drop());
    }
    return forward(header);
}

void InterZone::buffer(Repair& repair, const DataHeader& header) const {
    // Per source the newest packet (its prefix is the freshest way home); at
    // most repairBufferCapacity sources, the buffer's own size (#5 (l)).
    const auto it = repair.sources.find(header.source);
    if (it != repair.sources.end()) {
        it->second = header;
    } else if (repair.sources.size() < config_.repairBufferCapacity) {
        repair.sources.emplace(header.source, header);
    }
}

// ---- the repair ----------------------------------------------------------

Outcome InterZone::startRepair(DataHeader& header) {
    const NodeAddress self = intra_.self();
    const NodeAddress dest = header.destination;
    if (prefixHere(header).empty()) return one(Decision::drop());
    if (const auto it = repairs_.find(dest); it != repairs_.end()) {  // one per (M, D)
        buffer(it->second, header);
        return one(Decision::queue(dest));
    }
    if (repairs_.size() >= config_.interRtCapacity) return one(Decision::drop());  // org rule 7

    // The wait (fidelity, "Local-repair wait"): 5 x the lost path's estimated
    // delay. Only a record of this node's own through the lost segment has
    // one, and that record goes (#5 (i)); otherwise 1 s.
    const Time now = clock_.now();
    Time wait = config_.repairTimeout;
    if (const InterRoute* r = table_.find(dest, now);
        r && r->path.size() >= 2 && r->path[1] == header.target) {
        if (std::isfinite(r->delay) && r->delay > 0.0) wait = config_.repairWaitFactor * r->delay;
        table_.erase(dest);
    }

    // #5 (i): an ordinary external forward ant with this node as its Source
    // and its own SeqNo, handled here as at any bordercaster ([W07] p. 27:
    // the IntraRT, then the InterRT, else a bordercast).
    Ant ant{.type = AntType::ExternalForward,
            .source = self,
            .destination = dest,
            .seq = intra_.nextSeq(),
            .hops = 0,
            .time = 0.0,
            .path = VisitedPath(config_.maxPathLength)};
    Outcome out = atBordercaster(ant, kInvalidAddress);
    std::erase_if(out.decisions, [](const Decision& d) { return d.action == RouteAction::Drop; });
    if (out.decisions.empty()) {
        // No repair ant can leave (no peripheral node, p. 28): it fails at
        // once, and the packet's source gets the error ant (#5 (i), (l)).
        Outcome failed;
        Decision e = errorAnt(header);
        if (e.action == RouteAction::Unicast) failed.decisions.push_back(std::move(e));
        failed.decisions.push_back(Decision::drop());
        return failed;
    }
    Repair& repair = repairs_[dest];
    repair.deadline = now + wait;
    repair.lost = header.target;
    buffer(repair, header);
    out.decisions.push_back(Decision::queue(dest));
    out.repairFor = dest;
    out.repairWait = wait;
    return out;
}

std::vector<Decision> InterZone::repairSucceeded(const Repair& repair, const InterRoute& record,
                                                 const Ant& backward) {
    // #5 (j), (l): one notification ant per buffered source. Its Path is the
    // source's prefix spliced onto the new record, loop-erased; its SeqNo the
    // record's (the destination's counter). Past the Path cap the source gets
    // the error ant instead, so it rediscovers.
    const NodeAddress self = intra_.self();
    std::vector<Decision> out;
    for (const auto& [source, header] : repair.sources) {  // ascending: deterministic
        Splice s = spliceRoute(prefixHere(header), record.path);
        if (s.route.size() < 2 || s.route.size() > config_.maxPathLength) {
            Decision e = errorAnt(header);
            if (e.action == RouteAction::Unicast) out.push_back(std::move(e));
            continue;
        }
        Ant n{.type = AntType::Notification,
              .source = self,
              .destination = source,
              .seq = record.seq,
              .hops = 0,
              .time = backward.time,
              .path = VisitedPath(config_.maxPathLength),
              // Back over the source's own prefix: the Path node before M, or
              // the node where an erased loop through M closed.
              .target = s.route[s.junction] == self && s.junction > 0 ? s.route[s.junction - 1]
                                                                      : s.route[s.junction],
              .travelled = 0};
        for (NodeAddress node : s.route) {  // the repaired part keeps the backward ant's times
            double t = 0.0;
            for (const auto& h : backward.path.hops())
                if (h.node == node) t = h.time;
            n.path.push(node, t);
        }
        if (n.target == self) continue;  // nothing to travel (cannot happen for a transit M)
        Decision d = relay(std::move(n), false);
        if (d.action == RouteAction::Unicast) out.push_back(std::move(d));
    }
    return out;
}

Decision InterZone::errorAnt(const DataHeader& header) {
    // #5 (l): Source M, Destination S, M's next SeqNo (nothing reads it), Path
    // the packet's old route <S ... M, L ... D> (a transit M inserted where it
    // relayed, (m)); back toward S as the notification ant travels.
    const NodeAddress self = intra_.self();
    std::vector<NodeAddress> path = prefixHere(header);
    const long target = indexOf(header.route, header.target);
    if (path.size() < 2 || target < 0) return Decision::drop();
    const NodeAddress back = path[path.size() - 2];  // the Path node before M
    path.insert(path.end(), header.route.begin() + target, header.route.end());
    Ant e{.type = AntType::Error,
          .source = self,
          .destination = path.front(),
          .seq = intra_.nextSeq(),
          .hops = 0,
          .time = 0.0,
          .path = VisitedPath(config_.maxPathLength),
          .target = back,
          .travelled = 0};
    for (NodeAddress node : path)
        if (!e.path.push(node, 0.0)) return Decision::drop();
    return relay(std::move(e), false);
}

Outcome InterZone::onRepairTimeout(NodeAddress dest) {
    const auto it = repairs_.find(dest);
    if (it == repairs_.end() || clock_.now() < it->second.deadline) return {};
    // No backward ant "after a period of time" ([W07] p. 29): every buffered
    // source gets an error ant (#5 (l)), and the buffered packets go.
    Outcome out;
    for (const auto& [source, header] : it->second.sources) {
        Decision e = errorAnt(header);
        if (e.action == RouteAction::Unicast) out.decisions.push_back(std::move(e));
    }
    repairs_.erase(it);
    out.decisions.push_back(Decision::discardPending(dest));
    return out;
}

// ---- notification and error ants -----------------------------------------

bool InterZone::invalidated(const InterRoute& record, const Ant& ant) const {
    // #5 (k). M = the ant's Source. A notification invalidates a record that
    // goes through M and, after M, differs from the notification's Path after
    // M; an error ant every record through M. A record never names a transit
    // relay, so for an error ant the two Path nodes around M, consecutive in a
    // record, count as "through M" as well ((m), accepted on #5, 2026-11-11).
    const NodeAddress m = ant.source;
    const std::vector<NodeAddress> path = nodesOf(ant.path);
    const long inRecord = indexOf(record.path, m);
    const long inAnt = indexOf(path, m);
    if (ant.type == AntType::Notification) {
        if (inRecord < 0) return false;
        if (inAnt < 0) return true;
        return !std::equal(record.path.begin() + inRecord, record.path.end(), path.begin() + inAnt,
                           path.end());
    }
    if (inRecord >= 0) return true;
    if (inAnt <= 0 || static_cast<std::size_t>(inAnt) + 1 >= path.size()) return false;
    const NodeAddress before = path[static_cast<std::size_t>(inAnt) - 1];
    const NodeAddress after = path[static_cast<std::size_t>(inAnt) + 1];
    for (std::size_t i = 0; i + 1 < record.path.size(); ++i)
        if (record.path[i] == before && record.path[i + 1] == after) return true;
    return false;
}

Outcome InterZone::onRepairAnt(const Ant& ant, NodeAddress from) {
    const auto hops = ant.path.hops();
    if (ant.source == kInvalidAddress || ant.destination == kInvalidAddress ||
        ant.source == ant.destination || ant.target == kInvalidAddress || hops.size() < 2 ||
        hops.front().node != ant.destination || hops.back().node == ant.source ||
        hops.back().node == ant.destination) {
        return one(Decision::drop());
    }
    intra_.heard(from);
    const NodeAddress self = intra_.self();
    const NodeAddress dest = hops.back().node;
    const Time now = clock_.now();
    if (self == ant.destination) {
        // The source ([W07] p. 29): a notification replaces its record through
        // the SeqNo rule, so a newer generation it holds stays (#5 (k)); an
        // error ant removes a record through M, and the next packet for the
        // destination starts a discovery under the retry policy.
        Outcome out = one(Decision::none());
        if (ant.type == AntType::Notification) {
            if (table_.insert(ant.seq, nodesOf(ant.path), ant.time, now)) out.routeReady = dest;
        } else if (const InterRoute* r = table_.find(dest, now); r && invalidated(*r, ant)) {
            table_.erase(dest);
        }
        return out;
    }
    // Every node the ant passes removes its invalid routes for the
    // destination (#5 (k): transit relays included).
    if (const InterRoute* r = table_.find(dest, now); r && invalidated(*r, ant)) table_.erase(dest);
    if (self != ant.target) return one(relay(ant, false));  // a transit relay
    // A Path node: on to the one before it, toward the source.
    const long at = indexOf(nodesOf(ant.path), self);
    if (at <= 0) return one(Decision::drop());
    Ant next = ant;
    next.target = hops[static_cast<std::size_t>(at) - 1].node;
    return one(relay(std::move(next), false));
}

}  // namespace hopnet::core
