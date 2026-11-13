// Copyright (c) 2026 Daniel Joppi (https://github.com/swarm-routing)
// SPDX-License-Identifier: MIT OR GPL-2.0-only
//
// The intra-zone half against the readings pre-registered on #5 and accepted
// on 2026-11-10: eq. 4.1-4.3 with T' = T(S,D) - T(S,v_k) (a), neighbour
// discovery and loss (e), the zone-limited flood and the peripheral rule (f),
// the edge time w; plus selection, the caps and determinism.
#include "../hopnet-intrazone.h"
#include "hopnet-check.h"

#include <deque>
#include <map>
#include <memory>
#include <set>
#include <tuple>

namespace hopnet::core::test {

namespace {

/// A lossless, instantaneous radio over a fixed graph. Frames are delivered
/// in FIFO order, a broadcast to the sender's neighbours in ascending order,
/// so a flood reaches every node first along a shortest path.
class Net {
public:
    explicit Net(Config cfg = {}, std::uint64_t seed = 1) : cfg_(cfg), rng_(seed) {}

    void link(NodeAddress a, NodeAddress b) {
        adj_[a].insert(b);
        adj_[b].insert(a);
        node(a);
        node(b);
    }
    /// Sets node n's MAC service time, so w(n, .) = service (queue 0).
    void service(NodeAddress n, double s) { link_[n].service = s; }

    IntraZone& at(NodeAddress n) { return node(n).zone; }
    FakeClock& clock() { return clock_; }

    /// Node n's internal-ant timer fires; the network then runs to quiet.
    void tick(NodeAddress n) {
        emit(n, at(n).onInternalAntTimer());
        while (!air_.empty()) {
            auto [from, to, ant] = air_.front();
            air_.pop_front();
            emit(to, at(to).onReceive(ant, from));
        }
    }
    /// One period: every node's timer, in ascending order.
    void period() {
        for (const auto& kv : adj_)
            tick(kv.first);
    }

    std::map<NodeAddress, int> broadcasts;  // per sender
    std::set<NodeAddress> backwardOrigins;  // nodes that turned an ant around
    std::map<NodeAddress, std::set<NodeAddress>> adj_;

private:
    struct Node {
        IntraZone zone;
    };
    Node& node(NodeAddress n) {
        auto it = nodes_.find(n);
        if (it == nodes_.end()) {
            auto fresh = std::make_unique<Node>(Node{IntraZone(cfg_, n, clock_, link_[n], rng_)});
            it = nodes_.emplace(n, std::move(fresh)).first;
        }
        return *it->second;
    }
    void emit(NodeAddress from, const Decision& d) {
        if (!d.message) return;
        if (d.action == swarm::core::RouteAction::Broadcast) {
            ++broadcasts[from];
            for (NodeAddress to : adj_[from])
                air_.emplace_back(from, to, *d.message);
        } else if (d.action == swarm::core::RouteAction::Unicast && adj_[from].count(d.nextHop)) {
            if (d.message->type == AntType::Backward && d.message->source == from)
                backwardOrigins.insert(from);
            air_.emplace_back(from, d.nextHop, *d.message);
        }
    }

