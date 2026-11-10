// Copyright (c) 2026 Daniel Joppi (https://github.com/swarm-routing)
// SPDX-License-Identifier: MIT OR GPL-2.0-only
//
// The HOPNET header a data packet carries. [W07] p. 27 source-routes
// inter-zone data ("self-directed") over the Path of an InterRT record; that
// Path lists the bordercasters only, so the route is loose and each transit
// relay selects a next hop toward the next Path node (fidelity, selection and
// Path rows). The layout is ours (org rule 3), with its own version byte:
//
//   u8  version (kDataWireVersion)   i32 source   i32 destination
//   i32 target                       u16 travelled
//   u16 route count, then per node i32
//
// An intra-zone packet has an empty route and target = destination.
#ifndef HOPNET_CORE_DATA_H
#define HOPNET_CORE_DATA_H

#include "swarm-types.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace hopnet::core {

using swarm::core::kInvalidAddress;
using swarm::core::NodeAddress;

/// Bumped whenever a field, its width or its unit changes (org rule 3).
inline constexpr std::uint8_t kDataWireVersion = 1;

struct DataHeader {
    NodeAddress source = kInvalidAddress;
    NodeAddress destination = kInvalidAddress;
    /// The node the packet is routed to now: the next node of `route`, or the
    /// destination itself.
    NodeAddress target = kInvalidAddress;
    /// Hops travelled since the source.
    std::uint16_t travelled = 0;
    /// The InterRT Path, source first and destination last; empty inside the zone.
    std::vector<NodeAddress> route{};

    bool operator==(const DataHeader&) const = default;
};

/// Appends the encoded header to `out`. A route longer than the wire's u16
/// count cannot be built by the core (it is capped at Config::maxPathLength).
void encode(const DataHeader& header, std::vector<std::uint8_t>& out);

/// Decodes one header from untrusted bytes: nullopt on a bad version, a route
/// longer than `maxRouteLength`, a short buffer or trailing bytes.
std::optional<DataHeader> decodeData(std::span<const std::uint8_t> bytes,
                                     std::size_t maxRouteLength);

}  // namespace hopnet::core

#endif  // HOPNET_CORE_DATA_H
