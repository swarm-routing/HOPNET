// Copyright (c) 2026 Daniel Joppi (https://github.com/swarm-routing)
// SPDX-License-Identifier: MIT OR GPL-2.0-only
//
// The network the inter-zone and local-repair tests run on: a lossless,
// instantaneous radio over a fixed graph, every node an IntraZone + InterZone
// pair, the adapter's queue and repair timers included; the Fig. 4.1 fixture;
// and helpers that build ants by hand. A unicast ant is also overheard by the
// sender's other neighbours (#5 (s)), and a query-processing delay (#5 (t))
// ends when the air is quiet, shortest wait first, with the clock unmoved.
#ifndef HOPNET_CORE_TEST_NET_H
#define HOPNET_CORE_TEST_NET_H

#include "../hopnet-interzone.h"
#include "hopnet-check.h"

#include <algorithm>
#include <deque>
#include <initializer_list>
#include <map>
#include <memory>
#include <set>
#include <utility>
#include <vector>

namespace hopnet::core::test {
using swarm::core::RouteAction;

/// A lossless, instantaneous radio over a fixed graph carrying ants and data,
/// every node an IntraZone + InterZone pair. Frames are delivered in FIFO
/// order, so the copy that took fewer hops arrives first.
class Net {
public:
    /// `observer` (optional) is handed to every node's two halves.
    explicit Net(Config cfg = {}, std::uint64_t seed = 1,
                 swarm::core::IRouterObserver* observer = nullptr)
        : cfg_(cfg), rng_(seed), observer_(observer) {}

    void link(NodeAddress a, NodeAddress b) {
        adj[a].insert(b);
        adj[b].insert(a);
        node(a);
        node(b);
    }
    /// Removes a link from the radio only: the nodes notice by themselves.
    void cut(NodeAddress a, NodeAddress b) {
        adj[a].erase(b);
        adj[b].erase(a);
    }
    IntraZone& intra(NodeAddress n) { return node(n).intra; }
    InterZone& inter(NodeAddress n) { return node(n).inter; }
    FakeClock& clock() { return clock_; }

    /// One period of internal ants, every node in ascending order; the logs
    /// below start empty after it.
    void period() {
        for (const auto& kv : adj) {
            emit(kv.first, {{intra(kv.first).onInternalAntTimer()}, kInvalidAddress}, nullptr);
            run();
        }
        ants.clear();
        drops.clear();
    }
    /// A data packet from the transport at `src`; the network then runs to quiet.
    Outcome send(NodeAddress src, NodeAddress dst) {
        DataHeader h{.source = src, .destination = dst};
        const Outcome o = inter(src).routeData(h, kInvalidAddress);
        emit(src, o, &h);
        run();
        return o;
    }
    /// One packet per (source, destination) pair, all on the air before the
    /// network runs: they travel side by side, in FIFO order.
    void sendAll(std::initializer_list<std::pair<NodeAddress, NodeAddress>> flows) {
        for (auto [src, dst] : flows) {
            DataHeader h{.source = src, .destination = dst};
            emit(src, inter(src).routeData(h, kInvalidAddress), &h);
        }
        run();
    }
    /// The clock moves to `t`; the repair timers due by then fire, in the
    /// order they were set, and the network runs to quiet after each.
    void advance(Time t) {
        clock_.set(t);
        while (true) {
            const auto due = std::find_if(timers_.begin(), timers_.end(),
                                          [t](const Timer& x) { return x.at <= t; });
            if (due == timers_.end()) break;
            const Timer fire = *due;
            timers_.erase(due);
            emit(fire.node, inter(fire.node).onRepairTimeout(fire.dest), nullptr);
            run();
        }
    }
    /// Delivers `ant` to `to` as if `from` had sent it, then runs to quiet.
    Outcome inject(NodeAddress to, const Ant& ant, NodeAddress from) {
        const Outcome o = inter(to).onReceive(ant, from);
        emit(to, o, nullptr);
        run();
        return o;
    }

