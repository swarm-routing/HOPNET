// Copyright (c) 2026 Daniel Joppi (https://github.com/swarm-routing)
// SPDX-License-Identifier: MIT OR GPL-2.0-only
//
// The HOPNET ant header ([W07] p. 25: Source, Destination, SequenceNo, Type,
// Hops, Path, Time) and its wire codec. The layout is ours (fidelity
// Mechanisms, "Ant header" row; org rule 3): a leading version byte, then
// little-endian fields through swarm-core's WireWriter / WireReader:
//
//   u8  version (kWireVersion)   u8  type        i32 source   i32 destination
//   u32 seq                      u16 hops        f64 time
//   i32 target                   u16 travelled
//   u16 path count, then per entry i32 node + f64 time (swarm-core VisitedPath)
//
// Each Path entry's time is the cumulative Time at which the forward ant left
// that node (fidelity, backward-ant row: T' needs T(S, v_k) at every node).
//
// `target` and `travelled` are not [W07] fields; the inter-zone ants need them
// (version 2). An external ant's Path lists the bordercasters only ([W07]
// p. 28, Fig. 4.1), and between two of them the ant is routed hop by hop
// toward the next one (fidelity, selection row), so every transit relay must
// read which Path node that is: `target`. `travelled` counts every hop, for
// the bound on transit hops (sheet review point 2, proposed on #5).
#ifndef HOPNET_CORE_ANT_H
#define HOPNET_CORE_ANT_H

#include "hopnet-config.h"
#include "swarm-types.h"
#include "swarm-visited-path.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace hopnet::core {

using swarm::core::kInvalidAddress;
using swarm::core::NodeAddress;
using swarm::core::VisitedPath;

/// Bumped whenever a field, its width or its unit changes (org rule 3).
inline constexpr std::uint8_t kWireVersion = 2;

/// The five ant types and their numbers, [W07] p. 25.
enum class AntType : std::uint8_t {
    InternalForward = 0,
    ExternalForward = 1,
    Backward = 2,
    Notification = 3,
    Error = 4,
};

struct Ant {
    AntType type = AntType::InternalForward;
    NodeAddress source = kInvalidAddress;
    /// Blank (kInvalidAddress) for internal forward ants, [W07] p. 25.
    NodeAddress destination = kInvalidAddress;
    std::uint32_t seq = 0;
    /// Internal forward ants: the hop budget left. Backward ants: the hops
    /// from the ant's source to the receiver. External forward ants: unused.
    std::uint16_t hops = 0;
    /// The path time accumulated so far, seconds.
    double time = 0.0;
    VisitedPath path{Config{}.maxPathLength};
    /// External, and external backward, ants: the Path node the ant is being
    /// routed to (a peripheral node, the destination, or the previous Path
    /// node on the way back). kInvalidAddress for the intra-zone ants, which
    /// is how an external backward ant (type 2 too) is told from an internal one.
    NodeAddress target = kInvalidAddress;
    /// External ants: hops travelled since the ant left its source.
    std::uint16_t travelled = 0;

    bool operator==(const Ant&) const = default;
};

/// Appends the encoded ant to `out`.
void encode(const Ant& ant, std::vector<std::uint8_t>& out);

/// Decodes one ant from untrusted bytes: nullopt on a bad version, an unknown
/// type, a Path longer than `maxPathLength`, a short buffer or trailing bytes.
std::optional<Ant> decode(std::span<const std::uint8_t> bytes, std::size_t maxPathLength);

}  // namespace hopnet::core

#endif  // HOPNET_CORE_ANT_H
