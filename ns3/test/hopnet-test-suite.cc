// Copyright (c) 2026 Daniel Joppi (https://github.com/swarm-routing)
// SPDX-License-Identifier: GPL-2.0-only
//
// `test.py -s hopnet`. Needs ns-3.42+ (scoped TestCase::Duration).
//
// - core/'s tests (core/test/), one TestCase each, so the debug and sanitized
//   legs run them inside ns-3 as core.yml runs them alone;
// - the two wire formats in ns-3 packets: the ant payload and the DataHeader
//   (right after a UDP header, else in front), roundtrip, and a seeded fuzz
//   of both readers a received packet goes through (org rule 3);
// - the adapter on a static four-node 802.11b chain with zone radius 2: the
//   internal ants build the zones, a packet to the node three hops away (out
//   of the source's zone) waits for an inter-zone discovery and is delivered
//   with the rest;
// - determinism: the same seed gives the same ant and delivery trace, another
//   seed another (org rule 5);
// - the router observer (#40): the same seed gives a byte-identical run with
//   its trace sources connected, they agree with the counters, and the ant
//   kinds are named;
// - the adapter's guards: no core before the protocol starts, malformed
//   control payloads and data headers ignored.
// Through the include path (core/, resolved past the contrib link), not
// "../../core/...": gcov would collapse that beside the link, not its target.
#include "test/hopnet-check.h"
#include "../model/hopnet-header.h"

#include "ns3/hopnet-helper.h"
#include "ns3/hopnet-routing-protocol.h"
#include "ns3/inet-socket-address.h"
#include "ns3/internet-stack-helper.h"
#include "ns3/ipv4-address-helper.h"
#include "ns3/ipv4-l3-protocol.h"
#include "ns3/mobility-helper.h"
#include "ns3/simulator.h"
#include "ns3/socket.h"
#include "ns3/swarm-core-module.h"
#include "ns3/test.h"
#include "ns3/udp-header.h"
#include "ns3/udp-socket-factory.h"
#include "ns3/wifi-helper.h"
#include "ns3/yans-wifi-helper.h"

#include <cstdint>
#include <map>
#include <sstream>
#include <string>
#include <vector>

using namespace ns3;
using ::hopnet::core::Ant;
using ::hopnet::core::AntType;

namespace {

/// One core/ test function, its failures reported through ns-3.
class HopnetCoreTestCase : public TestCase {
public:
    explicit HopnetCoreTestCase(const ::hopnet::core::test::NamedTest& t)
        : TestCase(std::string("core: ") + t.name), m_test(t) {}

private:
    void DoRun() override {
        ::hopnet::core::test::Checker c;
        m_test.run(c);
        // Each failure prints as "got <file:line: check>, expected ''".
        for (const auto& f : c.failures()) {
            NS_TEST_EXPECT_MSG_EQ(f, std::string(), "core check failed");
        }
    }