    Config cfg_;
    FakeClock clock_;
    SeededRng rng_;
    std::map<NodeAddress, FakeLinkState> link_;
    std::map<NodeAddress, std::unique_ptr<Node>> nodes_;
    std::deque<std::tuple<NodeAddress, NodeAddress, Ant>> air_;
};

double phi(IntraZone& z, NodeAddress column, NodeAddress neighbour) {
    const auto cell = z.table().cell(column, neighbour);
    return cell ? cell->pheromone : -1.0;
}

int hopsOf(IntraZone& z, NodeAddress column, NodeAddress neighbour) {
    const auto cell = z.table().cell(column, neighbour);
    return cell ? cell->hops : -1;
}

/// A forward ant from `source` as `from` would re-broadcast it to us.
Ant forwardAnt(NodeAddress source, NodeAddress from, std::uint32_t seq, double time,
               std::uint16_t hopsLeft) {
    Ant a{.type = AntType::InternalForward,
          .source = source,
          .seq = seq,
          .hops = hopsLeft,
          .time = time,
          .path = VisitedPath(100)};
    a.path.push(source, time / 2);
    if (from != source) a.path.push(from, time);
    return a;
}

}  // namespace

void TestTableUpdate(Checker& c) {
    IntraZoneTable t(3, 0.2);
    c.check(t.update(9, 1, 10.0, 2, 5, 0.0), "a first deposit creates the cell");
    c.check(!t.update(9, 1, 10.0, 2, 5, 1.0), "the same SeqNo does not update a cell twice");
    c.check(t.update(9, 2, 4.0, 1, 5, 0.0), "the same SeqNo updates another cell");
    c.near(t.cell(9, 1)->pheromone, 8.0, "eq. 4.2: the other neighbour's cell keeps 1 - E");
    c.near(t.cell(9, 2)->pheromone, 4.0, "the deposited cell does not decay");
    c.check(t.update(9, 1, 1.0, 3, 6, 0.5), "a newer SeqNo updates");
    c.check(t.cell(9, 1)->visits == 2 && t.cell(9, 1)->hops == 3 && t.cell(9, 1)->seq == 6,
            "visits counted, hops and SeqNo replaced");
    c.near(t.cell(9, 2)->pheromone, 3.2, "decay again");
    c.check(!t.update(9, 3, 0.0, 1, 1, 0.5) && !t.update(9, 3, -1.0, 1, 1, 0.5),
            "no zero/negative deposit");
    c.check(t.update(8, 2, 1.0, 2, 1, 0.5), "third cell");
    c.check(!t.update(7, 1, 1.0, 1, 1, 0.5) && t.size() == 3, "a full table refuses a new cell");
    c.check(t.zone() == std::vector<NodeAddress>{8, 9}, "columns");
    c.check(t.neighbours() == std::vector<NodeAddress>{1, 2}, "rows");
    c.check(t.minHops(9) == 1 && !t.minHops(5), "minimum hops over the column");
    c.check(t.peripheral(2) == std::vector<NodeAddress>{8},
            "9 is interior: reached in 1 hop via 2 although 3 hops via 1");
    c.check(t.removeNeighbour(2) == 2 && t.neighbours() == std::vector<NodeAddress>{1},
            "a lost neighbour's row goes");
    c.check(t.zone() == std::vector<NodeAddress>{9}, "and the columns only it held");

    // The stale-entry row (#93): a cell no ant updated for the timeout goes.
    IntraZoneTable s(3, 0.2);
    s.update(9, 2, 4.0, 1, 5, 0.0);
    s.update(9, 1, 1.0, 3, 6, 0.5);
    s.update(8, 2, 1.0, 2, 1, 0.5);
    c.check(!s.update(9, 2, 4.0, 1, 5, 1.0) && s.cell(9, 2)->refreshed == 0.0 &&
                s.cell(9, 1)->refreshed == 0.5,
            "refreshed: the time of the last update, not of a repeated SeqNo");
    c.check(s.removeStale(1.9, 2.0) == 0 && s.size() == 3, "no cell is 2 s old at 1.9 s");
    c.check(s.removeStale(2.0, 2.0) == 1 && !s.cell(9, 2) && s.cell(9, 1) && s.cell(8, 2),
            "at 2.0 s the cell last updated at 0 s goes; its row and its column stay");
    c.check(s.pheromone().size() == 2 && s.minHops(9) == 3, "the pheromone goes with it");
    c.check(s.update(7, 1, 1.0, 1, 1, 2.0), "and the freed slot takes a new cell");
    c.check(s.removeStale(2.5, 2.0) == 2 && s.zone() == std::vector<NodeAddress>{7},
            "a column whose last cell goes leaves the zone");
}

void TestEdgeTime(Checker& c) {
    FakeClock clock;
    FakeLinkState link;
    SeededRng rng(1);
    IntraZone z(Config{}, 1, clock, link, rng);
    c.near(z.edgeTime(2), 0.003, "3 ms before the first MAC sample (#5)");
    link.service = 0.002;
    link.queue = 4;
    c.near(z.edgeTime(2), 0.010, "(Q_mac + 1) * T_mac");
    link.queue = -1;
    c.near(z.edgeTime(2), 0.002, "a negative queue reads as 0");
    const Decision d = z.onInternalAntTimer();
    c.check(d.action == swarm::core::RouteAction::Broadcast && d.message, "the timer broadcasts");
    c.check(d.message->hops == 2 && d.message->destination == kInvalidAddress &&
                d.message->seq == 1 && z.lastSeq() == 1,
            "Hops = radius, blank Destination, a fresh SeqNo ([W07] p. 25)");
    c.check(d.message->path.size() == 1 && d.message->path.back() == swarm::core::Hop{1, 0.002},
            "Path holds the source and the time it left");
}

void TestBackwardDeposits(Checker& c) {
    // #5 (a) pinned: S-a-b-D, radius 3, w1 = 1 ms, w2 = 2 ms, w3 = 4 ms.
    Config cfg;
    cfg.zoneRadius = 3;
    Net net(cfg);
    net.link(1, 2);
    net.link(2, 3);
    net.link(3, 4);
    net.service(1, 0.001);
    net.service(2, 0.002);
    net.service(3, 0.004);
    net.tick(1);
    c.check(net.backwardOrigins == std::set<NodeAddress>{4}, "D turns the ant around");
    // Eq. 4.1, column S.
    c.near(phi(net.at(2), 1, 1), 1 / 0.001, "a: (S, S) += 1 / w1");
    c.near(phi(net.at(3), 1, 2), 1 / 0.003, "b: (a, S) += 1 / (w1 + w2)");
    c.near(phi(net.at(4), 1, 3), 1 / 0.007, "D: (b, S) += 1 / (w1 + w2 + w3)");
    c.check(hopsOf(net.at(4), 1, 3) == 3, "D: S is 3 hops via b");
    // Eq. 4.3, column D, T' = time from the updating node to D.
    c.near(phi(net.at(3), 4, 4), 1 / 0.004, "b: (D, D) += 1 / w3");
    c.near(phi(net.at(2), 4, 3), 1 / 0.006, "a: (b, D) += 1 / (w2 + w3)");
    c.near(phi(net.at(1), 4, 2), 1 / 0.007, "S: (a, D) += 1 / (w1 + w2 + w3)");
    c.check(hopsOf(net.at(1), 4, 2) == 3 && hopsOf(net.at(3), 4, 4) == 1, "backward hops");
    c.check(net.at(4).lastSeq() == 1, "D spent a SeqNo on its backward ant");
}

void TestFloodLine(Checker& c) {
    // #5 (f) pinned: radius 2, one period of S = 0 on the line 0-1-2-3-4.
    Net net;
    for (NodeAddress n = 0; n < 4; ++n)
        net.link(n, n + 1);
    net.tick(0);
    c.check(net.broadcasts == std::map<NodeAddress, int>{{0, 1}, {1, 1}},
            "one transmission per node within r - 1 hops, none beyond");
    c.check(net.backwardOrigins == std::set<NodeAddress>{2}, "backward ants only from distance r");
    c.check(net.at(2).table().zone() == std::vector<NodeAddress>{0}, "2 holds column 0");
    c.check(net.at(3).table().zone().empty(), "3, beyond the radius, holds nothing");
    c.check(net.at(0).table().zone() == std::vector<NodeAddress>{2},
            "S learnt 2 by the backward ant");
    c.check(net.at(0).table().peripheral(2) == std::vector<NodeAddress>{2}, "2 is peripheral");
}

void TestFloodDuplicateCopies(Checker& c) {
    // Diamond S = 0 to 1 and 2, both to 3; radius 3, so 3 relays.
    Config cfg;
    cfg.zoneRadius = 3;
    Net net(cfg);
    net.link(0, 1);
    net.link(0, 2);
    net.link(1, 3);
    net.link(2, 3);
    net.link(3, 4);
    net.service(0, 0.001);
    net.service(1, 0.001);
    net.service(2, 0.003);
    net.tick(0);
    c.check(net.broadcasts.at(3) == 1, "3 re-broadcasts only the first copy");
    c.near(phi(net.at(3), 0, 2), 1 / 0.004, "the copy via 2 deposits in its own cell");
    c.near(phi(net.at(3), 0, 1), 0.8 / 0.002, "and decays the cell via 1 by 1 - E");
    const auto before = net.at(3).table().cell(0, 1);
    const Decision again = net.at(3).onReceive(forwardAnt(0, 1, 1, 0.002, 2), 1);
    c.check(again.action == swarm::core::RouteAction::None, "a repeated copy is consumed");
    c.check(net.at(3).table().cell(0, 1) == before, "and updates no cell twice (SeqNo guard)");
    c.check(net.backwardOrigins == std::set<NodeAddress>{4}, "backward ant from 4 only");
}

void TestZoneAfterOnePeriod(Checker& c) {
    // A 3 x 4 grid with one diagonal (0-5), radius 2: after one period every
    // node's zone is exactly the nodes within 2 hops, minimum hops are the
    // BFS distances, and the peripheral nodes are those at distance 2.
    Net net;
    for (NodeAddress n = 0; n < 12; ++n) {
        if (n % 4 != 3) net.link(n, n + 1);
        if (n < 8) net.link(n, n + 4);
    }
    net.link(0, 5);
    net.period();
    for (const auto& [v, nbrs] : net.adj_) {
        std::map<NodeAddress, int> dist{{v, 0}};
        std::deque<NodeAddress> q{v};
        while (!q.empty()) {
            const NodeAddress u = q.front();
            q.pop_front();
            for (NodeAddress x : net.adj_[u])
                if (!dist.count(x)) dist[x] = dist[u] + 1, q.push_back(x);
        }
        std::vector<NodeAddress> zone, peri;
        for (const auto& [x, d] : dist) {
            if (d >= 1 && d <= 2) zone.push_back(x);
            if (d == 2) peri.push_back(x);
        }
        const auto& t = net.at(v).table();
        c.check(t.zone() == zone, "zone = nodes within the radius");
        c.check(t.peripheral(2) == peri, "peripheral = minimum hops equal to the radius");
        c.check(t.neighbours() == std::vector<NodeAddress>(nbrs.begin(), nbrs.end()),
                "rows = one-hop neighbours, discovered by the ants alone");
        for (NodeAddress x : zone)
            c.check(t.minHops(x) == dist[x], "minimum hops = distance");
    }
}

void TestNeighbourLoss(Checker& c) {
    // #5 (e) pinned: discovery by the first-hop ant, loss after
    // Config::neighbourTimeout() (3 periods of 2 s = 6 s, #5 (n), (p)) or a MAC
    // failure.
    const Time timeout = Config{}.neighbourTimeout();
    c.check(timeout == 6.0, "the neighbour timeout: 3 internal-ant periods of 2 s");
    Net net;
    net.link(0, 1);
    net.tick(1);
    c.check(hopsOf(net.at(0), 1, 1) == 1, "the first-hop ant from 1 creates (1, 1), hops 1");
    net.clock().set(timeout - 0.1);
    net.at(0).purgeStaleNeighbours();
    c.check(net.at(0).table().hasNeighbour(1), "still a neighbour at 5.9 s");
    net.clock().set(timeout);
    net.at(0).purgeStaleNeighbours();
    c.check(!net.at(0).table().hasNeighbour(1), "gone after 3 periods without an ant");
    net.tick(1);
    c.check(net.at(0).table().hasNeighbour(1), "rediscovered by the next ant");
    net.clock().set(timeout + 5.5);
    net.at(0).onInternalAntTimer();
    c.check(net.at(0).table().hasNeighbour(1), "heard at 6 s: kept at 11.5 s");
    net.at(0).onLinkFailure(1);
    c.check(!net.at(0).table().hasNeighbour(1), "a unicast failure removes the row at once");
}

void TestSelection(Checker& c) {
    FakeClock clock;
    FakeLinkState link;
    SeededRng rng(42);
    IntraZone z(Config{}, 0, clock, link, rng);
    c.check(z.selectNextHop(9) == kInvalidAddress, "no pheromone: no next hop ([W07] p. 34)");
    z.onReceive(forwardAnt(9, 1, 1, 0.010, 1), 1);  // (1, 9) = 100
    z.onReceive(forwardAnt(9, 2, 1, 0.020, 1), 2);  // (2, 9) = 50, (1, 9) -> 80
    int via1 = 0;
    for (int i = 0; i < 1000; ++i)
        via1 += z.selectNextHop(9) == 1;
    c.check(via1 > 950, "beta = 20 strongly prefers the better neighbour (80 vs 50)");
    c.check(z.selectNextHop(9, 1) == 2, "the excluded neighbour is left out");
    c.check(z.selectNextHop(9) != kInvalidAddress && z.selectNextHop(5) == kInvalidAddress,
            "a column outside the zone has no next hop");
}

void TestBounds(Checker& c) {
    Config cfg;
    cfg.zoneRadius = 5;
    cfg.maxPathLength = 2;
    Net net(cfg);
    for (NodeAddress n = 0; n < 4; ++n)
        net.link(n, n + 1);
    net.tick(0);
    c.check(net.broadcasts == std::map<NodeAddress, int>{{0, 1}, {1, 1}},
            "an ant whose Path is full is not re-broadcast (Path cap)");
    c.check(net.at(3).table().size() == 0, "and goes no further");

    cfg = Config{};
    cfg.intraRtCapacity = 1;
    FakeClock clock;
    FakeLinkState link;
    SeededRng rng(1);
    IntraZone z(cfg, 0, clock, link, rng);
    z.onReceive(forwardAnt(1, 1, 1, 0.01, 1), 1);
    z.onReceive(forwardAnt(2, 2, 1, 0.01, 1), 2);
    c.check(z.table().size() == 1 && !z.table().hasNeighbour(2), "a full IntraRT takes no cell");

    cfg = Config{};
    cfg.historyCapacity = 1;
    IntraZone h(cfg, 0, clock, link, rng);
    c.check(h.onReceive(forwardAnt(5, 5, 1, 0.01, 2), 5).action ==
                swarm::core::RouteAction::Broadcast,
            "first copy re-broadcast");
    h.onReceive(forwardAnt(6, 6, 1, 0.01, 2), 6);  // evicts (5, 1) from a history of 1
    c.check(h.onReceive(forwardAnt(5, 7, 1, 0.01, 2), 7).action ==
                swarm::core::RouteAction::Broadcast,
            "the history holds no more than its cap: an evicted generation is new again");
}

void TestMalformedAnts(Checker& c) {
    FakeClock clock;
    FakeLinkState link;
    SeededRng rng(1);
    IntraZone z(Config{}, 0, clock, link, rng);
    using swarm::core::RouteAction;
    c.check(z.onReceive(forwardAnt(5, 6, 1, 0.01, 1), 7).action == RouteAction::Drop,
            "Path must end with the sender");
    c.check(z.onReceive(forwardAnt(5, 5, 1, 0.0, 1), 5).action == RouteAction::Drop,
            "a zero Time would deposit 1/0");
    c.check(z.onReceive(forwardAnt(5, 5, 1, 0.01, 0), 5).action == RouteAction::Drop,
            "no hop budget left");
    c.check(z.onReceive(forwardAnt(5, 5, 1, 0.01, 3), 5).action == RouteAction::Drop,
            "a hop budget above the zone radius (2) would flood past the zone");
    c.check(z.onReceive(forwardAnt(0, 5, 1, 0.01, 1), 5).action == RouteAction::Drop,
            "this node's own ant coming back");
    Ant ext = forwardAnt(5, 5, 1, 0.01, 1);
    ext.type = AntType::ExternalForward;
    c.check(z.onReceive(ext, 5).action == RouteAction::Drop, "inter-zone types: not this slice");
    Ant back = forwardAnt(5, 5, 1, 0.01, 1);
    back.type = AntType::Backward;
    c.check(z.onReceive(back, 5).action == RouteAction::Drop, "a backward ant not routed via us");
    c.check(z.table().size() == 0, "no malformed ant left a cell");
}

void TestDeterminism(Checker& c) {
    auto run = [](std::uint64_t seed) {
        Net net(Config{}, seed);
        for (NodeAddress n = 0; n < 8; ++n)
            net.link(n, (n + 1) % 8);
        net.link(0, 4);
        for (NodeAddress n = 0; n < 8; ++n)
            net.service(n, 0.001 * (n + 1));
        net.period();
        net.period();
        std::vector<double> out;
        for (NodeAddress v = 0; v < 8; ++v) {
            for (NodeAddress col : net.at(v).table().zone()) {
                for (NodeAddress nb : net.at(v).table().neighbours())
                    out.push_back(phi(net.at(v), col, nb));
                for (int i = 0; i < 5; ++i)
                    out.push_back(net.at(v).selectNextHop(col));
            }
        }
        return out;
    };
    c.check(run(7) == run(7), "a fixed seed gives identical tables and selections (org rule 5)");
}

}  // namespace hopnet::core::test
