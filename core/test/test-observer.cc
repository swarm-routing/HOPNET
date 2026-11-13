// Copyright (c) 2026 Daniel Joppi (https://github.com/swarm-routing)
// SPDX-License-Identifier: MIT OR GPL-2.0-only
//
// The core's reports to swarm-core's IRouterObserver (#40): every callback
// driven with a recording observer, the byte counts against the wire encoding,
// route adds and removes balanced against the tables, the two tables'
// footprint, and the decisions identical with and without an observer.
#include "../hopnet-observe.h"
#include "hopnet-net.h"

#include <algorithm>
#include <tuple>

namespace hopnet::core::test {

namespace {

using swarm::core::IRouterObserver;
using swarm::core::KindName;

/// Keeps every event, in order.
class Recorder : public IRouterObserver {
public:
    struct Control {
        std::uint8_t kind;
        bool broadcast;
        std::uint32_t bytes;
    };
    struct Route {
        NodeAddress dest, nextHop;
        bool added;
        bool operator==(const Route&) const = default;
    };
    std::vector<Control> sent;
    std::vector<std::pair<std::uint8_t, std::uint32_t>> received;
    std::vector<Route> routes;
    std::vector<std::pair<NodeAddress, RouteAction>> data;
    std::vector<std::pair<std::uint32_t, std::uint32_t>> states;

    void onControlSent(std::uint8_t kind, bool broadcast, std::uint32_t bytes) override {
        sent.push_back({kind, broadcast, bytes});
    }
    void onControlReceived(std::uint8_t kind, std::uint32_t bytes) override {
        received.emplace_back(kind, bytes);
    }
    void onRouteChanged(NodeAddress dest, NodeAddress nextHop, bool added) override {
        routes.push_back({dest, nextHop, added});
    }
    void onDataDecision(NodeAddress dest, RouteAction action) override {
        data.emplace_back(dest, action);
    }
    void onStateSize(std::uint32_t entries, std::uint32_t bytes) override {
        states.emplace_back(entries, bytes);
    }

