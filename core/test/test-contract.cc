// Copyright (c) 2026 Daniel Joppi (https://github.com/swarm-routing)
// SPDX-License-Identifier: MIT OR GPL-2.0-only
//
// swarm-core's protocol contract suite (its core/testbench/swarm-contract-tests.h,
// README "Protocol contract suite: how a protocol opts in") run on HOPNET's
// core: one IntraZone + InterZone pair per node on swarm-core's testbench, the
// adapter carrying out the decisions as ns3/model/hopnet-routing-protocol.cc
// does (the internal-ant timer with its first tick at U[0, period), the
// repair and query timers, the queue of packets waiting for a route). The
// testbench has no promiscuous reception, so overhearing (#5 (s)) is not
// exercised here; the inter-zone tests cover it (hopnet-net.h).
//
// The scene: swarm-core's default 6-node line, 10 s; node 0 sends a data
// packet to node 5 (outside its radius-2 zone, so a discovery with its
// random query-processing delay, #5 (t)) every second from t = 3 s.
//
// ZeroTtlAndSelfLoopPacketsAreDropped probes the internal forward ant, the
// one HOPNET message with a hop limit in the suite's sense: Hops starts at the
// zone radius and loses one per hop (docs/fidelity.md rows "Internal forward
// ants are sent periodically ...", "How an internal forward ant moves past the
// first hop" and "Internal forward ant hop limit", [W07] p. 25). An ant that
// arrives with Hops 0, or whose Path already holds the receiver (a loop), is
// dropped untouched (IntraZone::onInternalForward); the backward ant starts
// only where an ant arrives with Hops 1 ([W07] p. 26). External ants and data
// carry no hop limit in [W07] (row "External forward ants carry no hop limit";
// `travelled` is a repo safety cap, row "Hops an external ant or a data packet
// may travel"), so they are not the probe. The live control's hop limit is the
// zone radius: the suite's default 8 is above it, which the core rejects as
// malformed, and so would not be live.
#include "../hopnet-ant.h"
#include "../hopnet-config.h"
#include "../hopnet-data.h"
#include "../hopnet-interzone.h"
#include "../hopnet-intrazone.h"
#include "testbench/swarm-contract-tests.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <optional>
#include <vector>

namespace {

namespace tb = swarm::core::testbench;
using namespace hopnet::core;
using swarm::core::RouteAction;
using swarm::core::Time;

/// One frame on the testbench's air: an ant, or a data packet's header.
struct Frame {
    std::optional<Ant> ant;
    std::optional<DataHeader> data;
};

/// The kind byte of a frame: the ant's wire type, 0xFF for data.
std::uint8_t kindOf(const Frame& f) {
    return f.ant ? static_cast<std::uint8_t>(f.ant->type) : 0xFF;
}

/// FNV-1a over the frame's encoded bytes (the ant or data codec).
std::uint64_t payloadHash(const Frame& f) {
    std::vector<std::uint8_t> bytes;
    if (f.ant) encode(*f.ant, bytes);
    if (f.data) encode(*f.data, bytes);
    std::uint64_t h = 0xCBF29CE484222325ull;
    for (std::uint8_t b : bytes) {
        h ^= b;
        h *= 0x100000001B3ull;
    }
    return h;
}

/// A link-state port with no sample, for a run without one: the core takes
/// its ILinkState by reference, so "no such port" is a port that knows nothing.
struct NoLinkState : swarm::core::ILinkState {
    int macQueueLength(NodeAddress /*nextHop*/) const override { return 0; }
    Time macServiceTime(NodeAddress /*nextHop*/) const override { return 0.0; }
};

const Config kConfig{};
constexpr NodeAddress kSource = 0;
constexpr NodeAddress kDestination = 5;  // the line's far end, outside node 0's zone
constexpr Time kFirstSend = 3.0;
constexpr Time kLastSend = 10.0;

class HopnetNode : public tb::INodeAdapter<Frame> {
public:
    HopnetNode(tb::NodePorts<Frame>& p, const tb::ContractProviders& prov)
        : p_(p), intra_(kConfig, p.address(), p.clock(), prov.linkState ? *prov.linkState : noLink_,
                        p.rng(), prov.observer),
          inter_(kConfig, intra_, p.clock(), prov.observer) {}

    void start() override {
        // The first internal ant at U[0, period), as the ns-3 adapter.
        const double draw = p_.rng().uniform();
        p_.timers().schedule(draw * kConfig.internalAntPeriod, [this] { tick(); });
        // The scene's traffic (the suite wraps each adapter for payloadHash,
        // so a script cannot reach this one: it drives itself).
        if (p_.address() == kSource) {
            for (Time t = kFirstSend; t < kLastSend; t += 1.0)
                p_.timers().schedule(t, [this] { send(kDestination); });
        }
    }
    void receive(NodeAddress from, const Frame& f) override {
        if (f.ant) {
            process(inter_.onReceive(*f.ant, from), nullptr);
        } else if (f.data) {
            DataHeader h = *f.data;
            process(inter_.routeData(h, from), &h);
        }
    }
    void undelivered(NodeAddress to, const Frame& f) override {
        if (f.data) {  // the MAC's retry limit ([W07] §4.3)
            DataHeader h = *f.data;
            process(inter_.onDataSendFailure(h, to), &h);
        } else {
            intra_.onLinkFailure(to);
        }
    }

private:
    struct Held {
        DataHeader header;
        Time since;
    };

