// Copyright (c) 2026 Daniel Joppi (https://github.com/swarm-routing)
// SPDX-License-Identifier: MIT OR GPL-2.0-only
//
// The inter-zone half against the readings pre-registered on #5 and accepted
// on 2026-11-10: the bordercast and its query control on the Fig. 4.1
// topology (b), the destination's generation check (c), the external backward
// ant and the InterRT (d), the InterRT expiry and the discovery retry policy,
// a failed data transmission (e); plus the data header codec, the bounds and
// determinism.
#include "../hopnet-interzone.h"
#include "hopnet-check.h"

#include <deque>
#include <map>
#include <memory>
#include <set>

namespace hopnet::core::test {

namespace {

using swarm::core::RouteAction;

/// A lossless, instantaneous radio over a fixed graph carrying ants and data,
/// every node an IntraZone + InterZone pair. Frames are delivered in FIFO
/// order, so the copy that took fewer hops arrives first.
class Net {
public:
    explicit Net(Config cfg = {}, std::uint64_t seed = 1) : cfg_(cfg), rng_(seed) {}

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

private:
    struct Node {
        Node(const Config& cfg, NodeAddress self, FakeClock& clock, FakeLinkState& link, IRng& rng)
            : intra(cfg, self, clock, link, rng), inter(cfg, intra, clock) {}
        IntraZone intra;
        InterZone inter;
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
            it = nodes_.emplace(n, std::make_unique<Node>(cfg_, n, clock_, link_[n], rng_)).first;
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
            default:
                break;
            }
        }
        if (o.routeReady != kInvalidAddress) {  // re-offer what waits for it
            std::vector<DataHeader> waiting;
            std::erase_if(queued[at], [&](const DataHeader& q) {
                if (q.destination != o.routeReady) return false;
                waiting.push_back(q);
                return true;
            });
            for (DataHeader& q : waiting)
                emit(at, inter(at).routeData(q, kInvalidAddress), &q);
        }
    }
    void run() {
        while (!air_.empty()) {
            Frame f = air_.front();
            air_.pop_front();
            if (f.isData) {
                emit(f.to, inter(f.to).routeData(f.data, f.from), &f.data);
            } else {
                emit(f.to, inter(f.to).onReceive(f.ant, f.from), nullptr);
            }
        }
    }

    Config cfg_;
    FakeClock clock_;
    SeededRng rng_;
    std::map<NodeAddress, FakeLinkState> link_;
    std::map<NodeAddress, std::unique_ptr<Node>> nodes_;
    std::deque<Frame> air_;
};

// [W07] Fig. 4.1 as the pre-registration reads it (#5 (b), (d)), radius 2:
// S reaches A and D through C, F and G through E; D's peripheral nodes are S
// and A (through C), K (through J and L) and M (through I); M's are D, U and
// R; U and R both have T in their zones, U one hop nearer.
enum : NodeAddress { S = 1, C, A, D, E, F, G, I, M, J, K, L, O, U, P, R, V, T };