    std::map<NodeAddress, std::set<NodeAddress>> adj;
    /// Every ant unicast: (from, to, ant).
    struct Sent {
        NodeAddress from, to;
        Ant ant;
    };
    std::vector<Sent> ants;
    std::vector<std::pair<NodeAddress, DataHeader>> delivered;
    std::vector<std::pair<NodeAddress, NodeAddress>> dataHops;  // (from, to)
    std::map<NodeAddress, int> drops;                           // per node, ants and data
    std::map<NodeAddress, std::vector<DataHeader>> queued;      // per node
    std::map<NodeAddress, int> discarded;                       // per node, DiscardPending
    /// Every query-processing delay drawn: (node, wait).
    std::vector<std::pair<NodeAddress, Time>> queryWaits;
    /// The ants of `type` sent, in order.
    std::vector<Sent> antsOf(AntType type) const {
        std::vector<Sent> out;
        for (const auto& s : ants)
            if (s.ant.type == type) out.push_back(s);
        return out;
    }

private:
    struct Node {
        Node(const Config& cfg, NodeAddress self, FakeClock& clock, FakeLinkState& link, IRng& rng,
             swarm::core::IRouterObserver* observer)
            : intra(cfg, self, clock, link, rng, observer), inter(cfg, intra, clock, observer) {}
        IntraZone intra;
        InterZone inter;
    };
    struct Timer {
        Time at;
        NodeAddress node, dest;
    };
    struct QueryTimer {
        Time wait;
        NodeAddress node, source;
        std::uint32_t seq;
    };
    struct Frame {
        NodeAddress from, to;
        bool isData;
        Ant ant;
        DataHeader data;
    };
    Node& node(NodeAddress n) {
        auto it = nodes_.find(n);
        if (it == nodes_.end())
            it = nodes_
                     .emplace(n, std::make_unique<Node>(cfg_, n, clock_, link_[n], rng_, observer_))
                     .first;
        return *it->second;
    }
    void emit(NodeAddress at, const Outcome& o, const DataHeader* h) {
        for (const Decision& d : o.decisions) {
            switch (d.action) {
            case RouteAction::Broadcast:
                for (NodeAddress to : adj[at])
                    air_.push_back({at, to, false, *d.message, {}});
                break;
            case RouteAction::Unicast:
                if (d.message) {
                    ants.push_back({at, d.nextHop, *d.message});
                    if (adj[at].count(d.nextHop))
                        air_.push_back({at, d.nextHop, false, *d.message, {}});
                } else if (h) {
                    dataHops.emplace_back(at, d.nextHop);
                    if (adj[at].count(d.nextHop)) {
                        air_.push_back({at, d.nextHop, true, {}, *h});
                    } else {  // the MAC reports the failure
                        DataHeader again = *h;
                        emit(at, inter(at).onDataSendFailure(again, d.nextHop), &again);
                    }
                }
                break;
            case RouteAction::Deliver:
                if (h) delivered.emplace_back(at, *h);
                break;
            case RouteAction::Queue:
                if (h) queued[at].push_back(*h);
                break;
            case RouteAction::Drop:
                ++drops[at];
                break;
            case RouteAction::DiscardPending:
                discarded[at] += static_cast<int>(std::erase_if(
                    queued[at], [&](const DataHeader& q) { return q.destination == d.nextHop; }));
                break;
            default:
                break;
            }
        }
        if (o.repairFor != kInvalidAddress)
            timers_.push_back({clock_.now() + o.repairWait, at, o.repairFor});
        if (o.querySource != kInvalidAddress) {
            queryWaits.emplace_back(at, o.queryWait);
            queryTimers_.push_back({o.queryWait, at, o.querySource, o.querySeq});
        }
        if (o.routeReady != kInvalidAddress) {  // re-offer what waits for it
            std::vector<DataHeader> waiting;
            std::erase_if(queued[at], [&](const DataHeader& q) {
                if (q.destination != o.routeReady) return false;
                waiting.push_back(q);
                return true;
            });
            for (DataHeader& q : waiting)
                emit(at, inter(at).resume(q), &q);
        }
    }
    void run() {
        while (true) {
            while (!air_.empty()) {
                Frame f = air_.front();
                air_.pop_front();
                if (f.isData) {
                    emit(f.to, inter(f.to).routeData(f.data, f.from), &f.data);
                } else {
                    if (f.ant.type == AntType::ExternalForward)  // unicast only
                        for (NodeAddress n : adj[f.from])        // the sender's other neighbours
                            if (n != f.to) inter(n).overhear(f.ant);
                    emit(f.to, inter(f.to).onReceive(f.ant, f.from), nullptr);
                }
            }
            if (queryTimers_.empty()) break;
            const auto next = std::min_element(
                queryTimers_.begin(), queryTimers_.end(),
                [](const QueryTimer& a, const QueryTimer& b) { return a.wait < b.wait; });
            const QueryTimer fire = *next;
            queryTimers_.erase(next);
            emit(fire.node, inter(fire.node).onQueryDelay(fire.source, fire.seq), nullptr);
        }
    }