    /// A data packet from this node's transport.
    void send(NodeAddress dest) {
        DataHeader h{.source = p_.address(), .destination = dest};
        process(inter_.routeData(h, kInvalidAddress), &h);
    }
    void tick() {
        sendAnt(intra_.onInternalAntTimer());
        purge();
        p_.timers().schedule(kConfig.internalAntPeriod, [this] { tick(); });
    }
    void sendAnt(const Decision& d) {
        if (!d.message) return;
        if (d.action == RouteAction::Broadcast) p_.broadcast(Frame{*d.message, std::nullopt});
        if (d.action == RouteAction::Unicast)
            p_.unicast(d.nextHop, Frame{*d.message, std::nullopt});
    }
    void process(const Outcome& o, DataHeader* h) {
        bool handled = false;  // a call decides once about the packet in hand
        for (const Decision& d : o.decisions) {
            switch (d.action) {
            case RouteAction::Broadcast:
                sendAnt(d);
                break;
            case RouteAction::Unicast:
                if (d.message) {
                    sendAnt(d);
                } else if (h && !handled) {
                    p_.unicast(d.nextHop, Frame{std::nullopt, *h});
                    handled = true;
                }
                break;
            case RouteAction::Deliver:
            case RouteAction::Drop:
                if (h && !d.message) handled = true;
                break;
            case RouteAction::Queue:
                if (h && !handled) {
                    hold(*h);
                    handled = true;
                }
                break;
            case RouteAction::DiscardPending:
                std::erase_if(waiting_,
                              [&](const Held& w) { return w.header.destination == d.nextHop; });
                break;
            default:
                break;
            }
        }
        if (o.repairFor != kInvalidAddress) {
            const NodeAddress dest = o.repairFor;
            p_.timers().schedule(o.repairWait,
                                 [this, dest] { process(inter_.onRepairTimeout(dest), nullptr); });
        }
        if (o.querySource != kInvalidAddress) {  // #5 (t)
            const NodeAddress source = o.querySource;
            const std::uint32_t seq = o.querySeq;
            p_.timers().schedule(o.queryWait, [this, source, seq] {
                process(inter_.onQueryDelay(source, seq), nullptr);
            });
        }
        if (o.routeReady != kInvalidAddress) resume(o.routeReady);
    }
    // Config::repairBufferCapacity packets, each at most repairBufferTimeout.
    void hold(const DataHeader& h) {
        purge();
        if (waiting_.size() >= std::max<std::size_t>(kConfig.repairBufferCapacity, 1))
            waiting_.pop_front();
        waiting_.push_back({h, p_.clock().now()});
    }
    void resume(NodeAddress dest) {
        purge();
        std::vector<DataHeader> ready;
        std::erase_if(waiting_, [&](const Held& w) {
            if (w.header.destination != dest) return false;
            ready.push_back(w.header);
            return true;
        });
        for (DataHeader& r : ready)
            process(inter_.resume(r), &r);
    }
    void purge() {
        const Time now = p_.clock().now();
        while (!waiting_.empty() && now - waiting_.front().since > kConfig.repairBufferTimeout)
            waiting_.pop_front();
    }

    tb::NodePorts<Frame>& p_;
    NoLinkState noLink_;
    IntraZone intra_;
    InterZone inter_;
    std::deque<Held> waiting_;
};

}  // namespace

int main() {
    tb::ContractSpec<Frame> spec;  // a 6-node line, 10 s, seeds 1 and 2
    spec.factory = [](tb::NodePorts<Frame>& p, const tb::ContractProviders& prov) {
        return std::make_unique<HopnetNode>(p, prov);
    };
    spec.kindOf = kindOf;
    spec.payloadHash = payloadHash;
    // The suite hands the probe to node 1 from node 0, so its Path ends at 0;
    // the origin's own loop is origin -> 0 -> 1. Internal forward ants have
    // no destination ([W07] p. 25), and the sequence number is one the
    // scene's 10 s never reach.
    spec.hopLimited = [](NodeAddress origin, NodeAddress /*dest*/, int hopLimit) {
        constexpr NodeAddress kSender = 0;
        constexpr Time kHop = 0.001;
        Ant ant{.type = AntType::InternalForward,
                .source = origin,
                .seq = 0xFFFFFFFFu,
                .hops = static_cast<std::uint16_t>(hopLimit),
                .time = kHop};
        ant.path.push(origin, 0.0);
        if (origin != kSender) {
            ant.time += kHop;
            ant.path.push(kSender, kHop);
        }
        return Frame{ant, std::nullopt};
    };
    spec.liveHopLimit = kConfig.zoneRadius;
    return tb::runProtocolContract(spec) == 0 ? 0 : 1;
}