Net fig41(std::uint64_t seed = 1) {
    Net net(Config{}, seed);
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

std::set<NodeAddress> targetsSentBy(const Net& net, NodeAddress n, std::size_t from = 0) {
    std::set<NodeAddress> out;
    for (std::size_t i = from; i < net.ants.size(); ++i) {
        const auto& s = net.ants[i];
        if (s.from == n && s.ant.type == AntType::ExternalForward && s.ant.path.back().node == n)
            out.insert(s.ant.target);
    }
    return out;
}

std::vector<NodeAddress> nodesOf(const VisitedPath& p) {
    std::vector<NodeAddress> out;
    for (const auto& h : p.hops())
        out.push_back(h.node);
    return out;
}

/// Gives `z` a one-hop route to each of `nbrs` (cell (n, n), hops 1).
void neighbours(IntraZone& z, std::initializer_list<NodeAddress> nbrs, std::uint32_t seq = 1) {
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
Ant externalAnt(NodeAddress src, NodeAddress dest, std::uint32_t seq,
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

}  // namespace

void TestBordercastFig41(Checker& c) {
    // #5 (b) pinned on Fig. 4.1.
    Net net = fig41();
    c.check(net.intra(S).table().peripheral(2) == std::vector<NodeAddress>{A, D, F, G},
            "S's peripheral nodes are A, D, F and G");
    net.send(S, T);
    c.check(targetsSentBy(net, S) == std::set<NodeAddress>{A, D, F, G},
            "S bordercasts to every peripheral node");
    c.check(targetsSentBy(net, D) == std::set<NodeAddress>{K, M},
            "D (ant from C, Path <S>) skips S and A, sends to K and M");
    for (const auto& s : net.ants) {
        if (s.from == D && s.ant.type == AntType::ExternalForward)
            c.check(s.to != C, "no copy from D goes back through C");
    }
    c.check(targetsSentBy(net, M) == std::set<NodeAddress>{U, R}, "M skips D (on Path)");
    c.check(targetsSentBy(net, U) == std::set<NodeAddress>{T} &&
                targetsSentBy(net, R) == std::set<NodeAddress>{T},
            "U and R find T in their zones and send the ant to it ([W07] p. 27)");
    for (NodeAddress dead : {A, F, G, K}) {
        c.check(targetsSentBy(net, dead).empty() && net.drops[dead] == 1,
                "no peripheral left: the ant dies ([W07] p. 28)");
    }
    const std::size_t before = net.ants.size();
    const Outcome again = net.inject(D, externalAnt(S, T, net.intra(S).lastSeq(), {S}, D), C);
    c.check(again.decisions.size() == 1 && again.decisions[0].action == RouteAction::Drop &&
                net.ants.size() == before,
            "a second copy of the same (S, seq) is dropped at the bordercaster");
}

void TestBordercastSkipByBestNeighbour(Checker& c) {
    // #5 (b): from c, a peripheral node whose best IntraRT neighbour is c is
    // skipped even when another neighbour also reaches it; otherwise it is
    // kept and reached through that other neighbour.
    FakeClock clock;
    FakeLinkState link;
    SeededRng rng(1);
    IntraZone intra(Config{}, D, clock, link, rng);
    InterZone z(Config{}, intra, clock);
    neighbours(intra, {C, J});
    // Cell (via, p), hops 2: p's internal ant relayed by `via`.
    auto peripheralVia = [&](NodeAddress p, NodeAddress via, double time) {
        Ant a{.type = AntType::InternalForward,
              .source = p,
              .seq = 1,
              .hops = 1,
              .time = time,
              .path = VisitedPath(100)};
        a.path.push(p, time / 2);
        a.path.push(via, time);
        intra.onReceive(a, via);
    };
    peripheralVia(A, C, 0.004);  // A: best via C
    peripheralVia(A, J, 0.008);
    peripheralVia(K, J, 0.004);  // K: best via J
    peripheralVia(K, C, 0.008);
    c.check(intra.table().peripheral(2) == std::vector<NodeAddress>{A, K}, "two peripheral nodes");
    const Outcome o = z.onReceive(externalAnt(S, T, 4, {S}, D), C);
    c.check(o.decisions.size() == 1 && o.decisions[0].action == RouteAction::Unicast &&
                o.decisions[0].message->target == K && o.decisions[0].nextHop == J,
            "A (best via C) is skipped; K is kept and reached through J");
    std::set<NodeAddress> targets;
    for (const auto& d : z.discover(T))
        if (d.message) targets.insert(d.message->target);
    c.check(targets == std::set<NodeAddress>{A, K}, "at the source nothing is skipped");
    const Outcome onPath = z.onReceive(externalAnt(K, T, 5, {K}, D), C);
    c.check(onPath.decisions.size() == 1 && onPath.decisions[0].action == RouteAction::Drop,
            "K is skipped when it is on Path, though its best neighbour is J");
}

void TestDiscoveryFig41(Checker& c) {
    // #5 (c), (d) pinned on Fig. 4.1.
    Net net = fig41();
    const Outcome first = net.send(S, T);
    c.check(first.decisions.back().action == RouteAction::Queue &&
                first.decisions.back().nextHop == T,
            "with no route the packet waits while the discovery runs");
    int backward = 0;
    for (const auto& s : net.ants)
        backward += s.from == T && s.ant.type == AntType::Backward;
    c.check(backward == 1, "both U's and R's copies reach T: one backward ant");
    const InterRoute* r = net.inter(S).table().find(T, 0.0);
    c.check(r && r->path == std::vector<NodeAddress>{S, D, M, U, T},
            "S stores Path <S, D, M, U, T>, the bordercasters only");
    c.check(net.inter(S).table().size() == 1, "one record");
    for (NodeAddress n : {C, D, M, U, T})
        c.check(net.inter(n).table().size() == 0, "only the source stores the route");
    c.check(net.delivered.size() == 1 && net.delivered[0].first == T &&
                net.delivered[0].second.route == std::vector<NodeAddress>{S, D, M, U, T},
            "the waiting packet goes out on routeReady and arrives as a loose source route");
    // External backward ants deposit nothing (#5 (d)): a column for T only
    // where T is within the radius.
    for (const auto& [v, nbrs] : net.adj) {
        const bool near = v == T || net.adj[T].count(v) ||
                          std::any_of(net.adj[T].begin(), net.adj[T].end(),
                                      [&](NodeAddress x) { return net.adj[x].count(v) > 0; });
        c.check(net.intra(v).table().minHops(T).has_value() == (near && v != T),
                "an IntraRT column for T only inside T's radius");
    }
    // A second packet uses the record at once.
    const std::size_t antsBefore = net.ants.size();
    const Outcome second = net.send(S, T);
    c.check(second.decisions.size() == 1 && second.decisions[0].action == RouteAction::Unicast &&
                second.decisions[0].nextHop == C && net.ants.size() == antsBefore,
            "an unexpired InterRT path is used at once ([W07] p. 27)");
    c.check(net.delivered.size() == 2, "and delivered");
}

void TestIntraZoneData(Checker& c) {
    Net net = fig41();
    const Outcome o = net.send(S, D);
    c.check(o.decisions.size() == 1 && o.decisions[0].action == RouteAction::Unicast &&
                o.decisions[0].nextHop == C,
            "a destination in the zone goes by the IntraRT, no ant");
    c.check(net.ants.empty() && net.delivered.size() == 1 && net.delivered[0].second.route.empty(),
            "delivered, no route in the header");
    DataHeader self{.source = S, .destination = S};
    c.check(net.inter(S).routeData(self, kInvalidAddress).decisions[0].action ==
                RouteAction::Deliver,
            "to itself: delivered");
    DataHeader forged{.source = T, .destination = D};
    c.check(net.inter(S).routeData(forged, kInvalidAddress).decisions[0].action ==
                RouteAction::Drop,
            "the transport only sends this node's packets");
}

void TestDestinationGenerations(Checker& c) {
    // #5 (c) pinned: the destination compares with the highest SeqNo accepted
    // from the ant's source.
    FakeClock clock;
    FakeLinkState link;
    SeededRng rng(1);
    IntraZone intra(Config{}, T, clock, link, rng);
    InterZone z(Config{}, intra, clock);
    neighbours(intra, {U, R, V});
    auto turned = [&](NodeAddress src, std::uint32_t seq, NodeAddress via) {
        const Outcome o = z.onReceive(externalAnt(src, T, seq, {src, D, M, via}, T), via);
        return o.decisions.size() == 1 && o.decisions[0].action == RouteAction::Unicast &&
               o.decisions[0].message && o.decisions[0].message->type == AntType::Backward;
    };
    c.check(turned(S, 7, U), "(S, 7) via U turns around");
    c.check(!turned(S, 7, R), "(S, 7) via R dies: one backward ant per generation");
    c.check(turned(S, 9, U), "a later (S, 9) is accepted");
    c.check(!turned(S, 8, U), "a late (S, 8) dies");
    c.check(turned(A, 1, U), "another source's seq 1 is accepted independently");
    c.check(z.acceptedSeq(S) == 9 && z.acceptedSeq(A) == 1, "per source");
    const Outcome o = z.onReceive(externalAnt(F, T, 3, {F, D, M, U}, T), U);
    const Ant& back = *o.decisions[0].message;
    c.check(back.source == T && back.destination == F && back.target == U &&
                nodesOf(back.path) == std::vector<NodeAddress>{F, D, M, U, T} &&
                back.seq == intra.lastSeq(),
            "the backward ant: from T to the source, Path + T, toward the last bordercaster, "
            "a fresh SeqNo of T's");
    c.check(z.table().size() == 0, "the destination stores no route back (#5 (c))");

    Config cfg;
    cfg.historyCapacity = 2;
    InterZone small(cfg, intra, clock);
    for (NodeAddress src : {1, 2, 3})
        small.onReceive(externalAnt(src, T, 5, {src, U}, T), U);
    c.check(small.acceptedSeq(1) == 0 && small.acceptedSeq(3) == 5,
            "the accepted generations are bounded, oldest source first (org rule 7)");
}

void TestInterRtTable(Checker& c) {
    // #5 (d) and the InterRT Expire row pinned.
    InterZoneTable t(2, 3.0);
    c.check(t.insert(5, {S, D, T}, 0.02, 0.0), "a first record");
    c.check(!t.insert(4, {S, M, T}, 0.02, 0.5), "an older generation is ignored");
    c.check(!t.insert(5, {S, M, T}, 0.02, 0.5), "the same generation too");
    c.check(t.insert(6, {S, M, T}, 0.03, 0.5) && t.find(T, 0.5)->path.back() == T &&
                t.find(T, 0.5)->path[1] == M,
            "a newer generation replaces it: one Path per destination");
    c.check(t.find(T, 3.4) && !t.find(T, 3.5), "expires 3 s after it was stored");
    c.check(t.insert(1, {S, D, T}, 0.02, 3.5), "an expired record is replaced by any generation");
    c.check(t.use(T, 6.4) && t.find(T, 9.3) && !t.find(T, 9.4),
            "a use at 6.4 s extends it to 9.4 s");
    c.check(!t.use(T, 9.4), "an expired record cannot be used");
    c.check(t.insert(1, {S, A}, 0.01, 9.4) && t.insert(1, {S, G}, 0.01, 9.4),
            "the expired record makes room");
    c.check(t.size() == 2 && !t.insert(1, {S, F}, 0.01, 9.4),
            "a table full of live records refuses a new destination (org rule 7)");
    c.check(t.erase(A) && t.insert(1, {S, F}, 0.01, 9.4), "erase makes room");
    c.check(!t.insert(1, {S}, 0.0, 9.4), "a Path needs a source and a destination");
    c.check(t.purge(100.0) == 2 && t.size() == 0, "purge drops the expired");
}

void TestInterRtExpiry(Checker& c) {
    // The InterRT Expire row pinned: 3 s after the last use.
    Net net = fig41();
    net.send(S, T);
    c.check(net.inter(S).table().find(T, 0.0) != nullptr, "route at 0 s");
    const std::uint32_t seq = net.intra(S).lastSeq();
    for (double t = 1.0; t <= 6.0; t += 1.0) {
        net.clock().set(t);
        net.send(S, T);
    }
    c.check(net.intra(S).lastSeq() == seq && net.delivered.size() == 7,
            "each use refreshed the record: no rediscovery in 6 s");
    c.check(net.inter(S).table().find(T, 8.9) && !net.inter(S).table().find(T, 9.0),
            "a record used every second survives; the last use at 6 s holds it to 9 s");
    net.clock().set(9.0);
    const std::size_t before = net.ants.size();
    const Outcome o = net.send(S, T);
    c.check(o.decisions.back().action == RouteAction::Queue && net.ants.size() > before,
            "not used for 3 s: gone, and the next packet starts a discovery");
}

void TestDiscoveryRetry(Checker& c) {
    // The retry row pinned: at most one discovery per destination per 1 s.
    Net net = fig41();
    const NodeAddress nowhere = 99;
    const Outcome a = net.send(S, nowhere);
    c.check(a.decisions.size() == 5 && a.decisions.back().action == RouteAction::Queue,
            "four ants (one per peripheral node), then the packet waits");
    const std::uint32_t seq = net.intra(S).lastSeq();
    net.clock().set(0.9);
    const Outcome b = net.send(S, nowhere);
    c.check(b.decisions.size() == 1 && b.decisions[0].action == RouteAction::Queue &&
                net.intra(S).lastSeq() == seq,
            "0.9 s later: no new discovery");
    net.clock().set(1.0);
    const Outcome d = net.send(S, nowhere);
    c.check(d.decisions.size() == 5 && net.intra(S).lastSeq() == seq + 1,
            "1 s later: a new generation");
    c.check(net.queued[S].size() == 3, "all three wait");
    c.check(net.inter(S).discover(S).empty(), "never a discovery for itself");

    Config cfg;
    cfg.interRtCapacity = 1;
    FakeClock clock;
    FakeLinkState link;
    SeededRng rng(1);
    IntraZone intra(cfg, S, clock, link, rng);
    InterZone z(cfg, intra, clock);
    DataHeader lonely{.source = S, .destination = 50};
    const Outcome alone = z.routeData(lonely, kInvalidAddress);
    c.check(alone.decisions.size() == 1 && alone.decisions[0].action == RouteAction::Queue,
            "no peripheral node: the packet waits, and no Drop reads as its fate");
    const std::uint32_t s1 = intra.lastSeq();
    z.discover(51);
    c.check(intra.lastSeq() == s1, "the discovery times are bounded: a full map starts none");
    clock.set(1.0);
    z.discover(51);
    c.check(intra.lastSeq() == s1 + 1, "until the old entry is past the interval");
}

void TestDataSendFailure(Checker& c) {
    // #5 (e) pinned: a failed data transmission with another neighbour toward
    // the target reroutes, without an ant; with none the packet goes.
    Config cfg;
    cfg.zoneRadius = 2;
    Net net(cfg);
    // 0 reaches 3 through 1 or 2.
    net.link(0, 1);
    net.link(0, 2);
    net.link(1, 3);
    net.link(2, 3);
    net.period();
    net.cut(0, 1);
    net.cut(0, 2);
    net.link(0, 2);  // only 2 still answers
    for (int i = 0; i < 20; ++i)
        net.send(0, 3);
    c.check(net.delivered.size() == 20 && net.ants.empty(),
            "every packet arrives, the dead neighbour's row gone after its first failure");
    c.check(!net.intra(0).table().hasNeighbour(1), "the row of 1 went at once");
    c.check(net.delivered[0].second.travelled == 2, "the failed hop is not counted");
    net.cut(0, 2);
    net.send(0, 3);
    c.check(net.delivered.size() == 20 && net.drops[0] == 1 && net.ants.empty(),
            "no neighbour left toward the target: dropped (local repair: the next slice)");
}

void TestInterRtHitAtPeripheral(Checker& c) {
    // [W07] p. 27: a peripheral node that holds an unexpired InterRT route to
    // the destination sends the ant on along it.
    FakeClock clock;
    FakeLinkState link;
    SeededRng rng(1);
    IntraZone intra(Config{}, U, clock, link, rng);
    InterZone z(Config{}, intra, clock);
    neighbours(intra, {O, 30});
    // U's own discovery of 40 stored <U, 30, 40> (a backward ant from 40).
    Ant back{.type = AntType::Backward,
             .source = 40,
             .destination = U,
             .seq = 3,
             .time = 0.02,
             .path = VisitedPath(100),
             .target = U,
             .travelled = 4};
    back.path.push(U, 0.005);
    back.path.push(30, 0.01);
    back.path.push(40, 0.02);
    const Outcome stored = z.onReceive(back, 30);
    c.check(stored.routeReady == 40 && z.table().find(40, 0.0), "U stores its route to 40");
    const Outcome o = z.onReceive(externalAnt(S, 40, 9, {S, D, M}, U), O);
    c.check(o.decisions.size() == 1 && o.decisions[0].action == RouteAction::Unicast &&
                o.decisions[0].nextHop == 30,
            "the ant follows U's route");
    const Ant& next = *o.decisions[0].message;
    c.check(
        nodesOf(next.path) == std::vector<NodeAddress>{S, D, M, U, 30} && next.target == 30,
        "U joins Path, then the route's bordercasters ahead of the ant; the next is the target");

    // At 30, a node of the cached route the ant already lists: on to 40.
    IntraZone intra30(Config{}, 30, clock, link, rng);
    InterZone z30(Config{}, intra30, clock);
    neighbours(intra30, {U, 40});
    const Ant& ahead = next;
    const Outcome o30 = z30.onReceive(ahead, U);
    c.check(o30.decisions.size() == 1 && o30.decisions[0].nextHop == 40 &&
                o30.decisions[0].message->target == 40 &&
                o30.decisions[0].message->path.size() == ahead.path.size(),
            "a Path node ahead of the ant passes it on toward the next one");
}

void TestTravelledBound(Checker& c) {
    // Sheet review point 2, proposed on #5: Config::maxHopsTravelled().
    Config cfg;
    cfg.maxPathLength = 6;
    FakeClock clock;
    FakeLinkState link;
    SeededRng rng(1);
    IntraZone intra(cfg, C, clock, link, rng);
    InterZone z(cfg, intra, clock);
    neighbours(intra, {S, D});
    Ant ant = externalAnt(S, T, 1, {S}, D);
    ant.travelled = 5;
    const Outcome ok = z.onReceive(ant, S);
    c.check(ok.decisions[0].action == RouteAction::Unicast &&
                ok.decisions[0].message->travelled == 6,
            "a transit relay forwards and counts the hop");
    ant.travelled = 6;
    c.check(z.onReceive(ant, S).decisions[0].action == RouteAction::Drop,
            "an ant that travelled the bound is dropped");
    DataHeader h{.source = S, .destination = T, .target = D, .travelled = 6, .route = {S, D, T}};
    c.check(z.routeData(h, S).decisions[0].action == RouteAction::Drop, "so is a data packet");
    h.travelled = 5;
    c.check(z.routeData(h, S).decisions[0].action == RouteAction::Unicast && h.travelled == 6,
            "below the bound it goes on");
}

void TestInterZoneMalformed(Checker& c) {
    FakeClock clock;
    FakeLinkState link;
    SeededRng rng(1);
    IntraZone intra(Config{}, C, clock, link, rng);
    InterZone z(Config{}, intra, clock);
    neighbours(intra, {S, D});
    auto dropped = [&](const Ant& a, NodeAddress from = S) {
        const Outcome o = z.onReceive(a, from);
        return o.decisions.size() == 1 && o.decisions[0].action == RouteAction::Drop;
    };
    Ant a = externalAnt(S, T, 1, {S}, D);
    c.check(!dropped(a), "a well-formed ant is relayed");
    Ant bad = a;
    bad.target = kInvalidAddress;
    c.check(dropped(bad), "no target");
    bad = a;
    bad.destination = S;
    c.check(dropped(bad), "source = destination");
    bad = a;
    bad.path = VisitedPath(100);
    c.check(dropped(bad), "an empty Path");
    bad = externalAnt(S, T, 1, {D}, D);
    c.check(dropped(bad), "a Path that does not start at the source");
    bad = a;
    bad.time = 0.0;
    c.check(dropped(bad), "no path time");
    c.check(dropped(a, C), "from itself");
    Ant back{.type = AntType::Backward,
             .source = T,
             .destination = S,
             .seq = 1,
             .time = 0.02,
             .path = VisitedPath(100),
             .target = S,
             .travelled = 1};
    back.path.push(S, 0.01);
    back.path.push(T, 0.02);
    c.check(!dropped(back, D), "a well-formed backward ant is relayed");
    Ant badBack = back;
    badBack.path.pop();
    c.check(dropped(badBack, D), "a backward Path that does not end at its source");
    badBack = back;
    badBack.target = C;
    c.check(dropped(badBack, D), "a backward ant whose target is not on Path");
    for (AntType t : {AntType::Notification, AntType::Error}) {
        Ant n = a;
        n.type = t;
        c.check(dropped(n), "notification and error ants: the local-repair slice");
    }
    DataHeader h{.source = S, .destination = T, .target = C, .route = {S, D, T}};
    c.check(z.routeData(h, S).decisions[0].action == RouteAction::Drop,
            "a data packet targeting a node not on its route");
    DataHeader noDest{.source = S, .target = D};
    c.check(z.routeData(noDest, S).decisions[0].action == RouteAction::Drop, "no destination");
    c.check(z.table().size() == 0 && intra.table().zone() == std::vector<NodeAddress>{S, D},
            "no malformed ant left a record or a cell");
}

void TestDataWire(Checker& c) {
    const DataHeader h{
        .source = 7, .destination = -3, .target = 260, .travelled = 9, .route = {7, 1, 260, -3}};
    std::vector<std::uint8_t> bytes;
    encode(h, bytes);
    c.check(bytes.size() == 1 + 4 + 4 + 4 + 2 + 2 + 4 * 4, "frame length");
    c.check(bytes[0] == kDataWireVersion, "version byte leads");
    c.check(bytes[9] == 4 && bytes[10] == 1, "target little-endian");
    const auto back = decodeData(bytes, 100);
    c.check(back && *back == h, "decode(encode(h)) == h");
    for (std::size_t n = 0; n < bytes.size(); ++n)
        c.check(!decodeData(std::span(bytes).first(n), 100), "a truncated frame fails");
    auto bad = bytes;
    bad[0] = kDataWireVersion + 1;
    c.check(!decodeData(bad, 100), "another version fails");
    bad = bytes;
    bad.push_back(0);
    c.check(!decodeData(bad, 100), "trailing bytes fail");
    c.check(!decodeData(bytes, 3) && decodeData(bytes, 4), "the route cap");

    SeededRng rng(5);
    int decoded = 0;
    for (int i = 0; i < 4000; ++i) {
        std::vector<std::uint8_t> f;
        if (i % 2 == 0) {
            const int n = rng.uniformInt(40);
            for (int k = 0; k < n; ++k)
                f.push_back(static_cast<std::uint8_t>(rng.next()));
        } else {
            f = bytes;
            f[static_cast<std::size_t>(rng.uniformInt(static_cast<int>(f.size())))] ^=
                static_cast<std::uint8_t>(1u + rng.uniformInt(255));
        }
        const auto d = decodeData(f, 100);
        if (!d) continue;
        ++decoded;
        std::vector<std::uint8_t> again;
        encode(*d, again);
        c.check(again == f, "whatever decodes re-encodes to the same bytes");
    }
    c.check(decoded > 0, "the fuzz reaches the decoded path");
}

void TestInterZoneDeterminism(Checker& c) {
    auto run = [](std::uint64_t seed) {
        Net net = fig41(seed);
        for (int i = 0; i < 5; ++i) {
            net.clock().set(0.5 * i);
            net.send(S, T);
            net.send(T, S);
            net.send(F, K);
        }
        std::vector<std::int64_t> out;
        for (const auto& s : net.ants) {
            out.push_back(s.from);
            out.push_back(s.to);
            out.push_back(s.ant.target);
        }
        for (const auto& [f, t] : net.dataHops) {
            out.push_back(f);
            out.push_back(t);
        }
        out.push_back(static_cast<std::int64_t>(net.delivered.size()));
        return out;
    };
    const auto a = run(11);
    c.check(a == run(11), "a fixed seed gives the same ants, hops and deliveries (org rule 5)");
    c.check(a.back() == 15, "every packet of the three flows arrives");
}

}  // namespace hopnet::core::test
