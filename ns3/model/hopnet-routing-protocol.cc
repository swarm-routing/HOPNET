// Copyright (c) 2026 Daniel Joppi (https://github.com/swarm-routing)
// SPDX-License-Identifier: GPL-2.0-only
#include "hopnet-routing-protocol.h"

#include "hopnet-header.h"
#include "hopnet-interzone.h"
#include "hopnet-intrazone.h"

#include "ns3/ipv4.h"
#include "ns3/log.h"
#include "ns3/loopback-net-device.h"
#include "ns3/node.h"
#include "ns3/simulator.h"
#include "ns3/socket.h"
#include "ns3/swarm-ns3-ports.h"
#include "ns3/udp-header.h"

#include <utility>
#include <vector>

namespace ns3 {
namespace hopnet {

NS_LOG_COMPONENT_DEFINE("HopnetRoutingProtocol");
NS_OBJECT_ENSURE_REGISTERED(RoutingProtocol);

namespace {

constexpr uint8_t kUdpProtocol = 17;
/// The protocol constants: core::Config's defaults, each sourced in
/// docs/fidelity.md. The adapter has none of its own.
const core::Config kConfig{};

using swarm::core::RouteAction;

}  // namespace

TypeId RoutingProtocol::GetTypeId() {
    static TypeId tid =
        TypeId("ns3::hopnet::RoutingProtocol")
            .SetParent<SwarmRoutingBase>()
            .SetGroupName("Hopnet")
            .AddConstructor<RoutingProtocol>()
            .AddTraceSource("AntTx", "An ant this node sent (core::AntType byte, broadcast).",
                            MakeTraceSourceAccessor(&RoutingProtocol::m_antTx),
                            "ns3::hopnet::RoutingProtocol::AntTxCallback");
    // The ant kinds' names for the observer's ControlTx/ControlRx readers
    // (#40), registered once with the TypeId: a table set later through
    // SwarmRoutingHelper::SetKindNames replaces it and is not reset here.
    [[maybe_unused]] static const bool kindNames = [] {
        SwarmRoutingBase::SetKindNames(tid, core::kindNames());
        return true;
    }();
    return tid;
}

RoutingProtocol::RoutingProtocol() = default;

RoutingProtocol::~RoutingProtocol() = default;

void RoutingProtocol::DoInitialize() {
    // The interfaces are up by now (addresses are assigned before the
    // simulation starts), so the node's address is known.
    const swarm::core::NodeAddress self = ToNodeAddress(MainAddress());
    m_intra =
        std::make_unique<core::IntraZone>(kConfig, self, Clock(), LinkState(), Rng(), &Observer());
    m_inter = std::make_unique<core::InterZone>(kConfig, *m_intra, Clock(), &Observer());
    // The first internal ant at U[0, period) (fidelity, "First internal ant"),
    // drawn into a named local from the seeded stream (org rule 5).
    const double draw = Rng().uniform();
    Timers().schedule(draw * kConfig.internalAntPeriod, [this] { InternalAntTick(); });
    SwarmRoutingBase::DoInitialize();
}

void RoutingProtocol::DoDispose() {
    // The base cancels the pending timers first; then the cores go.
    SwarmRoutingBase::DoDispose();
    m_waiting.clear();
    m_inter.reset();
    m_intra.reset();
}

uint32_t RoutingProtocol::IntraRtCells() const {
    return m_intra ? static_cast<uint32_t>(m_intra->table().size()) : 0;
}

uint32_t RoutingProtocol::InterRtRecords() const {
    return m_inter ? static_cast<uint32_t>(m_inter->table().size()) : 0;
}

void RoutingProtocol::InternalAntTick() {
    if (!m_intra) return;
    SendAnt(m_intra->onInternalAntTimer());
    PurgeWaiting();
    Timers().schedule(kConfig.internalAntPeriod, [this] { InternalAntTick(); });
}

void RoutingProtocol::SendAnt(const swarm::core::RouteDecision<core::Ant>& d) {
    if (d.message && (d.action == RouteAction::Unicast || d.action == RouteAction::Broadcast)) {
        m_antTx(static_cast<uint8_t>(d.message->type), d.action == RouteAction::Broadcast);
    }
    Execute(d, [](const core::Ant& a) { return MakeAntPacket(a); });
}

void RoutingProtocol::Process(const core::Outcome& o, Data* data, core::DataHeader* header) {
    bool handled = false;  // a call decides once about the packet in hand
    for (const auto& d : o.decisions) {
        switch (d.action) {
        case RouteAction::Broadcast:
            SendAnt(d);
            break;
        case RouteAction::Unicast:
            if (d.message) {
                SendAnt(d);
            } else if (data && header && !handled) {
                SendData(*data, *header, ToIpv4Address(d.nextHop));
                handled = true;
            }
            break;
        case RouteAction::Deliver:
            if (data && !handled) {
                Ipv4Header out = data->ip;
                out.SetPayloadSize(data->payload->GetSize());
                if (!data->lcb.IsNull()) data->lcb(data->payload, out, data->iif);
                handled = true;
            }
            break;
        case RouteAction::Queue:
            if (data && header && !handled) {
                Hold(*data, *header);
                handled = true;
            }
            break;
        case RouteAction::Drop:
            // In an ant's outcome a Drop is the ant's: nothing to carry out.
            if (data && !handled && !d.message) {
                DropData(*data);
                handled = true;
            }
            break;
        case RouteAction::DiscardPending:
            Discard(d.nextHop);
            break;
        default:
            break;
        }
    }
    if (o.repairFor != swarm::core::kInvalidAddress) {
        const swarm::core::NodeAddress dest = o.repairFor;
        Timers().schedule(o.repairWait, [this, dest] {
            if (m_inter) Process(m_inter->onRepairTimeout(dest), nullptr, nullptr);
        });
    }
    if (o.routeReady != swarm::core::kInvalidAddress) Resume(o.routeReady);
}

void RoutingProtocol::SendData(const Data& data, const core::DataHeader& header, Ipv4Address next) {
    Ptr<Packet> q = AddDataHeader(data.payload, data.ip.GetProtocol(), header);
    Ipv4Header out = data.ip;
    out.SetPayloadSize(q->GetSize());
    if (!data.ucb.IsNull()) {
        ForwardData(q, out, next, data.ucb);
    } else if (!ResendData(q, out, next)) {
        NS_LOG_DEBUG("could not resend a data packet to " << next);
    }
}

void RoutingProtocol::DropData(const Data& data) {
    if (data.ecb.IsNull()) return;
    Ipv4Header out = data.ip;
    out.SetPayloadSize(data.payload->GetSize());
    data.ecb(data.payload, out, Socket::ERROR_NOROUTETOHOST);
}

void RoutingProtocol::Hold(const Data& data, const core::DataHeader& header) {
    // Config::repairBufferCapacity packets, each at most repairBufferTimeout
    // (fidelity, "Local-repair buffer"): the oldest goes when it is full.
    PurgeWaiting();
    if (m_waiting.size() >= std::max<std::size_t>(kConfig.repairBufferCapacity, 1)) {
        DropData(m_waiting.front().data);
        m_waiting.pop_front();
    }
    m_waiting.push_back(Held{data, std::make_unique<core::DataHeader>(header), Simulator::Now()});
}

void RoutingProtocol::Resume(swarm::core::NodeAddress dest) {
    PurgeWaiting();
    std::vector<Held> ready;
    for (auto it = m_waiting.begin(); it != m_waiting.end();) {
        if (it->header->destination == dest) {
            ready.push_back(std::move(*it));
            it = m_waiting.erase(it);
        } else {
            ++it;
        }
    }
    for (Held& h : ready) {
        if (!m_inter) return;
        Process(m_inter->resume(*h.header), &h.data, h.header.get());
    }
}

void RoutingProtocol::Discard(swarm::core::NodeAddress dest) {
    for (auto it = m_waiting.begin(); it != m_waiting.end();) {
        if (it->header->destination == dest) {
            DropData(it->data);
            it = m_waiting.erase(it);
        } else {
            ++it;
        }
    }
}

void RoutingProtocol::PurgeWaiting() {
    const Time limit = Seconds(kConfig.repairBufferTimeout);
    while (!m_waiting.empty() && Simulator::Now() - m_waiting.front().since > limit) {
        DropData(m_waiting.front().data);
        m_waiting.pop_front();
    }
}

Ipv4Address RoutingProtocol::SelectNextHop(Ipv4Address /*dst*/, Ipv4Address /*src*/,
                                           Ipv4Address /*prev*/, Ptr<const Packet> /*p*/) {
    // No next hop in RouteOutput: the transport header is added after it, and
    // the DataHeader goes in front of the transport header. The base sends
    // the packet through the loopback device to RouteInput, which originates it.
    return Ipv4Address();
}

void RoutingProtocol::StartDiscovery(Ipv4Address dst) {
    // RouteInput handles every data packet, so the base never holds one.
    DiscardDeferred(dst);
}

bool RoutingProtocol::RouteInput(Ptr<const Packet> p, const Ipv4Header& header,
                                 Ptr<const NetDevice> idev, const UnicastForwardCallback& ucb,
                                 const MulticastForwardCallback& mcb,
                                 const LocalDeliverCallback& lcb, const ErrorCallback& ecb) {
    const Ipv4Address dst = header.GetDestination();
    Ptr<Ipv4> ipv4 = idev ? idev->GetNode()->GetObject<Ipv4>() : nullptr;
    bool control = false;
    if (header.GetProtocol() == kUdpProtocol) {
        UdpHeader udp;
        // A control datagram leaves a socket bound to kHopnetPort, so both
        // its ports are; a data datagram keeps its own UDP header first.
        control = p->PeekHeader(udp) && udp.GetSourcePort() == kHopnetPort &&
                  udp.GetDestinationPort() == kHopnetPort;
    }
    // Control packets, broadcast and multicast are the base's.
    if (!m_inter || !ipv4 || control || dst.IsBroadcast() || dst.IsMulticast()) {
        return SwarmRoutingBase::RouteInput(p, header, idev, ucb, mcb, lcb, ecb);
    }
    const int32_t iif = ipv4->GetInterfaceForDevice(idev);
    if (DynamicCast<const LoopbackNetDevice>(idev)) {
        if (ipv4->IsDestinationAddress(dst, iif)) {
            return SwarmRoutingBase::RouteInput(p, header, idev, ucb, mcb, lcb, ecb);
        }
        // This node's own data, sent here by RouteOutput. It leaves through
        // ucb -> IpForward, which decrements the TTL for a hop no radio
        // carried: add it back (as SwarmRoutingBase does for its loopback).
        Ipv4Header own = header;
        own.SetTtl(header.GetTtl() + 1);
        core::DataHeader h;
        h.source = ToNodeAddress(MainAddress());
        h.destination = ToNodeAddress(dst);
        Data d{p, own, ucb, lcb, ecb, static_cast<uint32_t>(iif)};
        Process(m_inter->routeData(h, swarm::core::kInvalidAddress), &d, &h);
        return true;
    }
    // From a neighbour: the payload carries the DataHeader.
    const auto lead = ReadDataHeader(p, header.GetProtocol(), kConfig.maxPathLength);
    if (!lead || lead->header.destination != ToNodeAddress(dst)) {
        NS_LOG_DEBUG("no valid HOPNET data header on a data packet for " << dst);
        return false;
    }
    SwarmPrevHopTag tag;
    const Ipv4Address prev = p->PeekPacketTag(tag) ? tag.Get() : Ipv4Address();
    core::DataHeader h = lead->header;
    Data d{StripDataHeader(p, header.GetProtocol(), lead->size),
           header,
           ucb,
           lcb,
           ecb,
           static_cast<uint32_t>(iif)};
    Process(m_inter->routeData(h, ToNodeAddress(prev)), &d, &h);
    return true;
}

void RoutingProtocol::RecvControl(Ptr<Packet> p, Ipv4Address from, Ipv4Address /*receiver*/) {
    if (!m_inter) return;
    const auto ant = DecodeAntPacket(p, kConfig.maxPathLength);
    if (!ant) {
        NS_LOG_DEBUG("dropped a malformed ant from " << from);
        return;
    }
    Process(m_inter->onReceive(*ant, ToNodeAddress(from)), nullptr, nullptr);
}

void RoutingProtocol::OnLinkFailure(Ipv4Address neighbor) {
    if (m_intra) m_intra->onLinkFailure(ToNodeAddress(neighbor));
}

void RoutingProtocol::OnDataUndelivered(Ptr<const Packet> p, const Ipv4Header& header,
                                        Ipv4Address failedNext, Ipv4Address /*prev*/) {
    // The MAC gave the packet back at its retry limit ([W07] §4.3, fidelity
    // "How a broken link is detected"): the core re-routes it or starts a
    // local repair; a resend goes through ResendData (no stack callbacks here).
    if (!m_inter) return;
    const auto lead = ReadDataHeader(p, header.GetProtocol(), kConfig.maxPathLength);
    if (!lead) return;
    core::DataHeader h = lead->header;
    Data d{StripDataHeader(p, header.GetProtocol(), lead->size), header, {}, {}, {}, 0};
    Process(m_inter->onDataSendFailure(h, ToNodeAddress(failedNext)), &d, &h);
}

}  // namespace hopnet
}  // namespace ns3
