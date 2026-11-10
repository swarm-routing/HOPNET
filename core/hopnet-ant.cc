// Copyright (c) 2026 Daniel Joppi (https://github.com/swarm-routing)
// SPDX-License-Identifier: MIT OR GPL-2.0-only
#include "hopnet-ant.h"

#include "swarm-wire.h"

namespace hopnet::core {

using swarm::core::WireReader;
using swarm::core::WireWriter;

void encode(const Ant& ant, std::vector<std::uint8_t>& out) {
    WireWriter w(out);
    w.version(kWireVersion);
    w.u8(static_cast<std::uint8_t>(ant.type));
    w.i32(ant.source);
    w.i32(ant.destination);
    w.u32(ant.seq);
    w.u16(ant.hops);
    w.f64(ant.time);
    ant.path.write(w);
}

std::optional<Ant> decode(std::span<const std::uint8_t> bytes, std::size_t maxPathLength) {
    WireReader r(bytes);
    r.expectVersion(kWireVersion);
    Ant ant{.path = VisitedPath(maxPathLength)};
    const std::uint8_t type = r.u8();
    if (type > static_cast<std::uint8_t>(AntType::Error)) r.fail();
    ant.type = static_cast<AntType>(type);
    ant.source = r.i32();
    ant.destination = r.i32();
    ant.seq = r.u32();
    ant.hops = r.u16();
    ant.time = r.f64();
    VisitedPath::read(r, ant.path);
    if (!r.atEnd()) return std::nullopt;
    return ant;
}

}  // namespace hopnet::core
