// Copyright (c) 2026 Daniel Joppi (https://github.com/swarm-routing)
// SPDX-License-Identifier: MIT OR GPL-2.0-only
//
// [W07] §4.3's local repair against readings (i)-(l), accepted on #5 on
// 2026-11-11: the repair ant (i), the notification ant and the spliced route
// (j), the invalid routes the ants remove (k), the sources told (l); plus the
// repair wait, the buffer bound, a repair at a transit relay ((m), accepted
// 2026-11-11) and determinism.
#include "hopnet-net.h"

namespace hopnet::core::test {

namespace {

/// `z` stores the InterRT route `path` (z's node first) through a backward ant
/// with SeqNo `seq` and path time `time`, received from `from`.
void storeRoute(InterZone& z, std::vector<NodeAddress> path, std::uint32_t seq, NodeAddress from,
                double time = 0.02) {
    Ant back{.type = AntType::Backward,
             .source = path.back(),
             .destination = path.front(),
             .seq = seq,
             .time = time,
             .path = VisitedPath(100),
             .target = path.front(),
             .travelled = 3};
    double t = 0.0;
    for (NodeAddress n : path)
        back.path.push(n, t += time / static_cast<double>(path.size()));
    z.onReceive(back, from);
}

/// Gives `z` a cell (via, p) with hops 2: p becomes a peripheral node.
void zoneNode(IntraZone& z, NodeAddress p, NodeAddress via, double time = 0.004) {
    Ant a{.type = AntType::InternalForward,
          .source = p,
          .seq = 1,
          .hops = 1,
          .time = time,
          .path = VisitedPath(100)};
    a.path.push(p, time / 2);
    a.path.push(via, time);
    z.onReceive(a, via);
}

/// A notification or error ant from `m` to `src`, routed to `target`.
Ant repairAnt(AntType type, NodeAddress m, NodeAddress src, std::uint32_t seq,
              std::initializer_list<NodeAddress> path, NodeAddress target) {
    Ant a{.type = type,
          .source = m,
          .destination = src,
          .seq = seq,
          .time = 0.01,
          .path = VisitedPath(100),
          .target = target,
          .travelled = 1};
    for (NodeAddress n : path)
        a.path.push(n, 0.0);
    return a;
}

std::vector<NodeAddress> routeOf(Net& net, NodeAddress src, NodeAddress dst, Time now = 0.0) {
    const InterRoute* r = net.inter(src).table().find(dst, now);
    return r ? r->path : std::vector<NodeAddress>{};
}

bool hasSegment(const std::vector<NodeAddress>& v, NodeAddress a, NodeAddress b) {
    for (std::size_t i = 0; i + 1 < v.size(); ++i)
        if (v[i] == a && v[i + 1] == b) return true;
    return false;
}

/// The routes these tests pin are the discoveries' without the
/// query-processing delay (#5 (t)): with it, the bordercasters' race gives A
/// another route (through S), as valid, that the expected ants do not follow.
Config noWait() {
    Config cfg;
    cfg.queryProcessingDelay = 0.0;
    return cfg;
}

}  // namespace

void TestRepairSuccess(Checker& c) {
    // #5 (i), (j) on Fig. 4.1: M loses its last neighbour toward U under a
    // packet from S to T.
    Net net = fig41();
    net.send(S, T);
    c.check(routeOf(net, S, T) == std::vector<NodeAddress>{S, D, M, U, T}, "S's route via M, U");
    const std::uint32_t mSeq = net.intra(M).lastSeq();
    net.cut(M, O);
    net.ants.clear();
    net.delivered.clear();
    net.send(S, T);

    bool repairAnt = false;
    for (const auto& s : net.antsOf(AntType::ExternalForward))
        repairAnt |= s.from == M && s.ant.source == M && s.ant.seq == mSeq + 1 &&
                     s.ant.destination == T && s.ant.path.hops().front().node == M;
    c.check(repairAnt, "(i): M bordercasts an external forward ant of its own, Source M, M's seq");
    c.check(!net.intra(M).table().hasNeighbour(O), "the failed neighbour's row went");
    c.check(!net.inter(M).repairing(T), "the backward ant ended the repair");
    const InterRoute* r = net.inter(M).table().find(T, 0.0);
    c.check(r && r->path == std::vector<NodeAddress>{M, R, T} && r->replaces == U,
            "M stores a record for T that replaces the segment toward U");
    c.check(net.delivered.size() == 1 && net.delivered[0].first == T &&
                net.delivered[0].second.route == std::vector<NodeAddress>{S, D, M, R, T},
            "the buffered packet leaves on its prefix <S, D, M> spliced onto M's record");

    const auto notes = net.antsOf(AntType::Notification);
    std::set<NodeAddress> targets;
    int fromM = 0;
    bool reachedS = false;
    for (const auto& s : notes) {
        fromM += s.from == M;
        targets.insert(s.ant.target);
        reachedS |= s.to == S;
        c.check(s.ant.source == M && s.ant.destination == S && r && s.ant.seq == r->seq &&
                    nodesOf(s.ant.path) == std::vector<NodeAddress>{S, D, M, R, T},
                "(j): Source M, Destination S, the record's SeqNo, the spliced route");
    }
    c.check(fromM == 1 && reachedS && targets == std::set<NodeAddress>{D, S},
            "one notification ant, back over S's prefix <M, D, S>");
    const InterRoute* atS = net.inter(S).table().find(T, 0.0);
    c.check(atS && atS->path == std::vector<NodeAddress>{S, D, M, R, T} && r && atS->seq == r->seq,
            "S replaces its record with the notification's Path");
    c.check(net.antsOf(AntType::Error).empty(), "no error ant");

    net.ants.clear();
    net.delivered.clear();
    net.send(S, T);
    c.check(net.ants.empty() && net.delivered.size() == 1 &&
                net.delivered[0].second.route == std::vector<NodeAddress>{S, D, M, R, T},
            "S's next packet takes the new route at once");
}

void TestNoPheromoneIsLinkFailure(Checker& c) {
    // #5 (r), accepted 2026-11-13: inter-zone data that finds no neighbour
    // with pheromone toward its next Path node is handled as a data
    // transmission that failed with no other neighbour (#5 (e)), not dropped
    // ([W07] p. 34). The radio links stay; only the node's IntraRT loses them.
    {
        // At a Path node: M lost its row for O, its only way to U.
        Net net = fig41();
        net.send(S, T);
        const std::uint32_t mSeq = net.intra(M).lastSeq();
        net.intra(M).onLinkFailure(O);
        net.ants.clear();
        net.delivered.clear();
        net.send(S, T);
        bool repairAnt = false;
        for (const auto& s : net.antsOf(AntType::ExternalForward))
            repairAnt |= s.from == M && s.ant.source == M && s.ant.seq == mSeq + 1;
        c.check(repairAnt && net.drops[M] == 0, "M starts a local repair instead of dropping");
        c.check(net.delivered.size() == 1 &&
                    net.delivered[0].second.route == std::vector<NodeAddress>{S, D, M, R, T},
                "and the packet arrives on the repaired route");
    }
    {
        // At a transit relay: I, between D and M, lost its row for M.
        Net net = fig41();
        net.send(S, T);
        net.intra(I).onLinkFailure(M);
        net.cut(I, M);  // and the repair cannot go that way either
        net.cut(D, J);
        net.cut(D, L);
        net.delivered.clear();
        net.send(S, T);
        c.check(net.inter(I).repairing(T) && net.queued[I].size() == 1 && net.drops[I] == 0,
                "I repairs ((m)) and buffers the packet");
        net.advance(1.0);
        bool error = false;
        for (const auto& s : net.antsOf(AntType::Error))
            error |= s.from == I && s.ant.destination == S;
        c.check(error && net.discarded[I] == 1, "no backward ant: S gets the error ant");
    }
    {
        // At the source: S lost its row for C, its only way to D.
        Config cfg;
        cfg.discoveryRetryInterval = 0.5;
        Net net = fig41(1, cfg);
        net.send(S, T);
        net.intra(S).onLinkFailure(C);
        net.clock().set(0.5);
        const std::uint32_t seq = net.intra(S).lastSeq();
        const Outcome o = net.send(S, T);
        c.check(o.decisions.back().action == RouteAction::Queue && net.drops[S] == 0 &&
                    net.intra(S).lastSeq() == seq + 1 && !net.inter(S).table().find(T, 0.5),
                "S's record goes, a discovery starts and the packet waits");
    }
    {
        // Intra-zone data carries no route: still dropped (p. 34). C, on S's
        // way to D, lost its row for D.
        Net net = fig41();
        net.intra(C).onLinkFailure(D);
        net.send(S, D);
        c.check(net.drops[C] == 1 && net.delivered.empty() && !net.inter(C).repairing(D),
                "intra-zone data with no pheromone is dropped");
    }
    {
        // With the switch off, the p. 34 drop.
        Config cfg;
        cfg.noPheromoneIsLinkFailure = false;
        Net net = fig41(1, cfg);
        net.send(S, T);
        net.intra(M).onLinkFailure(O);
        net.delivered.clear();
        net.send(S, T);
        c.check(net.drops[M] == 1 && net.delivered.empty() && !net.inter(M).repairing(T),
                "noPheromoneIsLinkFailure = false: M drops it");
        net.intra(S).onLinkFailure(C);
        const Outcome o = net.send(S, T);
        c.check(o.decisions.size() == 1 && o.decisions[0].action == RouteAction::Drop &&
                    net.inter(S).table().find(T, 0.0),
                "and so does the source, keeping its record");
    }
}

void TestRepairFailure(Checker& c) {
    // #5 (i), (l): T unreachable from M. After the wait (1 s, a transit M has
    // no estimate) each buffered source gets one error ant.
    {
        Net net = fig41(1, noWait());
        net.send(S, T);
        net.send(A, T);
        c.check(hasSegment(routeOf(net, A, T), M, U), "A's route via M, U too");
        net.cut(M, O);
        net.cut(P, R);  // M's ant toward R is lost on the way
        net.ants.clear();
        net.sendAll({{S, T}, {A, T}});
        c.check(net.inter(M).repairing(T) && net.queued[M].size() == 2,
                "both packets wait at M while it repairs");
        net.advance(0.99);
        c.check(net.inter(M).repairing(T) && net.antsOf(AntType::Error).empty(),
                "not before the wait");
        net.advance(1.0);
        std::map<NodeAddress, std::vector<NodeAddress>> errors;  // per source, from M
        std::set<NodeAddress> reached;
        for (const auto& s : net.antsOf(AntType::Error)) {
            if (s.from == M) errors[s.ant.destination] = nodesOf(s.ant.path);
            if (s.to == s.ant.destination) reached.insert(s.to);
        }
        c.check(errors.size() == 2 && errors[S] == std::vector<NodeAddress>{S, D, M, U, T} &&
                    errors[A] == std::vector<NodeAddress>{A, D, M, U, T},
                "one error ant per source, carrying its packet's old route");
        c.check(reached == std::set<NodeAddress>{S, A}, "both reach their source");
        c.check(!net.inter(M).repairing(T) && net.discarded[M] == 2 && net.queued[M].empty(),
                "the buffered packets are discarded");
        c.check(!net.inter(S).table().find(T, 1.0) && !net.inter(A).table().find(T, 1.0),
                "both sources' records for T are gone");
        c.check(net.antsOf(AntType::Notification).empty(), "no notification ant");
    }
    {
        // A repair with no peripheral node to send to fails at once.
        FakeClock clock;
        FakeLinkState link;
        SeededRng rng(1);
        IntraZone intra(Config{}, M, clock, link, rng);
        InterZone z(Config{}, intra, clock);
        neighbours(intra, {D, U});
        DataHeader h{
            .source = S, .destination = T, .target = U, .travelled = 3, .route = {S, D, M, U, T}};
        const Outcome o = z.onDataSendFailure(h, U);
        c.check(o.decisions.size() == 2 && o.decisions[0].action == RouteAction::Unicast &&
                    o.decisions[0].nextHop == D && o.decisions[0].message->type == AntType::Error &&
                    o.decisions[0].message->target == D &&
                    o.decisions[0].message->destination == S &&
                    o.decisions[1].action == RouteAction::Drop,
                "no peripheral node: the error ant at once, and the packet is dropped");
        c.check(!z.repairing(T) && o.repairFor == kInvalidAddress, "no repair runs");
    }
    {
        // The wait: 5 x the lost path's estimated delay when M's own record
        // runs through the lost segment; that record goes.
        FakeClock clock;
        FakeLinkState link;
        SeededRng rng(1);
        IntraZone intra(Config{}, M, clock, link, rng);
        InterZone z(Config{}, intra, clock);
        neighbours(intra, {D, U, P});
        zoneNode(intra, R, P);
        storeRoute(z, {M, U, T}, 4, U, 0.02);
        DataHeader h{
            .source = S, .destination = T, .target = U, .travelled = 3, .route = {S, D, M, U, T}};
        const Outcome o = z.onDataSendFailure(h, U);
        c.check(o.repairFor == T && std::fabs(o.repairWait - 0.1) < 1e-12 &&
                    o.decisions.back().action == RouteAction::Queue && !z.table().find(T, 0.0),
                "a 0.02 s estimate gives a 0.1 s wait, and the record through U goes");
        c.check(o.querySource == kInvalidAddress && o.decisions.front().message &&
                    o.decisions.front().message->target == R,
                "the repair's own ant leaves at once: its origin never waits (#5 (t))");
        clock.set(0.05);
        c.check(z.onRepairTimeout(T).decisions.empty() && z.repairing(T),
                "an early timer does nothing");
        clock.set(0.1);
        const Outcome late = z.onRepairTimeout(T);
        c.check(!z.repairing(T) && late.decisions.back().action == RouteAction::DiscardPending &&
                    late.decisions.back().nextHop == T,
                "the wait over: the buffered packets for T are discarded");
        c.check(z.onRepairTimeout(T).decisions.empty(), "a second timer finds nothing");
    }
    {
        // At the source the repair is its own discovery (#5 (i)): no
        // notification or error ant.
        Net net = fig41();
        net.send(S, T);
        net.cut(S, C);
        net.ants.clear();
        const Time retry = Config{}.discoveryRetryInterval;
        net.advance(retry);  // past the discovery retry interval
        const std::uint32_t seq = net.intra(S).lastSeq();
        net.send(S, T);
        c.check(!net.inter(S).table().find(T, retry) && net.queued[S].size() == 1 &&
                    net.intra(S).lastSeq() == seq + 1,
                "S drops its record through C, discovers again and holds the packet");
        c.check(net.antsOf(AntType::Notification).empty() && net.antsOf(AntType::Error).empty() &&
                    !net.inter(S).repairing(T),
                "no repair state, no notification or error ant");
    }
}

void TestRepairSources(Checker& c) {
    // #5 (l): every distinct buffered source is told once; a source that
    // arrives after the success is re-routed by M's record and told nothing.
    Net net = fig41(1, noWait());
    net.send(S, T);
    net.send(A, T);
    net.send(F, T);
    c.check(hasSegment(routeOf(net, A, T), M, U) && hasSegment(routeOf(net, F, T), M, U),
            "A's and F's routes run through M, U");
    net.cut(M, O);
    net.ants.clear();
    net.delivered.clear();
    net.sendAll({{S, T}, {A, T}});
    std::map<NodeAddress, int> told;
    for (const auto& s : net.antsOf(AntType::Notification))
        if (s.from == M) ++told[s.ant.destination];
    c.check(told == std::map<NodeAddress, int>{{S, 1}, {A, 1}}, "S and A get one ant each");
    c.check(net.delivered.size() == 2, "both buffered packets arrive");
    c.check(routeOf(net, S, T) == std::vector<NodeAddress>{S, D, M, R, T} &&
                routeOf(net, A, T) == std::vector<NodeAddress>{A, D, M, R, T},
            "each source's record is its own prefix on M's new route");

    std::vector<NodeAddress> fRoute = routeOf(net, F, T);
    net.ants.clear();
    net.delivered.clear();
    net.send(F, T);
    const auto at = std::find(fRoute.begin(), fRoute.end(), M);
    std::vector<NodeAddress> expect(fRoute.begin(), at + 1);
    expect.push_back(R);
    expect.push_back(T);
    c.check(net.delivered.size() == 1 && net.delivered[0].second.route == expect,
            "F's packet on the old route is re-routed by M's record at M");
    c.check(net.ants.empty(), "and F gets no ant");

    // The sources told are bounded by the buffer's size (org rule 7).
    Config cfg;
    cfg.repairBufferCapacity = 1;
    Net small = fig41(1, cfg);
    small.send(S, T);
    small.send(A, T);
    small.cut(M, O);
    small.ants.clear();
    small.delivered.clear();
    small.sendAll({{S, T}, {A, T}});
    std::set<NodeAddress> smallTold;
    for (const auto& s : small.antsOf(AntType::Notification))
        if (s.from == M) smallTold.insert(s.ant.destination);
    c.check(smallTold == std::set<NodeAddress>{S} && small.delivered.size() == 2,
            "with room for one source only the first is told; both packets still arrive");
}

void TestNotificationInvalid(Checker& c) {
    // #5 (k) at a visited node, here D on the way back to S.
    FakeClock clock;
    FakeLinkState link;
    SeededRng rng(1);
    IntraZone intra(Config{}, D, clock, link, rng);
    InterZone z(Config{}, intra, clock);
    neighbours(intra, {C, I});
    zoneNode(intra, S, C);
    storeRoute(z, {D, M, U, T}, 5, I);
    storeRoute(z, {D, M, U, 40}, 5, I);
    const Ant note = repairAnt(AntType::Notification, M, S, 50, {S, D, M, R, T}, D);
    const Outcome o = z.onReceive(note, I);
    c.check(o.decisions.size() == 1 && o.decisions[0].action == RouteAction::Unicast &&
                o.decisions[0].nextHop == C && o.decisions[0].message->target == S,
            "a Path node passes it on toward the Path node before it");
    c.check(!z.table().find(T, 0.0), "a record for T through M on the old route goes");
    c.check(z.table().find(40, 0.0) != nullptr, "a record for another destination stays");
    storeRoute(z, {D, M, R, T}, 60, I);
    z.onReceive(note, I);
    c.check(z.table().find(T, 0.0) != nullptr, "a record on the new route stays");
    const Ant err = repairAnt(AntType::Error, M, S, 9, {S, D, M, U, T}, D);
    z.onReceive(err, I);
    c.check(!z.table().find(T, 0.0) && z.table().find(40, 0.0),
            "an error ant removes every record for T through M, and only for T");

    // Transit relays check too.
    IntraZone intraC(Config{}, C, clock, link, rng);
    InterZone zc(Config{}, intraC, clock);
    neighbours(intraC, {S, D});
    storeRoute(zc, {C, D, M, U, T}, 5, D);
    const Outcome relayed =
        zc.onReceive(repairAnt(AntType::Notification, M, S, 50, {S, D, M, R, T}, S), D);
    c.check(relayed.decisions[0].action == RouteAction::Unicast &&
                relayed.decisions[0].nextHop == S && !zc.table().find(T, 0.0),
            "a transit relay removes its invalid record and relays the ant");

    // At the source: the SeqNo rule.
    IntraZone intraS(Config{}, S, clock, link, rng);
    InterZone zs(Config{}, intraS, clock);
    neighbours(intraS, {C});
    storeRoute(zs, {S, D, M, U, T}, 10, C);
    const Ant toS = repairAnt(AntType::Notification, M, S, 50, {S, D, M, R, T}, S);
    const Outcome atS = zs.onReceive(toS, C);
    c.check(atS.routeReady == T &&
                zs.table().find(T, 0.0)->path == std::vector<NodeAddress>{S, D, M, R, T},
            "S replaces an older record with the notification's Path");
    storeRoute(zs, {S, D, K, T}, 70, C);
    zs.onReceive(toS, C);
    c.check(zs.table().find(T, 0.0)->path == std::vector<NodeAddress>{S, D, K, T},
            "S keeps a newer generation over the notification");
    zs.onReceive(repairAnt(AntType::Error, M, S, 9, {S, D, M, U, T}, S), C);
    c.check(zs.table().find(T, 0.0) != nullptr, "an error ant leaves a record not through M");
    storeRoute(zs, {S, D, M, U, 40}, 5, C);
    zs.onReceive(repairAnt(AntType::Error, M, S, 9, {S, D, M, U, 40}, S), C);
    c.check(!zs.table().find(40, 0.0), "and removes one through M");

    // Malformed repair ants die without touching a record.
    storeRoute(zs, {S, D, M, U, 41}, 5, C);
    auto dropped = [&](const Ant& a) {
        const Outcome x = zs.onReceive(a, C);
        return x.decisions.size() == 1 && x.decisions[0].action == RouteAction::Drop;
    };
    c.check(dropped(repairAnt(AntType::Error, M, S, 9, {D, M, U, 41}, S)),
            "a Path that does not start at its destination");
    c.check(dropped(repairAnt(AntType::Error, M, S, 9, {S}, S)), "a Path with one node");
    c.check(dropped(repairAnt(AntType::Error, M, S, 9, {S, D, M}, S)),
            "a Path that ends at its Source");
    c.check(dropped(repairAnt(AntType::Notification, M, S, 9, {S, D, M, R, 41}, kInvalidAddress)),
            "no target");
    c.check(zs.table().find(41, 0.0) != nullptr, "no malformed ant removed a record");
}

void TestSpliceLoop(Checker& c) {
    // #5 (j): the joined route is loop-erased.
    const Splice plain = spliceRoute({S, D, M}, {M, R, T});
    c.check(plain.route == std::vector<NodeAddress>{S, D, M, R, T} && plain.junction == 2,
            "no loop: the prefix, then the repaired part; M at the junction");
    const Splice loop = spliceRoute({S, D, M}, {M, D, K, T});
    c.check(loop.route == std::vector<NodeAddress>{S, D, K, T} && loop.junction == 1,
            "a repaired part that revisits D drops <M> between D's two visits");
    const Splice deep = spliceRoute({1, 2, 3, 4}, {4, 5, 2, 6});
    c.check(deep.route == std::vector<NodeAddress>{1, 2, 6} && deep.junction == 1,
            "a loop back to an earlier prefix node closes there");
    const Splice inner = spliceRoute({1, 2, 3}, {3, 4, 5, 4, 6});
    c.check(inner.route == std::vector<NodeAddress>{1, 2, 3, 4, 6} && inner.junction == 2,
            "a loop inside the repaired part is erased too");

    // In the network: a junction before M sends the packet and the
    // notification ant back to it. M's new route runs back through D.
    {
        FakeClock clock;
        FakeLinkState link;
        SeededRng rng(1);
        IntraZone intra(Config{}, M, clock, link, rng);
        InterZone z(Config{}, intra, clock);
        neighbours(intra, {I, U});
        zoneNode(intra, D, I);
        DataHeader h{
            .source = S, .destination = T, .target = U, .travelled = 3, .route = {S, D, M, U, T}};
        z.onDataSendFailure(h, U);
        storeRoute(z, {M, D, K, T}, 7, I);
        DataHeader again = h;
        const Outcome o = z.resume(again);
        c.check(again.route == std::vector<NodeAddress>{S, D, K, T} && again.target == D &&
                    o.decisions[0].action == RouteAction::Unicast && o.decisions[0].nextHop == I,
                "the packet takes the loop-erased route, back to D first");
    }

    // Past the Path cap the source gets the error ant instead.
    Config cfg;
    cfg.maxPathLength = 5;
    FakeClock clock;
    FakeLinkState link;
    SeededRng rng(1);
    IntraZone intra(cfg, 9, clock, link, rng);
    InterZone z(cfg, intra, clock);
    neighbours(intra, {2, 4, 5});
    zoneNode(intra, 7, 5);
    DataHeader h{
        .source = 1, .destination = 30, .target = 4, .travelled = 3, .route = {1, 2, 9, 4, 30}};
    const Outcome start = z.onDataSendFailure(h, 4);
    c.check(start.repairFor == 30 && start.repairWait == 1.0 && start.decisions.size() == 2 &&
                start.decisions[0].nextHop == 5 && start.decisions[0].message->target == 7 &&
                start.decisions[1].action == RouteAction::Queue && z.repairing(30),
            "a transit M with no estimate waits 1 s; one repair ant to its peripheral node");
    Ant back{.type = AntType::Backward,
             .source = 30,
             .destination = 9,
             .seq = 3,
             .time = 0.03,
             .path = VisitedPath(100),
             .target = 9,
             .travelled = 3};
    for (NodeAddress n : {9, 7, 8, 30})
        back.path.push(n, 0.01);
    const Outcome done = z.onReceive(back, 5);
    c.check(
        done.routeReady == 30 && done.decisions.size() == 1 &&
            done.decisions[0].action == RouteAction::Unicast && done.decisions[0].nextHop == 2 &&
            done.decisions[0].message->type == AntType::Error &&
            nodesOf(done.decisions[0].message->path) == std::vector<NodeAddress>{1, 2, 9, 4, 30},
        "<1, 2, 9> + <9, 7, 8, 30> passes the cap of 5: an error ant, not a notification");
    DataHeader resumed = h;
    c.check(z.resume(resumed).decisions[0].action == RouteAction::Drop,
            "and the buffered packet cannot take the route either");
}

void TestRepairAtTransitRelay(Checker& c) {
    // (m), accepted on #5 (2026-11-11): the failure happens at
    // I, a transit relay between Path nodes D and M. I's prefix is <S, D, I>,
    // and its error ant names the segment <D, I, M>; S's record holds D, M
    // consecutively, so it goes.
    Net net = fig41();
    net.send(S, T);
    net.cut(I, M);  // the component of T is now out of reach
    net.ants.clear();
    net.send(S, T);
    c.check(net.inter(I).repairing(T) && net.queued[I].size() == 1, "I repairs and buffers");
    net.advance(1.0);
    bool fromI = false;
    for (const auto& s : net.antsOf(AntType::Error))
        fromI |= s.from == I && s.ant.target == D && s.ant.destination == S &&
                 nodesOf(s.ant.path) == std::vector<NodeAddress>{S, D, I, M, U, T};
    c.check(fromI, "the error ant carries the old route with I where it relayed");
    c.check(!net.inter(S).table().find(T, 1.0) && net.discarded[I] == 1,
            "S's record through <D, M> goes; I discards the packet");
}

void TestRepairDeterminism(Checker& c) {
    auto run = [](std::uint64_t seed) {
        Net net = fig41(seed);
        net.send(S, T);
        net.send(A, T);
        net.cut(M, O);
        net.sendAll({{S, T}, {A, T}});
        net.cut(P, R);
        net.advance(1.0);
        net.sendAll({{S, T}, {A, T}});
        net.advance(2.5);
        std::vector<std::int64_t> out;
        for (const auto& s : net.ants) {
            out.push_back(s.from);
            out.push_back(s.to);
            out.push_back(static_cast<std::int64_t>(s.ant.type));
            out.push_back(s.ant.target);
        }
        for (const auto& [f, t] : net.dataHops) {
            out.push_back(f);
            out.push_back(t);
        }
        out.push_back(static_cast<std::int64_t>(net.delivered.size()));
        return out;
    };
    c.check(run(11) == run(11), "a fixed seed gives the same repair, ants and hops (org rule 5)");
}

}  // namespace hopnet::core::test