    std::size_t sentOf(AntType t) const {
        return static_cast<std::size_t>(
            std::count_if(sent.begin(), sent.end(), [&](const Control& e) {
                return e.kind == static_cast<std::uint8_t>(t);
            }));
    }
    std::size_t receivedOf(AntType t) const {
        return static_cast<std::size_t>(
            std::count_if(received.begin(), received.end(),
                          [&](const auto& e) { return e.first == static_cast<std::uint8_t>(t); }));
    }
    std::size_t decisions(RouteAction a) const {
        return static_cast<std::size_t>(
            std::count_if(data.begin(), data.end(), [&](const auto& e) { return e.second == a; }));
    }
    long balance() const {  // adds minus removes
        long n = 0;
        for (const auto& r : routes)
            n += r.added ? 1 : -1;
        return n;
    }
};

bool hasRoute(const Recorder& r, NodeAddress dest, NodeAddress next, bool added) {
    return std::find(r.routes.begin(), r.routes.end(), Recorder::Route{dest, next, added}) !=
           r.routes.end();
}

/// The nodes of fig41, S..T.
std::vector<NodeAddress> nodesFig41() {
    std::vector<NodeAddress> out;
    for (NodeAddress n = S; n <= T; ++n)
        out.push_back(n);
    return out;
}

/// What the tables of every node hold: IntraRT cells plus InterRT records.
long tableEntries(Net& net) {
    long n = 0;
    for (NodeAddress v : nodesFig41())
        n += static_cast<long>(net.intra(v).table().size() + net.inter(v).table().size());
    return n;
}

/// Everything a run did, to compare two runs.
struct Trace {
    std::vector<std::tuple<NodeAddress, NodeAddress, Ant>> ants;
    std::vector<std::pair<NodeAddress, DataHeader>> delivered;
    std::vector<std::pair<NodeAddress, NodeAddress>> dataHops;
    std::map<NodeAddress, int> drops, discarded;
    std::map<NodeAddress, std::vector<DataHeader>> queued;
    std::vector<std::uint32_t> seqs;
    bool operator==(const Trace&) const = default;
};

Trace traceOf(Net& net) {
    Trace t;
    for (const auto& s : net.ants)
        t.ants.emplace_back(s.from, s.to, s.ant);
    t.delivered = net.delivered;
    t.dataHops = net.dataHops;
    t.drops = net.drops;
    t.discarded = net.discarded;
    t.queued = net.queued;
    for (NodeAddress v : nodesFig41())
        t.seqs.push_back(net.intra(v).lastSeq());
    return t;
}

/// Discovery, a link lost under traffic, a local repair, a repair that fails
/// (error ants, discarded packets), and data in the zone.
void scenario(Net& net) {
    net.send(S, T);
    net.send(A, T);
    net.send(S, C);
    net.cut(M, O);
    net.sendAll({{S, T}, {A, T}});
    net.cut(P, R);
    net.advance(1.0);
    net.sendAll({{S, T}, {A, T}});
    net.advance(2.5);
    net.period();
}

}  // namespace

void TestKindNames(Checker& c) {
    const auto names = kindNames();
    c.check(names.size() == 5, "one name per ant type");
    const char* expect[] = {"InternalForward", "ExternalForward", "Backward", "Notification",
                            "Error"};
    for (std::uint8_t k = 0; k < 5; ++k) {
        const char* n = swarm::core::findKindName(names, k);
        c.check(n && std::string(n) == expect[k], "the wire type byte names its ant type");
        c.check(kindOf(Ant{.type = static_cast<AntType>(k)}) == k, "kind = the wire type byte");
    }
    c.check(swarm::core::findKindName(names, 5) == nullptr, "an unknown byte has no name");
    // The table agrees with the wire: the byte after the version is the type.
    for (std::uint8_t k = 0; k < 5; ++k) {
        std::vector<std::uint8_t> bytes;
        encode(Ant{.type = static_cast<AntType>(k)}, bytes);
        c.check(bytes.size() > 1 && bytes[1] == k, "the encoded type byte is the kind");
    }
}

void TestObserverIntraZone(Checker& c) {
    FakeClock clock;
    FakeLinkState link;
    SeededRng rng(1);
    Recorder rec;
    IntraZone z(Config{}, 5, clock, link, rng, &rec);

    // onControlSent: the timer's internal forward ant, a broadcast of its
    // encoded size.
    const Decision d = z.onInternalAntTimer();
    c.check(d.message.has_value(), "the timer sends an ant");
    c.check(rec.sent.size() == 1 && rec.sent[0].kind == 0 && rec.sent[0].broadcast &&
                rec.sent[0].bytes == encodedSize(*d.message),
            "onControlSent: an internal forward ant, broadcast, its encoded size");
    c.check(rec.received.empty() && rec.routes.empty() && rec.states.empty(),
            "nothing else happened: an empty footprint is not reported");

    // onControlReceived, onRouteChanged, onStateSize: a neighbour's internal
    // forward ant with its hop budget spent turns into a backward ant.
    Ant fwd{.type = AntType::InternalForward,
            .source = 7,
            .seq = 1,
            .hops = 1,
            .time = 0.01,
            .path = VisitedPath(100)};
    fwd.path.push(7, 0.01);
    const Decision back = z.onReceive(fwd, 7);
    c.check(back.message && back.action == swarm::core::RouteAction::Unicast, "a backward ant");
    c.check(rec.received.size() == 1 && rec.received[0].first == 0 &&
                rec.received[0].second == encodedSize(fwd),
            "onControlReceived: kind and encoded size");
    c.check(rec.sent.size() == 2 && rec.sent[1].kind == 2 && !rec.sent[1].broadcast &&
                rec.sent[1].bytes == encodedSize(*back.message),
            "onControlSent: the backward ant, unicast");
    c.check(rec.routes.size() == 1 && rec.routes[0] == Recorder::Route{7, 7, true},
            "onRouteChanged: the cell (zone node 7, neighbour 7) added");
    c.check(rec.states.size() == 1 && rec.states[0].first == 1 &&
                rec.states[0].second == z.table().bytes() && rec.states[0].second > 0,
            "onStateSize: one cell and its bytes");

    // A repeated generation changes nothing: no route event, no new footprint.
    z.onReceive(fwd, 7);
    c.check(rec.routes.size() == 1 && rec.states.size() == 1, "a repeated seq is not news");
    c.check(rec.receivedOf(AntType::InternalForward) == 2, "but the ant was received");

    // A second cell for the same neighbour grows the footprint.
    Ant far{.type = AntType::InternalForward,
            .source = 9,
            .seq = 2,
            .hops = 1,
            .time = 0.02,
            .path = VisitedPath(100)};
    far.path.push(9, 0.01);
    far.path.push(7, 0.02);
    z.onReceive(far, 7);
    c.check(rec.states.size() == 2 && rec.states[1].first == 2 &&
                rec.states[1].second == z.table().bytes(),
            "a second cell: the footprint grows");

    // The backward ant of an own forward ant, then a lost link removes both.
    Ant bwd{.type = AntType::Backward,
            .source = 7,
            .destination = 5,
            .seq = 3,
            .hops = 1,
            .time = 0.02,
            .path = VisitedPath(100)};
    bwd.path.push(5, 0.0);
    bwd.path.push(7, 0.02);
    z.onReceive(bwd, 7);  // path.back() is 7, not self: a malformed backward ant
    rec.routes.clear();
    rec.states.clear();
    z.onLinkFailure(7);
    c.check(rec.routes.size() == 2 && !rec.routes[0].added && !rec.routes[1].added &&
                hasRoute(rec, 7, 7, false) && hasRoute(rec, 9, 7, false),
            "onRouteChanged: both cells of the lost neighbour removed");
    c.check(rec.states.size() == 1 &&
                rec.states[0] == std::pair<std::uint32_t, std::uint32_t>(0, 0),
            "onStateSize: the table is empty again");

    // Silent neighbours are purged after the timeout, each with its route events.
    z.onReceive(fwd, 7);
    rec.routes.clear();
    rec.states.clear();
    clock.set(Config{}.neighbourTimeout() + 1.0);
    z.onInternalAntTimer();  // purges first
    c.check(rec.routes.size() == 1 && !rec.routes[0].added, "a stale neighbour's cell removed");
    c.check(!rec.states.empty() && rec.states.back().first == 0, "and the footprint shrinks");

    // Ants of the other half are not this half's to report.
    rec.sent.clear();
    rec.received.clear();
    Ant ext = externalAnt(7, 8, 1, {7}, 5);
    z.onReceive(ext, 7);
    c.check(rec.sent.empty() && rec.received.empty(), "an external ant is the inter-zone half's");
}

void TestObserverControl(Checker& c) {
    Recorder rec;
    Net net = fig41(1, Config{}, &rec);
    c.check(rec.sentOf(AntType::InternalForward) >= nodesFig41().size(),
            "every node originates one internal forward ant per period, and the zone-limited "
            "flood re-broadcasts them");
    c.check(
        std::all_of(rec.sent.begin(), rec.sent.end(), [](const auto& e) { return e.bytes > 1; }) &&
            std::all_of(rec.received.begin(), rec.received.end(),
                        [](const auto& e) { return e.second > 1; }),
        "every report carries the encoded size");
    c.check(std::all_of(rec.sent.begin(), rec.sent.end(),
                        [](const auto& e) {
                            return e.broadcast ==
                                   (e.kind == static_cast<std::uint8_t>(AntType::InternalForward));
                        }),
            "internal forward ants are broadcast, backward ants unicast");
    c.check(rec.receivedOf(AntType::InternalForward) > nodesFig41().size(),
            "a broadcast is received by each neighbour");

    // A discovery: external forward and backward ants, each unicast hop sent
    // once and received once (a lossless radio), at the size on the wire.
    const std::size_t sentBefore = rec.sent.size();
    const std::size_t recvBefore = rec.received.size();
    net.ants.clear();
    net.send(S, T);
    c.check(rec.sentOf(AntType::ExternalForward) > 0 && rec.sentOf(AntType::Backward) > 0,
            "the discovery's ants are reported");
    std::uint64_t wire = 0, sent = 0, received = 0;
    for (const auto& s : net.ants)
        wire += encodedSize(s.ant);
    for (std::size_t i = sentBefore; i < rec.sent.size(); ++i) {
        sent += rec.sent[i].bytes;
        c.check(!rec.sent[i].broadcast, "the discovery's ants are unicast");
    }
    for (std::size_t i = recvBefore; i < rec.received.size(); ++i)
        received += rec.received[i].second;
    c.check(rec.sent.size() - sentBefore == net.ants.size() && sent == wire,
            "sent bytes = the encoded ants the network carried");
    c.check(rec.received.size() - recvBefore == rec.sent.size() - sentBefore && received == sent,
            "every ant sent was received, byte for byte");
}

void TestObserverRoutesAndState(Checker& c) {
    // The tables, driven directly.
    {
        Recorder rec;
        InterZoneTable t(2, 3.0, &rec);
        c.check(t.insert(1, {1, 2, 9}, 0.01, 0.0), "a record");
        c.check(rec.routes == std::vector<Recorder::Route>{{9, 2, true}},
                "added: destination 9 through the node after this one");
        const std::size_t one = t.bytes();
        c.check(one == sizeof(NodeAddress) + sizeof(InterRoute) + 3 * sizeof(NodeAddress),
                "bytes: the record's fields and its Path");
        c.check(t.insert(2, {1, 2, 4, 9}, 0.01, 0.1) && rec.routes.size() == 1,
                "a newer record through the same node: no route event");
        c.check(t.bytes() == one + sizeof(NodeAddress), "but its longer Path is counted");
        c.check(t.insert(3, {1, 3, 9}, 0.01, 0.2), "a newer record through another node");
        c.check(rec.routes.size() == 3 && rec.routes[1] == Recorder::Route{9, 2, false} &&
                    rec.routes[2] == Recorder::Route{9, 3, true},
                "a removal and an addition");
        c.check(t.bytes() == one, "bytes follow the Path");
        c.check(!t.insert(3, {1, 4, 9}, 0.01, 0.3) && rec.routes.size() == 3,
                "a refused record is not news");
        t.insert(1, {1, 5, 8}, 0.01, 0.4);
        rec.routes.clear();
        c.check(t.purge(3.35) == 1 && rec.routes == std::vector<Recorder::Route>{{9, 3, false}},
                "purge: the expired record removed");
        c.check(t.erase(8) && rec.routes.size() == 2 &&
                    rec.routes[1] == Recorder::Route{8, 5, false},
                "erase: removed");
        c.check(!t.erase(8) && rec.routes.size() == 2 && t.bytes() == 0,
                "nothing left, nothing reported");
    }
    {
        Recorder rec;
        IntraZoneTable t(2, 0.2, &rec);
        c.check(t.update(7, 3, 1.0, 1, 1) && t.update(7, 3, 1.0, 1, 2) && rec.routes.size() == 1,
                "a deposit into an existing cell is no route event");
        c.check(t.update(7, 4, 1.0, 2, 1) && rec.routes.size() == 2, "a new cell");
        c.check(!t.update(8, 4, 1.0, 1, 1) && rec.routes.size() == 2, "a full table adds none");
        c.check(t.removeNeighbour(3) == 1 && rec.routes.size() == 3 && !rec.routes[2].added,
                "a removed row");
    }
    // One node's two halves: the footprint is both tables.
    {
        FakeClock clock;
        FakeLinkState link;
        SeededRng rng(1);
        Recorder rec;
        IntraZone intra(Config{}, 1, clock, link, rng, &rec);
        InterZone inter(Config{}, intra, clock, &rec);
        neighbours(intra, {2, 3});
        c.check(rec.states.back().first == 2, "two IntraRT cells");
        Ant back{.type = AntType::Backward,
                 .source = 9,
                 .destination = 1,
                 .seq = 1,
                 .time = 0.02,
                 .path = VisitedPath(100),
                 .target = 1,
                 .travelled = 3};
        back.path.push(1, 0.0);  // the destination of the backward ant (this node) first
        back.path.push(2, 0.01);
        back.path.push(9, 0.02);
        inter.onReceive(back, 2);
        c.check(inter.table().size() == 1 && hasRoute(rec, 9, 2, true),
                "the discovery's backward ant adds the InterRT record");
        c.check(rec.states.back().first == 3 &&
                    rec.states.back().second == intra.table().bytes() + inter.table().bytes(),
                "the footprint: both tables, entries and bytes");
        const auto before = rec.states.size();
        intra.reportState();
        c.check(rec.states.size() == before, "on demand: reported only when it changed");
    }
    // The whole network: adds minus removes is what the tables hold.
    {
        Recorder rec;
        Net net = fig41(1, Config{}, &rec);
        c.check(rec.balance() == tableEntries(net), "after a period, adds - removes = IntraRT");
        net.send(S, T);
        c.check(rec.balance() == tableEntries(net), "and with the InterRT records");
        const InterRoute* r = net.inter(S).table().find(T, 0.0);
        c.check(r && hasRoute(rec, T, r->path[1], true), "S's record for T was reported");
        net.cut(M, O);
        net.advance(0.5);
        net.sendAll({{S, T}});
        net.advance(2.0);
        c.check(rec.balance() == tableEntries(net), "also after a lost link and a repair");
        c.check(std::any_of(rec.routes.begin(), rec.routes.end(),
                            [](const auto& e) { return !e.added; }),
                "removals were reported");
    }
    // A zone node that moved away (#93, the stale-entry row): its cells leave
    // as route removals while the neighbour's row stays, and the footprint
    // follows.
    {
        Recorder rec;
        FakeClock clock;
        FakeLinkState link;
        SeededRng rng(1);
        IntraZone z(Config{}, 1, clock, link, rng, &rec);
        const auto ant = [](NodeAddress source, NodeAddress via, std::uint32_t seq) {
            Ant a{.type = AntType::InternalForward,
                  .source = source,
                  .seq = seq,
                  .hops = 1,
                  .time = 0.01,
                  .path = VisitedPath(100)};
            a.path.push(source, 0.005);
            if (via != source) a.path.push(via, 0.01);
            return a;
        };
        z.onReceive(ant(2, 2, 1), 2);  // (2, 2): the neighbour
        z.onReceive(ant(3, 2, 1), 2);  // (2, 3): a zone node through it
        c.check(z.table().size() == 2 && rec.states.back().first == 2, "two cells");
        clock.set(1.0);
        z.onInternalAntTimer();
        z.onReceive(ant(2, 2, 2), 2);  // only the neighbour is heard again
        clock.set(2.0);
        rec.routes.clear();
        z.onInternalAntTimer();
        c.check(rec.routes == std::vector<Recorder::Route>{{3, 2, false}},
                "the cell no ant refreshed for two periods: one route removal");
        c.check(z.table().size() == 1 && z.table().hasNeighbour(2) && !z.table().minHops(3),
                "the neighbour's own cell stays");
        c.check(rec.states.back() ==
                    std::make_pair(std::uint32_t{1}, static_cast<std::uint32_t>(z.table().bytes())),
                "and the footprint follows");
    }
}

void TestObserverData(Checker& c) {
    Recorder rec;
    Net net = fig41(1, Config{}, &rec);
    net.send(S, C);  // inside the zone: forwarded by S, delivered at C
    c.check(rec.data == std::vector<std::pair<NodeAddress, RouteAction>>{{C, RouteAction::Unicast},
                                                                         {C, RouteAction::Deliver}},
            "in the zone: Unicast at S, Deliver at C, both for destination C");
    rec.data.clear();
    const std::size_t hopsBefore = net.dataHops.size();
    const std::size_t deliveredBefore = net.delivered.size();
    net.send(S, T);  // out of the zone: queued while the discovery runs, then resumed
    c.check(rec.decisions(RouteAction::Queue) >= 1, "Queue while the discovery runs");
    c.check(rec.decisions(RouteAction::Unicast) == net.dataHops.size() - hopsBefore &&
                rec.decisions(RouteAction::Deliver) == net.delivered.size() - deliveredBefore &&
                net.delivered.size() > deliveredBefore,
            "every hop forwarded and the delivery are decisions");
    c.check(
        std::all_of(rec.data.begin(), rec.data.end(), [](const auto& e) { return e.first == T; }),
        "all for destination T");
    // Drop: a packet the core cannot route.
    rec.data.clear();
    DataHeader bad{.source = S, .destination = kInvalidAddress};
    net.inter(S).routeData(bad, kInvalidAddress);
    c.check(rec.data == std::vector<std::pair<NodeAddress, RouteAction>>{{kInvalidAddress,
                                                                          RouteAction::Drop}},
            "Drop");
    // A decision comes with no ant: an ant's fate is not a data decision.
    rec.data.clear();
    net.inject(S, externalAnt(U, T, 99, {U}, S), C);
    c.check(rec.data.empty(), "ants make no data decisions");
}

void TestObserverRepair(Checker& c) {
    {
        Recorder rec;
        Net net = fig41(1, Config{}, &rec);
        net.send(S, T);
        net.cut(M, O);
        net.send(S, T);
        const std::size_t hops = net.antsOf(AntType::Notification).size();
        c.check(hops > 0 && rec.sentOf(AntType::Notification) == hops &&
                    rec.receivedOf(AntType::Notification) == hops,
                "the notification ant: every hop sent and received");
        c.check(rec.balance() == tableEntries(net), "the repair's record changes balance");
    }
    {
        Recorder rec;
        Net net = fig41(1, Config{}, &rec);
        net.send(S, T);
        net.send(A, T);
        net.cut(M, O);
        net.cut(P, R);
        net.sendAll({{S, T}, {A, T}});
        const auto queued = rec.decisions(RouteAction::Queue);
        net.advance(1.0);
        c.check(queued >= 2, "the packets wait at M");
        const std::size_t hops = net.antsOf(AntType::Error).size();
        c.check(hops >= 2 && rec.sentOf(AntType::Error) == hops &&
                    rec.receivedOf(AntType::Error) == hops,
                "the error ants of both sources: every hop sent and received");
        c.check(rec.decisions(RouteAction::DiscardPending) == 1,
                "the repair's end discards the buffered packets: DiscardPending");
        c.check(rec.balance() == tableEntries(net), "the error ants' removals are reported");
    }
}

void TestObserverSilent(Checker& c) {
    Net plain = fig41(7);
    scenario(plain);
    Recorder rec;
    Net watched = fig41(7, Config{}, &rec);
    scenario(watched);
    c.check(!rec.sent.empty() && !rec.routes.empty() && !rec.data.empty() && !rec.states.empty() &&
                rec.sentOf(AntType::Error) > 0 && rec.decisions(RouteAction::DiscardPending) > 0,
            "the observer saw the run, errors and discards included");
    const Trace a = traceOf(plain), b = traceOf(watched);
    c.check(a.ants == b.ants, "the same ants, to the same neighbours, in the same order");
    c.check(a.delivered == b.delivered && a.dataHops == b.dataHops && a.drops == b.drops &&
                a.discarded == b.discarded && a.queued == b.queued && a.seqs == b.seqs,
            "the same data decisions and counters");
    // A second run with a CountingObserver is read-only too, and its counters
    // agree with the recorder's.
    FakeClock clock;
    swarm::core::CountingObserver counting(clock);
    Net counted = fig41(7, Config{}, &counting);
    scenario(counted);
    c.check(a == traceOf(counted), "a counting observer changes nothing either");
    const auto stats = counting.stats();
    c.check(stats.routeAdds - stats.routeRemoves == static_cast<std::uint64_t>(rec.balance()),
            "the counters agree with the events");
    c.check(stats.control.count(static_cast<std::uint8_t>(AntType::Error)) == 1 &&
                stats.decisions(RouteAction::DiscardPending) == 1,
            "per-kind and per-action counters filled");
}

}  // namespace hopnet::core::test
