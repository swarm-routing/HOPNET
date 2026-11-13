// Copyright (c) 2026 Daniel Joppi (https://github.com/swarm-routing)
// SPDX-License-Identifier: GPL-2.0-only
//
// HOPNET's ns-3 adapter: an ns3::SwarmRoutingBase that hosts one node's two
// core halves (core/hopnet-intrazone.h, core/hopnet-interzone.h) and carries
// out their decisions (HOPNET golden rule 10). It only converts and executes:
// ants between UDP payloads and core::Ant, data between the DataHeader in
// front of the transport header and core::DataHeader, IPv4 addresses to core
// addresses, and the cores' decisions into sends, deliveries, drops and held
// packets. Time, randomness and timers are swarm-core's ports over ns-3
// (Clock(), Rng() pinned by AssignStreams, Timers()), and the edge time is the
// node's WifiLinkMonitor through LinkState(), so a fixed seed gives
// byte-identical runs (org rule 5). Written fresh (HOPNET golden rule 13).
//
// What the cores decide, the adapter does:
//  - every Config::internalAntPeriod: IntraZone::onInternalAntTimer's
//    broadcast. Each node's first tick is drawn from Rng() in [0, period)
//    (fidelity, "First internal ant"), so the nodes do not all broadcast at
//    once;
//  - every ant received: InterZone::onReceive, its decisions in order;
//  - data: RouteInput handles every unicast data packet (RouteOutput sends a
//    node's own through the loopback first, so its transport header is on it).
//    InterZone::routeData decides: Deliver (DataHeader off, to the transport),
//    Unicast (DataHeader on, as the call left it, to the next hop), Queue (held
//    here, Config::repairBufferCapacity packets, each at most
//    Config::repairBufferTimeout, offered to resume() when an InterRT route to
//    its destination is stored), Drop (through the stack's error callback);
//    DiscardPending drops what is held for a destination;
//  - a local repair's wait: InterZone::onRepairTimeout after repairWait;
//  - a link failure (WifiLinkMonitor, OnLinkFailure): IntraZone::onLinkFailure;
//    a data packet the MAC gave back (OnDataUndelivered):
//    InterZone::onDataSendFailure, its packet resent through ResendData.
#ifndef HOPNET_ROUTING_PROTOCOL_H
#define HOPNET_ROUTING_PROTOCOL_H

#include "ns3/nstime.h"
#include "ns3/swarm-routing-base.h"
#include "ns3/traced-callback.h"

#include <cstdint>
#include <deque>
#include <memory>

namespace hopnet::core {
class IntraZone;
class InterZone;
struct Ant;
struct DataHeader;
struct Outcome;
}  // namespace hopnet::core

namespace ns3 {
namespace hopnet {

/// HOPNET's simulator-free core (core/), from inside ns3::hopnet.
namespace core = ::hopnet::core;

class RoutingProtocol : public SwarmRoutingBase {
public:
    /// UDP port of HOPNET's ants (fidelity, "Control port").
    static constexpr uint16_t kHopnetPort = 5545;

    static TypeId GetTypeId();
    RoutingProtocol();
    ~RoutingProtocol() override;

    /// Data packets held while a route is found or repaired.
    uint32_t Waiting() const { return static_cast<uint32_t>(m_waiting.size()); }
    /// IntraRT cells and InterRT records this node holds.
    uint32_t IntraRtCells() const;
    uint32_t InterRtRecords() const;

    /// An ant this node sent: (core::AntType byte, broadcast).
    typedef void (*AntTxCallback)(uint8_t type, bool broadcast);

    // Ipv4RoutingProtocol: every unicast data packet passes through here.
    bool RouteInput(Ptr<const Packet> p, const Ipv4Header& header, Ptr<const NetDevice> idev,
                    const UnicastForwardCallback& ucb, const MulticastForwardCallback& mcb,
                    const LocalDeliverCallback& lcb, const ErrorCallback& ecb) override;

protected:
    void DoInitialize() override;
    void DoDispose() override;

    uint16_t ControlPort() const override { return kHopnetPort; }
    Ipv4Address SelectNextHop(Ipv4Address dst, Ipv4Address src, Ipv4Address prev,
                              Ptr<const Packet> p) override;
    void StartDiscovery(Ipv4Address dst) override;
    void RecvControl(Ptr<Packet> p, Ipv4Address from, Ipv4Address receiver) override;
    void OnLinkFailure(Ipv4Address neighbor) override;
    void OnDataUndelivered(Ptr<const Packet> p, const Ipv4Header& header, Ipv4Address failedNext,
                           Ipv4Address prev) override;

private:
    /// A data packet in hand: its payload without the DataHeader, the IPv4
    /// header it travels with, and the stack's callbacks (null when the MAC
    /// gave it back: it is then resent with ResendData).
    struct Data {
        Ptr<const Packet> payload;
        Ipv4Header ip;
        UnicastForwardCallback ucb;
        LocalDeliverCallback lcb;
        ErrorCallback ecb;
        uint32_t iif = 0;
    };
    struct Held {
        Data data;
        std::unique_ptr<core::DataHeader> header;
        Time since;
    };

    void InternalAntTick();
    /// Carries out `o`. With `data`, the data decisions are about that packet
    /// and `header` is its DataHeader as the call left it.
    void Process(const core::Outcome& o, Data* data, core::DataHeader* header);
    void SendAnt(const swarm::core::RouteDecision<::hopnet::core::Ant>& d);
    void SendData(const Data& data, const core::DataHeader& header, Ipv4Address next);
    void DropData(const Data& data);
    void Hold(const Data& data, const core::DataHeader& header);
    void Resume(swarm::core::NodeAddress dest);
    void Discard(swarm::core::NodeAddress dest);
    void PurgeWaiting();

    std::unique_ptr<core::IntraZone> m_intra;
    std::unique_ptr<core::InterZone> m_inter;
    std::deque<Held> m_waiting;
    TracedCallback<uint8_t, bool> m_antTx;
};

}  // namespace hopnet
}  // namespace ns3

#endif  // HOPNET_ROUTING_PROTOCOL_H