    ::hopnet::core::test::NamedTest m_test;
};

std::vector<uint8_t> Bytes(Ptr<const Packet> p) {
    std::vector<uint8_t> b(p->GetSize());
    if (!b.empty()) p->CopyData(b.data(), static_cast<uint32_t>(b.size()));
    return b;
}

Ptr<Packet> FromBytes(const std::vector<uint8_t>& b) {
    if (b.empty()) return Create<Packet>();  // data() may be null
    return Create<Packet>(b.data(), static_cast<uint32_t>(b.size()));
}

constexpr std::size_t kCap = 100;  // core::Config{}.maxPathLength

Ant SampleAnt() {
    Ant a{.type = AntType::ExternalForward,
          .source = 0x0A010001,
          .destination = 0x0A010004,
          .seq = 7,
          .hops = 0,
          .time = 0.006,
          .path = swarm::core::VisitedPath(kCap),
          .target = 0x0A010003,
          .travelled = 2};
    a.path.push(0x0A010001, 0.0);
    a.path.push(0x0A010003, 0.006);
    return a;
}

::hopnet::core::DataHeader SampleData() {
    return {.source = 0x0A010001,
            .destination = 0x0A010004,
            .target = 0x0A010003,
            .travelled = 1,
            .route = {0x0A010001, 0x0A010003, 0x0A010004}};
}

/// SplitMix64, as core/'s tests: the same bytes on every platform.
uint64_t Mix(uint64_t& s) {
    uint64_t z = (s += 0x9E3779B97F4A7C15ull);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

class WireTestCase : public TestCase {
public:
    WireTestCase() : TestCase("wire: ants and the data header through ns-3 packets") {}

private:
    void DoRun() override {
        const Ant a = SampleAnt();
        Ptr<Packet> p = ns3::hopnet::MakeAntPacket(a);
        NS_TEST_EXPECT_MSG_EQ(Bytes(p)[0], ::hopnet::core::kWireVersion, "leading version byte");
        const auto back = ns3::hopnet::DecodeAntPacket(p, kCap);
        NS_TEST_ASSERT_MSG_EQ(back.has_value(), true, "the ant decodes");
        NS_TEST_EXPECT_MSG_EQ((*back == a), true, "the same ant back");

        const auto h = SampleData();
        Ptr<Packet> payload = Create<Packet>(64);
        // Not UDP (protocol 0 here): the header goes in front.
        Ptr<Packet> q = ns3::hopnet::AddDataHeader(payload, 0, h);
        NS_TEST_EXPECT_MSG_EQ(q->GetSize(), 64u + 17u + 4u * 3u, "17 bytes + 4 per route node");
        NS_TEST_EXPECT_MSG_EQ(Bytes(q)[0], ::hopnet::core::kDataWireVersion, "version first");
        const auto lead = ns3::hopnet::ReadDataHeader(q, 0, kCap);
        NS_TEST_ASSERT_MSG_EQ(lead.has_value(), true, "the data header reads");
        NS_TEST_EXPECT_MSG_EQ((lead->header == h), true, "the same header back");
        NS_TEST_EXPECT_MSG_EQ(lead->size, 29u, "its size");
        NS_TEST_EXPECT_MSG_EQ(ns3::hopnet::StripDataHeader(q, 0, lead->size)->GetSize(), 64u,
                              "the payload after it");
        ns3::hopnet::DataHeader viaHeader;
        NS_TEST_EXPECT_MSG_EQ(q->Copy()->RemoveHeader(viaHeader), 29u,
                              "RemoveHeader consumes the header");
        NS_TEST_EXPECT_MSG_EQ((viaHeader.IsValid() && viaHeader.Get() == h), true,
                              "the same header through ns3::Header");
        NS_TEST_EXPECT_MSG_EQ(ns3::hopnet::ReadDataHeader(q, 0, 2).has_value(), false,
                              "a route over the cap");

        // UDP: the header rides right after the UDP header, so a reader of the
        // ports (the harness's control count) still sees the data port.
        Ptr<Packet> datagram = Create<Packet>(64);
        UdpHeader udp;
        udp.SetSourcePort(49153);
        udp.SetDestinationPort(9);
        datagram->AddHeader(udp);
        const std::vector<uint8_t> before = Bytes(datagram);
        Ptr<Packet> u = ns3::hopnet::AddDataHeader(datagram, 17, h);
        UdpHeader seen;
        u->PeekHeader(seen);
        NS_TEST_EXPECT_MSG_EQ(seen.GetDestinationPort(), 9u, "the UDP header stays first");
        NS_TEST_EXPECT_MSG_EQ(Bytes(u)[8], ::hopnet::core::kDataWireVersion,
                              "the data header right after it");
        const auto ul = ns3::hopnet::ReadDataHeader(u, 17, kCap);
        NS_TEST_ASSERT_MSG_EQ(ul.has_value(), true, "read after the UDP header");
        NS_TEST_EXPECT_MSG_EQ((ul->header == h), true, "the same header back");
        NS_TEST_EXPECT_MSG_EQ((Bytes(ns3::hopnet::StripDataHeader(u, 17, ul->size)) == before),
                              true, "stripped: the datagram as it was");
        NS_TEST_EXPECT_MSG_EQ(ns3::hopnet::ReadDataHeader(Create<Packet>(4), 17, kCap).has_value(),
                              false, "shorter than a UDP header");
    }
};

class FuzzTestCase : public TestCase {
public:
    FuzzTestCase() : TestCase("wire: seeded fuzz of the ant and data-header readers") {}

private:
    void DoRun() override {
        std::vector<uint8_t> ant;
        ::hopnet::core::encode(SampleAnt(), ant);
        std::vector<uint8_t> data;
        ::hopnet::core::encode(SampleData(), data);
        uint64_t s = 0x4F9E7;
        uint32_t ants = 0;
        uint32_t datas = 0;
        for (int i = 0; i < 6000; ++i) {
            const std::vector<uint8_t>& valid = (i % 2 == 0) ? ant : data;
            std::vector<uint8_t> b;
            if (i % 3 == 0) {
                b.resize(Mix(s) % 80);  // random bytes
                for (auto& x : b)
                    x = static_cast<uint8_t>(Mix(s));
            } else {  // a valid frame with one byte changed, cut short or extended
                b = valid;
                b[Mix(s) % b.size()] = static_cast<uint8_t>(Mix(s));
                const uint64_t shape = Mix(s) % 3;
                if (shape == 1) b.resize(Mix(s) % b.size());
                if (shape == 2) b.push_back(static_cast<uint8_t>(Mix(s)));
            }
            if (const auto a = ns3::hopnet::DecodeAntPacket(FromBytes(b), kCap)) {
                ++ants;
                std::vector<uint8_t> again;
                ::hopnet::core::encode(*a, again);
                NS_TEST_ASSERT_MSG_EQ((again == b), true, "an accepted ant re-encodes identically");
            }
            if (const auto d = ns3::hopnet::ReadDataHeader(FromBytes(b), 0, kCap)) {
                ++datas;
                std::vector<uint8_t> again;
                ::hopnet::core::encode(d->header, again);
                NS_TEST_ASSERT_MSG_EQ(again.size(), d->size, "the size read is the encoded size");
                NS_TEST_ASSERT_MSG_EQ(
                    (std::vector<uint8_t>(b.begin(), b.begin() + d->size) == again), true,
                    "an accepted header re-encodes identically");
            }
        }
        NS_TEST_EXPECT_MSG_GT(ants, 0u, "the ant fuzz reaches the accepting path");
        NS_TEST_EXPECT_MSG_GT(datas, 0u, "the data fuzz reaches the accepting path");
        NS_TEST_EXPECT_MSG_EQ(ns3::hopnet::DecodeAntPacket(Create<Packet>(), kCap).has_value(),
                              false, "empty ant payload");
        NS_TEST_EXPECT_MSG_EQ(ns3::hopnet::ReadDataHeader(Create<Packet>(16), 0, kCap).has_value(),
                              false, "a payload shorter than the data header");
    }
};

// --- the adapter on a chain ------------------------------------------------------

constexpr uint16_t kDataPort = 9;

/// Places `nodes` on a line 200 m apart, each reaching only its neighbours
/// (250 m range), on 802.11b at 2 Mbit/s.
NetDeviceContainer LineOfWifiNodes(NodeContainer& nodes) {
    MobilityHelper mobility;
    Ptr<ListPositionAllocator> pos = CreateObject<ListPositionAllocator>();
    for (uint32_t i = 0; i < nodes.GetN(); ++i)
        pos->Add(Vector(200.0 * i, 0.0, 0.0));
    mobility.SetPositionAllocator(pos);
    mobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
    mobility.Install(nodes);

    WifiHelper wifi;
    wifi.SetStandard(WIFI_STANDARD_80211b);
    wifi.SetRemoteStationManager("ns3::ConstantRateWifiManager", "DataMode",
                                 StringValue("DsssRate2Mbps"), "ControlMode",
                                 StringValue("DsssRate1Mbps"));
    YansWifiChannelHelper channel;
    channel.SetPropagationDelay("ns3::ConstantSpeedPropagationDelayModel");
    channel.AddPropagationLoss("ns3::RangePropagationLossModel", "MaxRange", DoubleValue(250.0));
    YansWifiPhyHelper phy;
    phy.SetChannel(channel.Create());
    WifiMacHelper mac;
    mac.SetType("ns3::AdhocWifiMac");
    return wifi.Install(phy, mac, nodes);
}

/// What one chain run observed.
struct ChainRun {
    std::vector<std::string> trace;  ///< ant sends and data receptions, in order
    /// Every IP packet the nodes sent, its bytes (IP header, ant or DataHeader,
    /// transport, payload) folded in order into one FNV-1a digest.
    uint64_t txDigest = 0xcbf29ce484222325ull;
    uint64_t txPackets = 0;
    uint32_t sent = 0;
    uint32_t received = 0;
    std::map<int, uint64_t> ants;  ///< sent, by core::AntType byte
    uint32_t interRt0 = 0;         ///< node 0's InterRT records after the run
    uint32_t intraRt1 = 0;         ///< node 1's IntraRT cells
    uint32_t waiting0 = 0;
    // The router observer: the trace sources' own tallies, and the counters.
    uint64_t ctlTx = 0;
    uint64_t ctlRx = 0;
    uint64_t routeAdds = 0;
    uint64_t routeRemoves = 0;
    std::map<int, uint64_t> decisions;  ///< by RouteAction
    uint64_t stateReports = 0;
    swarm::core::RouterStats stats;  ///< SwarmRoutingHelper::CollectStats' aggregate
    std::string internalName;
    std::string externalName;
};

class Chain {
public:
    /// `observe` connects the router observer's trace sources, which must not
    /// change the run (org rule 5).
    ChainRun Run(uint32_t seed, uint32_t packets, bool observe = false) {
        SwarmCoreSeed(seed);
        NodeContainer nodes;
        nodes.Create(4);
        NetDeviceContainer devs = LineOfWifiNodes(nodes);

        HopnetHelper hopnet;
        hopnet.Install(nodes);
        Ipv4AddressHelper a;
        a.SetBase("10.1.0.0", "255.255.255.0");
        Ipv4InterfaceContainer ifs = a.Assign(devs);
        // Every stream pinned, so a second run in this process draws as the first.
        int64_t stream = hopnet.AssignStreams(nodes, 0);
        stream += WifiHelper::AssignStreams(devs, stream);
        InternetStackHelper().AssignStreams(nodes, stream);

        m_run = ChainRun{};
        for (uint32_t i = 0; i < 4; ++i) {
            Proto(nodes.Get(i))
                ->TraceConnect("AntTx", std::to_string(i), MakeCallback(&Chain::AntTx, this));
        }
        for (uint32_t i = 0; i < 4; ++i) {
            nodes.Get(i)->GetObject<Ipv4L3Protocol>()->TraceConnect(
                "Tx", std::to_string(i), MakeCallback(&Chain::IpTx, this));
        }
        if (observe) {
            for (uint32_t i = 0; i < 4; ++i) {
                Ptr<ns3::hopnet::RoutingProtocol> p = Proto(nodes.Get(i));
                p->TraceConnectWithoutContext("ControlTx", MakeCallback(&Chain::ControlTx, this));
                p->TraceConnectWithoutContext("ControlRx", MakeCallback(&Chain::ControlRx, this));
                p->TraceConnectWithoutContext("RouteChanged",
                                              MakeCallback(&Chain::RouteChanged, this));
                p->TraceConnectWithoutContext("DataDecision",
                                              MakeCallback(&Chain::DataDecision, this));
                p->TraceConnectWithoutContext("StateSize", MakeCallback(&Chain::StateSize, this));
            }
        }
        Ptr<Socket> sink = Socket::CreateSocket(nodes.Get(3), UdpSocketFactory::GetTypeId());
        sink->Bind(InetSocketAddress(Ipv4Address::GetAny(), kDataPort));
        sink->SetRecvCallback(MakeCallback(&Chain::Receive, this));
        m_src = Socket::CreateSocket(nodes.Get(0), UdpSocketFactory::GetTypeId());
        m_dst = ifs.GetAddress(3);
        // The zones form within the first internal-ant periods; data from 3 s.
        for (uint32_t k = 0; k < packets; ++k) {
            Simulator::Schedule(Seconds(3.0 + 0.5 * k), &Chain::Send, this);
        }
        Simulator::Stop(Seconds(4.0 + 0.5 * packets));
        Simulator::Run();
        m_run.interRt0 = Proto(nodes.Get(0))->InterRtRecords();
        m_run.intraRt1 = Proto(nodes.Get(1))->IntraRtCells();
        m_run.waiting0 = Proto(nodes.Get(0))->Waiting();
        m_run.stats = SwarmRoutingHelper::CollectStats(nodes).aggregate;
        m_run.internalName = hopnet.KindName(static_cast<uint8_t>(AntType::InternalForward));
        m_run.externalName = hopnet.KindName(static_cast<uint8_t>(AntType::ExternalForward));
        m_src = nullptr;
        Simulator::Destroy();
        return m_run;
    }

private:
    static Ptr<ns3::hopnet::RoutingProtocol> Proto(Ptr<Node> n) {
        return n->GetObject<ns3::hopnet::RoutingProtocol>();
    }
    void AntTx(std::string node, uint8_t type, bool broadcast) {
        ++m_run.ants[type];
        std::ostringstream os;
        os << Simulator::Now().GetNanoSeconds() << " n" << node << " tx type=" << int{type}
           << (broadcast ? " bcast" : " ucast");
        m_run.trace.push_back(os.str());
    }
    void IpTx(std::string node, Ptr<const Packet> p, Ptr<Ipv4>, uint32_t) {
        ++m_run.txPackets;
        auto fold = [this](uint8_t b) { m_run.txDigest = (m_run.txDigest ^ b) * 0x100000001b3ull; };
        for (char ch : node)
            fold(static_cast<uint8_t>(ch));
        for (uint8_t b : Bytes(p))
            fold(b);
    }
    void ControlTx(uint8_t, bool, uint32_t) { ++m_run.ctlTx; }
    void ControlRx(uint8_t, uint32_t) { ++m_run.ctlRx; }
    void RouteChanged(Ipv4Address, Ipv4Address, bool added) {
        ++(added ? m_run.routeAdds : m_run.routeRemoves);
    }
    void DataDecision(Ipv4Address, swarm::core::RouteAction a) {
        ++m_run.decisions[static_cast<int>(a)];
    }
    void StateSize(uint32_t, uint32_t) { ++m_run.stateReports; }
    void Send() {
        ++m_run.sent;
        m_src->SendTo(Create<Packet>(64), 0, InetSocketAddress(m_dst, kDataPort));
    }
    void Receive(Ptr<Socket> s) {
        while (Ptr<Packet> p = s->Recv()) {
            ++m_run.received;
            NS_ASSERT_MSG(p->GetSize() == 64, "the DataHeader is off before delivery");
            std::ostringstream os;
            os << Simulator::Now().GetNanoSeconds() << " n3 rx data";
            m_run.trace.push_back(os.str());
        }
    }

    ChainRun m_run;
    Ptr<Socket> m_src;
    Ipv4Address m_dst;
};

constexpr auto kInternal = static_cast<int>(AntType::InternalForward);
constexpr auto kExternal = static_cast<int>(AntType::ExternalForward);
constexpr auto kBackward = static_cast<int>(AntType::Backward);

class ChainDeliveryTestCase : public TestCase {
public:
    ChainDeliveryTestCase()
        : TestCase("adapter: zones, an inter-zone discovery and delivery on a 4-node chain") {}

private:
    void DoRun() override {
        const ChainRun r = Chain().Run(1, 20);
        NS_TEST_ASSERT_MSG_EQ(r.sent, 20u, "packets sent");
        NS_TEST_EXPECT_MSG_GT(r.ants.at(kInternal), 0u, "internal forward ants each period");
        NS_TEST_EXPECT_MSG_GT(r.ants.at(kBackward), 0u, "backward ants");
        NS_TEST_EXPECT_MSG_GT(r.intraRt1, 0u, "node 1 holds IntraRT cells");
        // Node 3 is three hops from node 0, out of its radius-2 zone: an
        // external forward ant finds it and node 0 keeps the InterRT record.
        NS_TEST_EXPECT_MSG_GT(r.ants.at(kExternal), 0u, "an inter-zone discovery");
        NS_TEST_EXPECT_MSG_GT(r.interRt0, 0u, "node 0 keeps an InterRT record for node 3");
        // The first packet waits for the discovery and goes with the others.
        NS_TEST_EXPECT_MSG_EQ(r.received, r.sent, "every packet delivered");
        NS_TEST_EXPECT_MSG_EQ(r.waiting0, 0u, "nothing left waiting");
    }
};

class DeterminismTestCase : public TestCase {
public:
    DeterminismTestCase() : TestCase("determinism: same seed same trace, another seed another") {}

private:
    void DoRun() override {
        const ChainRun a = Chain().Run(1, 10);
        const ChainRun b = Chain().Run(1, 10);
        const ChainRun c = Chain().Run(2, 10);
        NS_TEST_ASSERT_MSG_GT(a.trace.size(), 0u, "something happened");
        NS_TEST_EXPECT_MSG_EQ((a.trace == b.trace), true, "seed 1 twice: identical");
        NS_TEST_EXPECT_MSG_EQ(a.txDigest, b.txDigest, "seed 1 twice: every packet byte-identical");
        // The first internal ant of each node and the roulette are drawn from
        // the seeded stream, so another seed gives another trace.
        NS_TEST_EXPECT_MSG_EQ((a.trace != c.trace), true, "seed 2: a different trace");
    }
};

class ObserverTestCase : public TestCase {
public:
    ObserverTestCase()
        : TestCase("observer: trace sources and counters, same run with and without them") {}

private:
    void DoRun() override {
        const ChainRun plain = Chain().Run(1, 20);
        ChainRun seen = Chain().Run(1, 20, true);
        NS_TEST_ASSERT_MSG_GT(plain.trace.size(), 0u, "something happened");
        NS_TEST_EXPECT_MSG_EQ((seen.trace == plain.trace), true,
                              "same seed: the same ant and delivery trace");
        NS_TEST_EXPECT_MSG_GT(plain.txPackets, 0u, "packets on the air");
        NS_TEST_EXPECT_MSG_EQ(seen.txPackets, plain.txPackets, "the same packets sent");
        NS_TEST_EXPECT_MSG_EQ(seen.txDigest, plain.txDigest,
                              "same seed: every packet byte-identical with the trace sources "
                              "connected");
        NS_TEST_EXPECT_MSG_EQ(seen.received, plain.received, "same deliveries");

        uint64_t antsSent = 0;
        for (const auto& [type, n] : seen.ants)
            antsSent += n;
        NS_TEST_EXPECT_MSG_EQ(seen.ctlTx, antsSent, "ControlTx per ant sent");
        NS_TEST_EXPECT_MSG_GT(seen.ctlRx, 0u, "ants received");
        NS_TEST_EXPECT_MSG_GT(seen.routeAdds, 0u, "routes added");
        NS_TEST_EXPECT_MSG_GT(seen.stateReports, 0u, "the footprint reported");
        NS_TEST_EXPECT_MSG_EQ(seen.decisions[static_cast<int>(swarm::core::RouteAction::Deliver)],
                              seen.received, "a Deliver per packet received");

        // The counters are kept with or without trace sources connected.
        NS_TEST_EXPECT_MSG_EQ((seen.stats == plain.stats), true,
                              "counters do not depend on traces");
        const auto internal = seen.stats.control.find(static_cast<uint8_t>(kInternal));
        NS_TEST_ASSERT_MSG_EQ((internal != seen.stats.control.end()), true,
                              "internal forward ants counted");
        NS_TEST_EXPECT_MSG_EQ(internal->second.txPackets, seen.ants.at(kInternal),
                              "internal forward ants sent");
        NS_TEST_EXPECT_MSG_EQ(seen.stats.routeAdds, seen.routeAdds, "route adds");
        NS_TEST_EXPECT_MSG_EQ(seen.stats.routeRemoves, seen.routeRemoves, "route removes");
        NS_TEST_EXPECT_MSG_EQ(seen.internalName, "InternalForward", "kind names are registered");
        NS_TEST_EXPECT_MSG_EQ(seen.externalName, "ExternalForward", "kind names are registered");
    }
};

// --- the adapter's guards, through its hooks --------------------------------------

/// ns3::hopnet::RoutingProtocol with its protected hooks callable from a test.
class ProbeProtocol : public ns3::hopnet::RoutingProtocol {
public:
    static TypeId GetTypeId();
    /// RecvControl of a payload from the one-hop neighbour `from`.
    void Control(Ptr<Packet> p, Ipv4Address from) { RecvControl(p, from, Ipv4Address()); }
};

NS_OBJECT_ENSURE_REGISTERED(ProbeProtocol);

TypeId ProbeProtocol::GetTypeId() {
    static TypeId tid = TypeId("ns3::hopnettest::ProbeProtocol")
                            .SetParent<ns3::hopnet::RoutingProtocol>()
                            .SetGroupName("Hopnet")
                            .AddConstructor<ProbeProtocol>();
    return tid;
}

class AdapterGuardsTestCase : public TestCase {
public:
    AdapterGuardsTestCase() : TestCase("adapter: idle protocol, malformed ants ignored") {}

private:
    void DoRun() override {
        std::vector<uint8_t> ant;
        ::hopnet::core::encode(SampleAnt(), ant);

        // Before the protocol starts there are no cores: a control payload is
        // ignored, and nothing is held.
        Ptr<ProbeProtocol> idle = CreateObject<ProbeProtocol>();
        idle->Control(FromBytes(ant), Ipv4Address("10.1.0.2"));
        NS_TEST_EXPECT_MSG_EQ(idle->Waiting(), 0u, "idle: nothing held");
        NS_TEST_EXPECT_MSG_EQ(idle->IntraRtCells(), 0u, "idle: no IntraRT");

        // Two neighbours, both running the probe (node 0 at 10.1.0.1).
        SwarmCoreSeed(1);
        NodeContainer nodes;
        nodes.Create(2);
        NetDeviceContainer devs = LineOfWifiNodes(nodes);
        SwarmRoutingHelper probe("ns3::hopnettest::ProbeProtocol");
        probe.Install(nodes);
        Ipv4AddressHelper a;
        a.SetBase("10.1.0.0", "255.255.255.0");
        Ipv4InterfaceContainer ifs = a.Assign(devs);
        probe.AssignStreams(nodes, 0);
        Ptr<ProbeProtocol> p0 = nodes.Get(0)->GetObject<ProbeProtocol>();
        const Ipv4Address n1 = ifs.GetAddress(1);

        // Before any internal ant (all are due within the first second), a
        // malformed payload changes nothing: a wrong version, a short one.
        Simulator::Schedule(MilliSeconds(1), [&]() {
            const uint32_t cells = p0->IntraRtCells();
            std::vector<uint8_t> wrong = ant;
            wrong[0] = ::hopnet::core::kWireVersion + 1;
            p0->Control(FromBytes(wrong), n1);
            p0->Control(Create<Packet>(3), n1);
            NS_TEST_EXPECT_MSG_EQ(p0->IntraRtCells(), cells, "no cell from a bad ant");
            NS_TEST_EXPECT_MSG_EQ(p0->InterRtRecords(), 0u, "no record from a bad ant");
        });
        Simulator::Stop(Seconds(3.0));
        Simulator::Run();
        // The internal ants made the two nodes neighbours.
        NS_TEST_EXPECT_MSG_GT(p0->IntraRtCells(), 0u, "neighbours found by internal ants");
        Simulator::Destroy();
    }
};

class InputGuardsTestCase : public TestCase {
public:
    InputGuardsTestCase()
        : TestCase("adapter: short UDP payloads and subnet broadcasts reach no header reader") {}

private:
    void DoRun() override {
        SwarmCoreSeed(1);
        NodeContainer nodes;
        nodes.Create(2);
        NetDeviceContainer devs = LineOfWifiNodes(nodes);
        HopnetHelper hopnet;
        hopnet.Install(nodes);
        Ipv4AddressHelper a;
        a.SetBase("10.1.0.0", "255.255.255.0");
        a.Assign(devs);
        Ptr<ns3::hopnet::RoutingProtocol> p1 =
            nodes.Get(1)->GetObject<ns3::hopnet::RoutingProtocol>();
        Simulator::Schedule(MilliSeconds(1), [this, p1, devs]() {
            const auto ucb = MakeCallback(&InputGuardsTestCase::Unicast, this);
            const auto mcb = MakeCallback(&InputGuardsTestCase::Multicast, this);
            const auto lcb = MakeCallback(&InputGuardsTestCase::Deliver, this);
            const auto ecb = MakeCallback(&InputGuardsTestCase::Error, this);
            Ipv4Header h;
            h.SetSource(Ipv4Address("10.1.0.1"));
            h.SetProtocol(17);
            h.SetTtl(64);
            // Three bytes of "UDP" for another node: no UDP header to peek, no
            // DataHeader: refused, nothing read past the payload.
            h.SetDestination(Ipv4Address("10.1.0.3"));
            m_shortTaken = p1->RouteInput(Create<Packet>(3), h, devs.Get(1), ucb, mcb, lcb, ecb);
            // A subnet-directed broadcast: the base's, delivered locally.
            h.SetDestination(Ipv4Address("10.1.0.255"));
            Ptr<Packet> b = Create<Packet>(16);
            UdpHeader udp;
            udp.SetSourcePort(49153);
            udp.SetDestinationPort(9);
            b->AddHeader(udp);
            p1->RouteInput(b, h, devs.Get(1), ucb, mcb, lcb, ecb);
        });
        Simulator::Stop(MilliSeconds(10));
        Simulator::Run();
        Simulator::Destroy();
        NS_TEST_EXPECT_MSG_EQ(m_shortTaken, false, "the short packet is refused");
        NS_TEST_EXPECT_MSG_EQ(m_delivered, 1u, "the subnet broadcast delivered by the base");
    }

    void Unicast(Ptr<Ipv4Route>, Ptr<const Packet>, const Ipv4Header&) {}
    void Multicast(Ptr<Ipv4MulticastRoute>, Ptr<const Packet>, const Ipv4Header&) {}
    void Deliver(Ptr<const Packet>, const Ipv4Header&, uint32_t) { ++m_delivered; }
    void Error(Ptr<const Packet>, const Ipv4Header&, Socket::SocketErrno) {}

    bool m_shortTaken = true;
    uint32_t m_delivered = 0;
};

class HopnetTestSuite : public TestSuite {
public:
    HopnetTestSuite() : TestSuite("hopnet", Type::UNIT) {
        for (const auto& t : ::hopnet::core::test::AllTests()) {
            AddTestCase(new HopnetCoreTestCase(t), TestCase::Duration::QUICK);
        }
        AddTestCase(new WireTestCase, TestCase::Duration::QUICK);
        AddTestCase(new FuzzTestCase, TestCase::Duration::QUICK);
        AddTestCase(new ChainDeliveryTestCase, TestCase::Duration::QUICK);
        AddTestCase(new DeterminismTestCase, TestCase::Duration::QUICK);
        AddTestCase(new ObserverTestCase, TestCase::Duration::QUICK);
        AddTestCase(new AdapterGuardsTestCase, TestCase::Duration::QUICK);
        AddTestCase(new InputGuardsTestCase, TestCase::Duration::QUICK);
    }
};

HopnetTestSuite g_hopnetTestSuite;

}  // namespace
