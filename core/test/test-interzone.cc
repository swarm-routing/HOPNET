// Copyright (c) 2026 Daniel Joppi (https://github.com/swarm-routing)
// SPDX-License-Identifier: MIT OR GPL-2.0-only
//
// The inter-zone half against the readings pre-registered on #5 and accepted
// on 2026-11-10: the bordercast and its query control on the Fig. 4.1
// topology (b), the destination's generation check (c), the external backward
// ant and the InterRT (d), the InterRT expiry and the discovery retry policy,
// a failed data transmission (e); plus the data header codec, the bounds and
// determinism.
#include "hopnet-net.h"

#include <algorithm>

namespace hopnet::core::test {

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
    Config cfg;  // (b) alone: the bordercast at once, no wait (#5 (t), TestQueryDelay)
    cfg.queryProcessingDelay = 0.0;
    IntraZone intra(cfg, D, clock, link, rng);
    InterZone z(cfg, intra, clock);
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

void TestQueryDetection(Checker& c) {
    // #5 (q), accepted 2026-11-13: ZRP's query detection (QD1) and early
    // termination on top of (b), on Fig. 4.1. The previous bordercaster of a
    // copy is the Path node it was sent from.
    {
        // Early termination at a transit relay: C carried (S, 50) from S.
        Net net = fig41();
        const Outcome toD = net.inject(C, externalAnt(S, T, 50, {S}, D), S);
        c.check(toD.decisions.size() == 1 && toD.decisions[0].action == RouteAction::Unicast &&
                    toD.decisions[0].nextHop == D,
                "C relays S's copy toward D");
        const Outcome toA = net.inject(C, externalAnt(S, T, 50, {S}, A), S);
        c.check(toA.decisions.size() == 1 && toA.decisions[0].action == RouteAction::Unicast &&
                    toA.decisions[0].nextHop == A,
                "and S's second copy, toward A: one bordercast's branches both pass (b)");
        const Outcome fromD = net.inject(C, externalAnt(S, T, 50, {S, D}, A), D);
        c.check(fromD.decisions.size() == 1 && fromD.decisions[0].action == RouteAction::Drop,
                "a copy of the same query from another bordercaster (D) ends at C");
        const Outcome other = net.inject(C, externalAnt(S, T, 51, {S, D}, A), D);
        c.check(other.decisions.size() == 1 && other.decisions[0].action == RouteAction::Unicast,
                "another query (S, 51) is not affected");
        // Directed copies are not bordercasts: they always pass.
        net.inject(C, externalAnt(S, A, 52, {S}, D), S);
        const Outcome toDest = net.inject(C, externalAnt(S, A, 52, {S, D}, A), D);
        c.check(toDest.decisions.size() == 1 &&
                    toDest.decisions[0].action == RouteAction::Unicast &&
                    toDest.decisions[0].nextHop == A,
                "a copy sent to the destination itself passes C");
        net.inject(C, externalAnt(S, T, 53, {S}, D), S);
        const Outcome cached = net.inject(C, externalAnt(S, T, 53, {S, D, A}, A), D);
        c.check(cached.decisions.size() == 1 &&
                    cached.decisions[0].action == RouteAction::Unicast &&
                    cached.decisions[0].nextHop == A,
                "and so does a copy following a cached InterRT route (target on Path)");
    }
    {
        // A covered peripheral node does not bordercast: D relayed (S, 60)
        // from M before S's copy addressed to D arrives.
        Net net = fig41();
        net.inject(D, externalAnt(S, T, 60, {S, M}, C), I);
        const std::size_t before = net.ants.size();
        const Outcome covered = net.inject(D, externalAnt(S, T, 60, {S}, D), C);
        c.check(covered.decisions.size() == 1 && covered.decisions[0].action == RouteAction::Drop &&
                    targetsSentBy(net, D, before).empty(),
                "D, covered, does not bordercast S's copy");
        net.inject(D, externalAnt(S, K, 61, {S, M}, C), I);
        const Outcome inZone = net.inject(D, externalAnt(S, K, 61, {S}, D), C);
        c.check(inZone.decisions.size() == 1 &&
                    inZone.decisions[0].action == RouteAction::Unicast &&
                    inZone.decisions[0].message->target == K,
                "but still sends it toward a destination in its zone ([W07] p. 27)");
    }
    {
        // The source records its own query: a copy that comes back to it
        // through another bordercaster ends there.
        Net net = fig41();
        net.send(S, T);
        const std::uint32_t seq = net.intra(S).lastSeq();
        const Outcome back = net.inject(S, externalAnt(S, T, seq, {S, D}, E), C);
        c.check(back.decisions.size() == 1 && back.decisions[0].action == RouteAction::Drop,
                "the source detected its own query");
    }
    {
        // The full discovery of Fig. 4.1 is unchanged by detection.
        Net net = fig41();
        net.send(S, T);
        c.check(targetsSentBy(net, D) == std::set<NodeAddress>{K, M} &&
                    net.inter(S).table().find(T, 0.0) != nullptr,
                "D still sends to K and M, and S gets its route");
    }
    {
        // With the switch off, (b) alone: the relay passes every copy.
        Config cfg;
        cfg.queryDetection = false;
        Net net = fig41(1, cfg);
        net.inject(C, externalAnt(S, T, 50, {S}, D), S);
        const Outcome fromD = net.inject(C, externalAnt(S, T, 50, {S, D}, A), D);
        c.check(fromD.decisions.size() == 1 && fromD.decisions[0].action == RouteAction::Unicast,
                "queryDetection = false: no early termination");
        net.inject(D, externalAnt(S, T, 60, {S, M}, C), I);
        const std::size_t before = net.ants.size();
        net.inject(D, externalAnt(S, T, 60, {S}, D), C);
        c.check(targetsSentBy(net, D, before) == std::set<NodeAddress>{K, M},
                "and a peripheral node that relayed the query still bordercasts it");
    }
    {
        // Bounded (org rule 7): the detection table keeps historyCapacity
        // queries, oldest first.
        Config cfg;
        cfg.historyCapacity = 2;
        Net net = fig41(1, cfg);
        net.inject(C, externalAnt(S, T, 70, {S}, D), S);
        net.inject(C, externalAnt(S, T, 71, {S}, D), S);
        net.inject(C, externalAnt(S, T, 72, {S}, D), S);
        const Outcome forgotten = net.inject(C, externalAnt(S, T, 70, {S, D}, A), D);
        c.check(forgotten.decisions.size() == 1 &&
                    forgotten.decisions[0].action == RouteAction::Unicast,
                "the oldest query is forgotten past the capacity");
        const Outcome kept = net.inject(C, externalAnt(S, T, 72, {S, D}, A), D);
        c.check(kept.decisions.size() == 1 && kept.decisions[0].action == RouteAction::Drop,
                "the newest is still detected");
    }
}

void TestQueryOverhearing(Checker& c) {
    // #5 (s), accepted 2026-11-13: ZRP's QD2. A node detects a query it only
    // overheard (a copy addressed to another node), as a transit relay does
    // under (q). In Fig. 4.1, M's copy toward C (previous bordercaster M)
    // can be overheard by D.
    {
        Net net = fig41();
        net.inter(D).overhear(externalAnt(S, T, 60, {S, M}, C));
        const std::size_t before = net.ants.size();
        const Outcome covered = net.inject(D, externalAnt(S, T, 60, {S}, D), C);
        c.check(covered.decisions.size() == 1 && covered.decisions[0].action == RouteAction::Drop &&
                    targetsSentBy(net, D, before).empty(),
                "D overheard (S, 60) from M: covered, it does not bordercast S's copy");
        net.inter(D).overhear(externalAnt(S, K, 61, {S, M}, C));
        const Outcome inZone = net.inject(D, externalAnt(S, K, 61, {S}, D), C);
        c.check(inZone.decisions.size() == 1 &&
                    inZone.decisions[0].action == RouteAction::Unicast &&
                    inZone.decisions[0].message->target == K,
                "but still sends it toward a destination in its zone ([W07] p. 27)");
        const Outcome relayed = net.inject(D, externalAnt(S, T, 60, {S}, K), C);
        c.check(relayed.decisions.size() == 1 && relayed.decisions[0].action == RouteAction::Drop,
                "and, as a relay, ends a bordercast copy of it from another bordercaster (S)");
    }
    {
        // Overheard from the same bordercaster: S's copy toward A, then S's
        // copy addressed to D. One bordercast's copies do not cover D.
        Net net = fig41();
        net.inter(D).overhear(externalAnt(S, T, 62, {S}, A));
        const std::size_t before = net.ants.size();
        net.inject(D, externalAnt(S, T, 62, {S}, D), C);
        c.check(targetsSentBy(net, D, before) == std::set<NodeAddress>{K, M},
                "a copy overheard from D's own bordercaster leaves D uncovered");
    }
    {
        // Directed copies are not bordercasts (#5 (q)): overhearing one
        // detects nothing.
        Net net = fig41();
        net.inter(D).overhear(externalAnt(S, T, 63, {S, M, A}, A));  // following a route
        net.inter(D).overhear(externalAnt(S, T, 63, {S, M}, T));     // to the destination
        const std::size_t before = net.ants.size();
        net.inject(D, externalAnt(S, T, 63, {S}, D), C);
        c.check(targetsSentBy(net, D, before) == std::set<NodeAddress>{K, M},
                "a directed copy overheard does not cover D");
    }
    {
        // Overhearing never sends, never touches a table.
        Net net = fig41();
        const std::size_t ants = net.ants.size();
        const std::size_t cells = net.intra(D).table().size();
        net.inter(D).overhear(externalAnt(S, T, 64, {S, M}, C));
        net.inter(D).overhear(externalAnt(S, T, 64, {S, M}, C));  // a MAC retransmission
        Ant bad = externalAnt(S, T, 65, {M}, C);                  // Path not from its source
        net.inter(D).overhear(bad);
        net.inject(D, externalAnt(S, T, 65, {S}, D), C);
        c.check(net.ants.size() > ants && net.intra(D).table().size() == cells,
                "a malformed overheard ant is ignored, and nothing is sent or stored");
    }
    {
        // With the switch off, (q)'s QD1 alone: an overheard copy is ignored.
        Config cfg;
        cfg.queryOverhearing = false;
        Net net = fig41(1, cfg);
        net.inter(D).overhear(externalAnt(S, T, 60, {S, M}, C));
        const std::size_t before = net.ants.size();
        net.inject(D, externalAnt(S, T, 60, {S}, D), C);
        c.check(targetsSentBy(net, D, before) == std::set<NodeAddress>{K, M},
                "queryOverhearing = false: D still bordercasts");
        Config off;
        off.queryDetection = false;
        Net net2 = fig41(1, off);
        net2.inter(D).overhear(externalAnt(S, T, 60, {S, M}, C));
        const std::size_t before2 = net2.ants.size();
        net2.inject(D, externalAnt(S, T, 60, {S}, D), C);
        c.check(targetsSentBy(net2, D, before2) == std::set<NodeAddress>{K, M},
                "and so with queryDetection off");
    }
    {
        // The whole discovery of Fig. 4.1, every unicast ant overheard by the
        // sender's other neighbours: S still gets its route.
        Net net = fig41();
        net.send(S, T);
        c.check(net.inter(S).table().find(T, 0.0) != nullptr && net.delivered.size() == 1,
                "with overhearing the discovery still finds T and the packet arrives");
    }
}

void TestQueryDelay(Checker& c) {
    // #5 (t), accepted 2026-11-13: BRP's random query-processing delay. A
    // bordercaster addressed by a copy waits U[0, J) before it bordercasts and
    // learns coverage in the wait. Calls go straight to D (not inject), so the
    // test network's timers do not fire on their own.
    const Time J = Config{}.queryProcessingDelay;
    c.check(J == 0.1, "J = 0.1 s (fidelity, Query-processing delay)");
    {
        Net net = fig41();
        const Outcome wait = net.inter(D).onReceive(externalAnt(S, T, 70, {S}, D), C);
        c.check(wait.decisions.empty() && wait.querySource == S && wait.querySeq == 70 &&
                    wait.queryWait >= 0.0 && wait.queryWait < J,
                "D sends nothing yet and asks for a wait in [0, J)");
        const Outcome go = net.inter(D).onQueryDelay(S, 70);
        std::set<NodeAddress> targets;
        for (const auto& d : go.decisions)
            if (d.action == RouteAction::Unicast && d.message) targets.insert(d.message->target);
        c.check(targets == std::set<NodeAddress>{K, M}, "after the wait, D bordercasts to K and M");
        c.check(net.inter(D).onQueryDelay(S, 70).decisions.empty(), "a second timer finds nothing");
    }
    {
        // A copy D relays in the wait from another bordercaster (M) covers its
        // zone: the bordercast is cancelled (BRP §4).
        Net net = fig41();
        net.inter(D).onReceive(externalAnt(S, T, 71, {S}, D), C);
        net.inter(D).onReceive(externalAnt(S, T, 71, {S, M}, C), I);
        const Outcome go = net.inter(D).onQueryDelay(S, 71);
        c.check(go.decisions.size() == 1 && go.decisions[0].action == RouteAction::Drop,
                "a covering copy relayed in the wait cancels the bordercast");
    }
    {
        // So does one D overhears (#5 (s)).
        Net net = fig41();
        net.inter(D).onReceive(externalAnt(S, T, 72, {S}, D), C);
        net.inter(D).overhear(externalAnt(S, T, 72, {S, M}, C));
        const Outcome go = net.inter(D).onQueryDelay(S, 72);
        c.check(go.decisions.size() == 1 && go.decisions[0].action == RouteAction::Drop,
                "a covering copy overheard in the wait cancels it too");
    }
    {
        // A copy from the same bordercaster does not cover D.
        Net net = fig41();
        net.inter(D).onReceive(externalAnt(S, T, 73, {S}, D), C);
        net.inter(D).overhear(externalAnt(S, T, 73, {S}, A));
        std::set<NodeAddress> targets;
        for (const auto& d : net.inter(D).onQueryDelay(S, 73).decisions)
            if (d.message) targets.insert(d.message->target);
        c.check(targets == std::set<NodeAddress>{K, M}, "one bordercast's branches do not cover D");
    }
    {
        // A peripheral node seen on a copy's Path in the wait has bordercast
        // the query already: skipped. Here M's copy addressed to D arrives as
        // a duplicate.
        Net net = fig41();
        net.inter(D).onReceive(externalAnt(S, T, 74, {S}, D), C);
        const Outcome dup = net.inter(D).onReceive(externalAnt(S, T, 74, {S, M}, D), I);
        c.check(dup.decisions.size() == 1 && dup.decisions[0].action == RouteAction::Drop,
                "the duplicate is dropped (#5 (b))");
        std::set<NodeAddress> targets;
        for (const auto& d : net.inter(D).onQueryDelay(S, 74).decisions)
            if (d.message) targets.insert(d.message->target);
        c.check(targets == std::set<NodeAddress>{K},
                "and M, on its Path, is skipped: D sends to K");
    }
    {
        // No wait where the node can answer: the destination in its zone, or
        // a cached InterRT route; and none at the origin of a query.
        Net net = fig41();
        const Outcome inZone = net.inter(D).onReceive(externalAnt(S, K, 75, {S}, D), C);
        c.check(inZone.querySource == kInvalidAddress && inZone.decisions.size() == 1 &&
                    inZone.decisions[0].message && inZone.decisions[0].message->target == K,
                "the destination in the zone: sent at once");
        const auto origin = net.inter(S).discover(T);
        c.check(!origin.empty() && net.queryWaits.empty(), "the source bordercasts at once");
    }
    {
        // Waits are drawn from the node's IRng: a seed fixes them, another
        // seed changes them (org rule 5). J = 0 draws none.
        auto waits = [](std::uint64_t seed, Time j) {
            Config cfg;
            cfg.queryProcessingDelay = j;
            Net net = fig41(seed, cfg);
            net.send(S, T);
            return std::pair{net.queryWaits, net.inter(S).table().find(T, 0.0) != nullptr};
        };
        const auto a = waits(3, J);
        c.check(!a.first.empty() && a.second && a == waits(3, J), "a fixed seed: the same waits");
        c.check(a.first != waits(4, J).first, "another seed: other waits");
        const auto none = waits(3, 0.0);
        c.check(none.first.empty() && none.second, "J = 0: no wait, and the route is found");
    }
    {
        // Bounded (org rule 7): at most historyCapacity bordercasts wait; past
        // that, a bordercaster sends at once.
        Config cfg;
        cfg.historyCapacity = 1;
        Net net = fig41(1, cfg);
        const Outcome first = net.inter(D).onReceive(externalAnt(S, T, 76, {S}, D), C);
        const Outcome second = net.inter(D).onReceive(externalAnt(S, T, 77, {S}, D), C);
        c.check(first.querySource == S && second.querySource == kInvalidAddress &&
                    !second.decisions.empty() && second.decisions[0].action == RouteAction::Unicast,
                "a second waiting bordercast past the capacity goes at once");
    }
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
    // The retry row pinned: at most one discovery per destination per 3 s
    // (#5 (o)).
    const Time retry = Config{}.discoveryRetryInterval;
    c.check(retry == 3.0, "the retry interval: 3 s");
    Net net = fig41();
    const NodeAddress nowhere = 99;
    const Outcome a = net.send(S, nowhere);
    c.check(a.decisions.size() == 5 && a.decisions.back().action == RouteAction::Queue,
            "four ants (one per peripheral node), then the packet waits");
    const std::uint32_t seq = net.intra(S).lastSeq();
    net.clock().set(retry - 0.1);
    const Outcome b = net.send(S, nowhere);
    c.check(b.decisions.size() == 1 && b.decisions[0].action == RouteAction::Queue &&
                net.intra(S).lastSeq() == seq,
            "2.9 s later: no new discovery");
    net.clock().set(retry);
    const Outcome d = net.send(S, nowhere);
    c.check(d.decisions.size() == 5 && net.intra(S).lastSeq() == seq + 1,
            "3 s later: a new generation");
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
    clock.set(retry);
    z.discover(51);
    c.check(intra.lastSeq() == s1 + 1, "until the old entry is past the interval");
}

void TestZoneNodeMovesAway(Checker& c) {
    // #93, the stale-entry row: a zone node that moved out of the zone, while
    // the neighbour it was reached through stays, leaves the IntraRT after
    // Config::neighbourTimeout() without an ant; its data then starts a route
    // discovery and is delivered over the new path.
    Net net;
    for (auto [a, b] :
         std::vector<std::pair<NodeAddress, NodeAddress>>{{1, 2}, {2, 3}, {2, 4}, {4, 5}, {5, 6}})
        net.link(a, b);
    net.period();
    const auto has = [](const std::vector<NodeAddress>& v, NodeAddress n) {
        return std::find(v.begin(), v.end(), n) != v.end();
    };
    c.check(net.intra(1).table().minHops(3) == 2 && has(net.intra(1).table().peripheral(2), 3),
            "before the move: 3 is a peripheral node of 1, through 2");
    net.cut(2, 3);
    net.link(6, 3);  // node 3 moves: now 1-2-4-5-6-3, five hops from 1

    const Config cfg;
    for (int k = 1; k < cfg.allowedAntLoss; ++k) {
        net.clock().set(k * cfg.internalAntPeriod);
        net.period();
        c.check(net.intra(1).table().minHops(3) == 2,
                "fewer than 3 periods without its ants: still held");
    }
    net.clock().set(cfg.neighbourTimeout());
    net.period();
    c.check(!net.intra(1).table().minHops(3) && !has(net.intra(1).table().zone(), 3) &&
                !has(net.intra(1).table().peripheral(2), 3),
            "three periods without an ant: 3 is out of 1's zone and its peripheral "
            "set");
    c.check(net.intra(1).table().hasNeighbour(2) && net.intra(1).table().minHops(4) == 2,
            "the neighbour it was reached through, and the nodes still there, stay");
    c.check(!net.intra(4).table().minHops(3), "and out of 4's zone, which also held it through 2");

    net.send(1, 3);
    c.check(!net.antsOf(AntType::ExternalForward).empty() &&
                net.antsOf(AntType::ExternalForward)[0].from == 1,
            "a packet for it now starts a route discovery at 1");
    c.check(net.delivered.size() == 1 && net.delivered[0].first == 3,
            "and is delivered over the new path");
    int delivered = 0;
    for (int t = 1; t <= 10; ++t) {
        net.clock().set(cfg.neighbourTimeout() + t);
        net.period();
        net.delivered.clear();
        net.send(1, 3);
        delivered += static_cast<int>(net.delivered.size());
    }
    c.check(delivered == 10, "every later packet is delivered (10 of 10)");
}

void TestZoneNodeStays(Checker& c) {
    // The stale-entry row's other side: a zone node that stays reachable is
    // refreshed every period, so it is never removed, at any phase of the
    // timer against the timeout.
    Net net = fig41();
    std::vector<std::vector<NodeAddress>> zones;
    std::size_t cells = 0;
    for (NodeAddress v = S; v <= T; ++v) {
        zones.push_back(net.intra(v).table().zone());
        cells += net.intra(v).table().size();
    }
    bool same = true;
    for (int t = 1; t <= 10 && same; ++t) {
        net.clock().set(t);
        net.period();
        std::size_t now = 0;
        for (NodeAddress v = S; v <= T; ++v) {
            same = same && net.intra(v).table().zone() == zones[v - S];
            now += net.intra(v).table().size();
        }
        same = same && now == cells;
        // Between two timers, at the timeout boundary of the cells refreshed
        // one period ago and of those refreshed in this one: nothing goes.
        net.clock().set(t + 0.999);
        for (NodeAddress v = S; v <= T; ++v) {
            net.intra(v).purgeStaleNeighbours();
            same = same && net.intra(v).table().zone() == zones[v - S];
        }
    }
    c.check(same, "ten periods on a fixed graph: every zone and every cell as "
                  "after the first");
    net.send(S, D);
    c.check(net.ants.empty() && net.delivered.size() == 1, "in-zone data still needs no ant");
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
    c.check(
        net.delivered.size() == 20 && net.drops[0] == 1 && net.ants.empty(),
        "an intra-zone packet with no neighbour left toward it: dropped, no repair ([W07] §4.3)");
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
    // Sheet review point 2, accepted on #5: Config::maxHopsTravelled().
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
        c.check(dropped(n), "a notification or error ant with a one-node Path");
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
