// Copyright (c) 2026 Daniel Joppi (https://github.com/swarm-routing)
// SPDX-License-Identifier: MIT OR GPL-2.0-only
//
// The ant header codec: roundtrip, fail-closed decoding and a seeded fuzz
// (org rule 3).
#include "../hopnet-ant.h"
#include "hopnet-check.h"

namespace hopnet::core::test {

namespace {

Ant sampleAnt() {
    Ant a{.type = AntType::Backward,
          .source = 7,
          .destination = -3,
          .seq = 0xA1B2C3D4u,
          .hops = 2,
          .time = 0.0125,
          .path = VisitedPath(Config{}.maxPathLength)};
    a.path.push(1, 0.001);
    a.path.push(2, 0.004);
    a.path.push(7, 0.0125);
    return a;
}

std::vector<std::uint8_t> bytesOf(const Ant& a) {
    std::vector<std::uint8_t> out;
    encode(a, out);
    return out;
}

}  // namespace

void TestWireRoundtrip(Checker& c) {
    const Ant a = sampleAnt();
    const auto bytes = bytesOf(a);
    c.check(bytes.size() == 1 + 1 + 4 + 4 + 4 + 2 + 8 + 2 + 3 * 12, "frame length");
    c.check(bytes[0] == kWireVersion, "version byte leads");
    c.check(bytes[1] == 2, "type byte: backward is 2 ([W07] p. 25)");
    c.check(bytes[2] == 7 && bytes[3] == 0, "source little-endian");
    c.check(bytes[10] == 0xD4 && bytes[13] == 0xA1, "seq little-endian");
    const auto back = decode(bytes, Config{}.maxPathLength);
    c.check(back.has_value() && *back == a, "decode(encode(a)) == a");
    for (std::uint8_t t = 0; t <= 4; ++t) {
        Ant b = a;
        b.type = static_cast<AntType>(t);
        const auto d = decode(bytesOf(b), Config{}.maxPathLength);
        c.check(d.has_value() && d->type == b.type, "every [W07] type roundtrips");
    }
}

void TestWireFailsClosed(Checker& c) {
    const auto good = bytesOf(sampleAnt());
    for (std::size_t n = 0; n < good.size(); ++n) {
        c.check(!decode(std::span(good).first(n), 100), "a truncated frame fails");
    }
    auto bad = good;
    bad[0] = kWireVersion + 1;
    c.check(!decode(bad, 100), "another wire version fails");
    bad = good;
    bad[1] = 5;
    c.check(!decode(bad, 100), "an unknown type fails");
    bad = good;
    bad.push_back(0);
    c.check(!decode(bad, 100), "trailing bytes fail");
    c.check(!decode(good, 2), "a Path over the cap fails");
    c.check(decode(good, 3).has_value(), "a Path at the cap decodes");
}

void TestWireFuzz(Checker& c) {
    SeededRng rng(20261110);
    int decoded = 0;
    for (int i = 0; i < 4000; ++i) {
        std::vector<std::uint8_t> bytes;
        if (i % 2 == 0) {  // random bytes of random length
            const int n = rng.uniformInt(80);
            for (int k = 0; k < n; ++k)
                bytes.push_back(static_cast<std::uint8_t>(rng.next()));
        } else {  // a valid frame with one byte changed
            bytes = bytesOf(sampleAnt());
            bytes[static_cast<std::size_t>(rng.uniformInt(static_cast<int>(bytes.size())))] ^=
                static_cast<std::uint8_t>(1u + rng.uniformInt(255));
        }
        const auto ant = decode(bytes, 100);
        if (!ant) continue;
        ++decoded;
        c.check(bytesOf(*ant) == bytes, "whatever decodes re-encodes to the same bytes");
    }
    c.check(decoded > 0, "the fuzz reaches the decoded path");
}

}  // namespace hopnet::core::test
