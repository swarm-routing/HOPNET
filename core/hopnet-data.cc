// Copyright (c) 2026 Daniel Joppi (https://github.com/swarm-routing)
// SPDX-License-Identifier: MIT OR GPL-2.0-only
#include "hopnet-data.h"

#include "swarm-wire.h"

#include <algorithm>
#include <limits>

namespace hopnet::core {

using swarm::core::WireReader;
using swarm::core::WireWriter;

void encode(const DataHeader& header, std::vector<std::uint8_t>& out) {
    WireWriter w(out);
    w.version(kDataWireVersion);
    w.i32(header.source);
    w.i32(header.destination);
    w.i32(header.target);
    w.u16(header.travelled);
    w.u16(static_cast<std::uint16_t>(header.route.size()));
    for (NodeAddress n : header.route)
        w.i32(n);
}

std::optional<DataHeader> decodeData(std::span<const std::uint8_t> bytes,
                                     std::size_t maxRouteLength) {
    WireReader r(bytes);
    r.expectVersion(kDataWireVersion);
    DataHeader h;
    h.source = r.i32();
    h.destination = r.i32();
    h.target = r.i32();
    h.travelled = r.u16();
    const auto cap = static_cast<std::uint16_t>(
        std::min<std::size_t>(maxRouteLength, std::numeric_limits<std::uint16_t>::max()));
    const std::size_t n = r.count(cap, 4);
    for (std::size_t i = 0; i < n && r.ok(); ++i)
        h.route.push_back(r.i32());
    if (!r.atEnd()) return std::nullopt;
    return h;
}

}  // namespace hopnet::core