    Config cfg_;
    FakeClock clock_;
    SeededRng rng_;
    swarm::core::IRouterObserver* observer_;
    std::map<NodeAddress, FakeLinkState> link_;
    std::map<NodeAddress, std::unique_ptr<Node>> nodes_;
    std::deque<Frame> air_;
    std::vector<Timer> timers_;
    std::vector<QueryTimer> queryTimers_;
};

// [W07] Fig. 4.1 as the pre-registration reads it (#5 (b), (d)), radius 2:
// S reaches A and D through C, F and G through E; D's peripheral nodes are S
// and A (through C), K (through J and L) and M (through I); M's are D, U and
// R; U and R both have T in their zones, U one hop nearer.
enum : NodeAddress { S = 1, C, A, D, E, F, G, I, M, J, K, L, O, U, P, R, V, T };

inline Net fig41(std::uint64_t seed = 1, Config cfg = {},
                 swarm::core::IRouterObserver* observer = nullptr) {
    Net net(cfg, seed, observer);
    for (auto [a, b] : std::vector<std::pair<NodeAddress, NodeAddress>>{{S, C},
                                                                        {C, A},
                                                                        {C, D},
                                                                        {S, E},
                                                                        {E, F},
                                                                        {E, G},
                                                                        {D, I},
                                                                        {I, M},
                                                                        {D, J},
                                                                        {J, K},
                                                                        {D, L},
                                                                        {L, K},
                                                                        {M, O},
                                                                        {O, U},
                                                                        {M, P},
                                                                        {P, R},
                                                                        {R, V},
                                                                        {V, T},
                                                                        {U, T}})
        net.link(a, b);
    net.period();
    return net;
}

inline std::set<NodeAddress> targetsSentBy(const Net& net, NodeAddress n, std::size_t from = 0) {
    std::set<NodeAddress> out;
    for (std::size_t i = from; i < net.ants.size(); ++i) {
        const auto& s = net.ants[i];
        if (s.from == n && s.ant.type == AntType::ExternalForward && s.ant.path.back().node == n)
            out.insert(s.ant.target);
    }
    return out;
}

inline std::vector<NodeAddress> nodesOf(const VisitedPath& p) {
    std::vector<NodeAddress> out;
    for (const auto& h : p.hops())
        out.push_back(h.node);
    return out;
}

/// Gives `z` a one-hop route to each of `nbrs` (cell (n, n), hops 1).
inline void neighbours(IntraZone& z, std::initializer_list<NodeAddress> nbrs,
                       std::uint32_t seq = 1) {
    for (NodeAddress n : nbrs) {
        Ant a{.type = AntType::InternalForward,
              .source = n,
              .seq = seq,
              .hops = 1,
              .time = 0.01,
              .path = VisitedPath(100)};
        a.path.push(n, 0.01);
        z.onReceive(a, n);
    }
}

/// An external forward ant for `dest` that `last` bordercast toward `target`.
inline Ant externalAnt(NodeAddress src, NodeAddress dest, std::uint32_t seq,
                       std::initializer_list<NodeAddress> path, NodeAddress target) {
    Ant a{.type = AntType::ExternalForward,
          .source = src,
          .destination = dest,
          .seq = seq,
          .time = 0.01,
          .path = VisitedPath(100),
          .target = target,
          .travelled = 2};
    double t = 0.0;
    for (NodeAddress n : path)
        a.path.push(n, t += 0.002);
    return a;
}

}  // namespace hopnet::core::test

#endif  // HOPNET_CORE_TEST_NET_H
