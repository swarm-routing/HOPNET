// Copyright (c) 2026 Daniel Joppi (https://github.com/swarm-routing)
// SPDX-License-Identifier: GPL-2.0-only
#include "hopnet-header.h"

#include "ns3/udp-header.h"

#include <algorithm>
#include <vector>

namespace ns3 {
namespace hopnet {

NS_OBJECT_ENSURE_REGISTERED(DataHeader);

TypeId DataHeader::GetTypeId() {
    static TypeId tid = TypeId("ns3::hopnet::DataHeader")
                            .SetParent<Header>()
                            .SetGroupName("Hopnet")
                            .AddConstructor<DataHeader>();
    return tid;
}

TypeId DataHeader::GetInstanceTypeId() const {
    return GetTypeId();
}

uint32_t DataHeader::GetSerializedSize() const {
    return m_valid ? kFixedSize + 4 * static_cast<uint32_t>(m_header.route.size()) : m_size;
}

void DataHeader::Serialize(Buffer::Iterator start) const {
    std::vector<uint8_t> bytes;
    core::encode(m_header, bytes);
    start.Write(bytes.data(), static_cast<uint32_t>(bytes.size()));
}

uint32_t DataHeader::Deserialize(Buffer::Iterator start) {
    // The fixed part, then as many route nodes as it announces (or what is left).
    const uint32_t left = start.GetRemainingSize();
    uint32_t n = std::min(left, kFixedSize);
    std::vector<uint8_t> bytes(n);
    if (n > 0) start.Read(bytes.data(), n);
    if (n == kFixedSize) {
        const uint32_t count = bytes[kFixedSize - 2] | (uint32_t{bytes[kFixedSize - 1]} << 8);
        const uint32_t more = std::min(left - n, 4 * count);
        bytes.resize(n + more);
        if (more > 0) start.Read(bytes.data() + n, more);
        n += more;
    }
    const auto h = core::decodeData(bytes, core::Config{}.maxPathLength);
    m_valid = h.has_value();
    m_header = h.value_or(core::DataHeader{});
    m_size = n;
    return n;
}

void DataHeader::Print(std::ostream& os) const {
    if (!m_valid) {
        os << "invalid hopnet data header";
        return;
    }
    os << "src=" << m_header.source << " dst=" << m_header.destination
       << " target=" << m_header.target << " travelled=" << m_header.travelled
       << " route=" << m_header.route.size();
}

Ptr<Packet> MakeAntPacket(const core::Ant& ant) {
    std::vector<uint8_t> bytes;
    core::encode(ant, bytes);
    return Create<Packet>(bytes.data(), static_cast<uint32_t>(bytes.size()));
}

std::optional<core::Ant> DecodeAntPacket(Ptr<const Packet> p, std::size_t maxPathLength) {
    if (!p || p->GetSize() == 0) return std::nullopt;
    std::vector<uint8_t> bytes(p->GetSize());
    p->CopyData(bytes.data(), static_cast<uint32_t>(bytes.size()));
    return core::decode(bytes, maxPathLength);
}

namespace {

constexpr uint8_t kUdp = 17;

/// The bytes of `p` from where the DataHeader sits: after the UDP header for
/// UDP, all of it otherwise. Null when a UDP payload has no UDP header.
Ptr<Packet> AtDataHeader(Ptr<const Packet> p, uint8_t protocol) {
    Ptr<Packet> q = p->Copy();
    if (protocol == kUdp) {
        UdpHeader udp;
        if (q->GetSize() < udp.GetSerializedSize()) return nullptr;
        q->RemoveHeader(udp);
    }
    return q;
}

std::optional<LeadingData> ReadLeading(Ptr<const Packet> p, std::size_t maxRouteLength) {
    if (!p || p->GetSize() < DataHeader::kFixedSize) return std::nullopt;
    std::vector<uint8_t> fixed(DataHeader::kFixedSize);
    p->CopyData(fixed.data(), DataHeader::kFixedSize);
    const uint32_t count =
        fixed[DataHeader::kFixedSize - 2] | (uint32_t{fixed[DataHeader::kFixedSize - 1]} << 8);
    if (count > maxRouteLength) return std::nullopt;
    const uint32_t size = DataHeader::kFixedSize + 4 * count;
    if (p->GetSize() < size) return std::nullopt;
    std::vector<uint8_t> bytes(size);
    p->CopyData(bytes.data(), size);
    const auto h = core::decodeData(bytes, maxRouteLength);
    if (!h) return std::nullopt;
    return LeadingData{*h, size};
}

}  // namespace

std::optional<LeadingData> ReadDataHeader(Ptr<const Packet> p, uint8_t protocol,
                                          std::size_t maxRouteLength) {
    if (!p) return std::nullopt;
    return ReadLeading(AtDataHeader(p, protocol), maxRouteLength);
}

Ptr<Packet> AddDataHeader(Ptr<const Packet> payload, uint8_t protocol, const core::DataHeader& h) {
    Ptr<Packet> q = payload->Copy();
    UdpHeader udp;
    const bool udpFirst = protocol == kUdp && q->GetSize() >= udp.GetSerializedSize();
    if (udpFirst) q->RemoveHeader(udp);
    q->AddHeader(DataHeader(h));
    if (udpFirst) q->AddHeader(udp);
    return q;
}

Ptr<Packet> StripDataHeader(Ptr<const Packet> p, uint8_t protocol, uint32_t size) {
    Ptr<Packet> q = p->Copy();
    UdpHeader udp;
    const bool udpFirst = protocol == kUdp && q->GetSize() >= udp.GetSerializedSize();
    if (udpFirst) q->RemoveHeader(udp);
    q->RemoveAtStart(size);
    if (udpFirst) q->AddHeader(udp);
    return q;
}

}  // namespace hopnet
}  // namespace ns3
