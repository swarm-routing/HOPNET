// Copyright (c) 2026 Daniel Joppi (https://github.com/swarm-routing)
// SPDX-License-Identifier: GPL-2.0-only
//
// HOPNET's two wire formats in ns-3 packets. The bytes are core/'s codecs
// (core/hopnet-ant.h, core/hopnet-data.h, versioned, org rule 3); this file
// only moves them in and out of an ns3::Packet. Written fresh for this module
// (HOPNET golden rule 13).
//
// - An ant is the whole payload of a UDP control datagram on the protocol's
//   port: MakeAntPacket / DecodeAntPacket (fail-closed: a wrong version, an
//   unknown type, a Path over the cap, a short buffer or trailing bytes).
// - Every data packet carries a DataHeader in front of its transport header
//   ([W07] p. 27: the packet is source-routed along an InterRT Path; fidelity
//   row "Data to a destination outside the zone"): DataHeader is that
//   ns3::Header, ReadDataHeader reads it without trusting the bytes, and
//   AddDataHeader / StripDataHeader put it on and take it off.
#ifndef HOPNET_HEADER_H
#define HOPNET_HEADER_H

#include "hopnet-ant.h"
#include "hopnet-data.h"

#include "ns3/header.h"
#include "ns3/packet.h"
#include "ns3/ptr.h"

#include <cstddef>
#include <cstdint>
#include <optional>

namespace ns3 {
namespace hopnet {

/// HOPNET's simulator-free core (core/), from inside ns3::hopnet.
namespace core = ::hopnet::core;

/// The core's DataHeader as an ns3::Header (variable length: 17 bytes and 4
/// per route node). Deserialize cannot fail in ns-3's API, so it records
/// whether the bytes decoded (IsValid).
class DataHeader : public Header {
public:
    /// Bytes before the route: version, source, destination, target,
    /// travelled, route count (core/hopnet-data.cc).
    static constexpr uint32_t kFixedSize = 1 + 4 + 4 + 4 + 2 + 2;

    DataHeader() = default;
    explicit DataHeader(const core::DataHeader& h) : m_header(h), m_valid(true) {}

    static TypeId GetTypeId();
    TypeId GetInstanceTypeId() const override;
    uint32_t GetSerializedSize() const override;
    void Serialize(Buffer::Iterator start) const override;
    uint32_t Deserialize(Buffer::Iterator start) override;
    void Print(std::ostream& os) const override;

    const core::DataHeader& Get() const { return m_header; }
    bool IsValid() const { return m_valid; }

private:
    core::DataHeader m_header;
    bool m_valid = false;
    uint32_t m_size = kFixedSize;  ///< bytes Deserialize consumed
};

/// A control datagram holding `ant`.
Ptr<Packet> MakeAntPacket(const core::Ant& ant);

/// The ant a control payload holds, or nullopt (core::decode's rules).
std::optional<core::Ant> DecodeAntPacket(Ptr<const Packet> p, std::size_t maxPathLength);

/// A data payload's leading DataHeader and its size in bytes, or nullopt when
/// the payload does not start with one valid header whose route holds at most
/// `maxRouteLength` nodes. The bytes after it are not read.
struct LeadingData {
    core::DataHeader header;
    uint32_t size = 0;
};
std::optional<LeadingData> ReadDataHeader(Ptr<const Packet> p, std::size_t maxRouteLength);

/// A copy of `payload` (an IPv4 payload: transport header and data) with `h`
/// in front; its tags come along.
Ptr<Packet> AddDataHeader(Ptr<const Packet> payload, const core::DataHeader& h);

/// A copy of `p` without its first `size` bytes (the header ReadDataHeader read).
Ptr<Packet> StripDataHeader(Ptr<const Packet> p, uint32_t size);

}  // namespace hopnet
}  // namespace ns3

#endif  // HOPNET_HEADER_H
