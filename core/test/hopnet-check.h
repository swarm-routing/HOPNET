// Copyright (c) 2026 Daniel Joppi (https://github.com/swarm-routing)
// SPDX-License-Identifier: MIT OR GPL-2.0-only
//
// Dependency-free test support: a failure collector, a seeded RNG and fake
// ports, in the shape of swarm-core's core/test/swarm-core-check.h, so the same
// test functions can later run under ns-3's test.py as well as under ctest.
#ifndef HOPNET_CORE_TEST_CHECK_H
#define HOPNET_CORE_TEST_CHECK_H

#include "swarm-ports.h"

#include <cmath>
#include <cstdint>
#include <source_location>
#include <string>
#include <string_view>
#include <vector>

namespace hopnet::core::test {

class Checker {
public:
    void check(bool ok, std::string_view what,
               std::source_location where = std::source_location::current()) {
        if (ok) return;
        failures_.push_back(std::string(where.file_name()) + ":" + std::to_string(where.line()) +
                            ": " + std::string(what));
    }
    /// |a - b| within 1e-9 relative (plus 1e-12 absolute, for values near 0):
    /// the deposits are sums and quotients of the same doubles taken in a
    /// different order.
    void near(double a, double b, std::string_view what,
              std::source_location where = std::source_location::current()) {
        check(std::fabs(a - b) <= 1e-12 + 1e-9 * std::fmax(std::fabs(a), std::fabs(b)), what,
              where);
    }
    const std::vector<std::string>& failures() const { return failures_; }

private:
    std::vector<std::string> failures_;
};

/// SplitMix64: fully specified, so a seed gives the same sequence everywhere.
class SeededRng : public swarm::core::IRng {
public:
    explicit SeededRng(std::uint64_t seed) : state_(seed) {}
    std::uint64_t next() {
        std::uint64_t z = (state_ += 0x9E3779B97F4A7C15ull);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        return z ^ (z >> 31);
    }
    double uniform() override { return static_cast<double>(next() >> 11) * 0x1.0p-53; }
    int uniformInt(int n) override {
        return n <= 0 ? 0 : static_cast<int>(next() % static_cast<std::uint64_t>(n));
    }

private:
    std::uint64_t state_;
};

class FakeClock : public swarm::core::IClock {
public:
    swarm::core::Time now() const override { return now_; }
    void set(swarm::core::Time t) { now_ = t; }

private:
    swarm::core::Time now_ = 0.0;
};

/// A MAC with a fixed queue length and service time (<= 0: no sample yet).
class FakeLinkState : public swarm::core::ILinkState {
public:
    int queue = 0;
    swarm::core::Time service = 0.0;
    int macQueueLength(swarm::core::NodeAddress) const override { return queue; }
    swarm::core::Time macServiceTime(swarm::core::NodeAddress) const override { return service; }
};

using TestFn = void (*)(Checker&);
struct NamedTest {
    const char* name;
    TestFn run;
};

void TestWireRoundtrip(Checker& c);
void TestWireFailsClosed(Checker& c);
void TestWireFuzz(Checker& c);
void TestTableUpdate(Checker& c);
void TestEdgeTime(Checker& c);
void TestBackwardDeposits(Checker& c);
void TestFloodLine(Checker& c);
void TestFloodDuplicateCopies(Checker& c);
void TestZoneAfterOnePeriod(Checker& c);
void TestNeighbourLoss(Checker& c);
void TestSelection(Checker& c);
void TestBounds(Checker& c);
void TestMalformedAnts(Checker& c);
void TestDeterminism(Checker& c);
void TestBordercastFig41(Checker& c);
void TestBordercastSkipByBestNeighbour(Checker& c);
void TestQueryDetection(Checker& c);
void TestQueryOverhearing(Checker& c);
void TestQueryDelay(Checker& c);
void TestDiscoveryFig41(Checker& c);
void TestIntraZoneData(Checker& c);
void TestDestinationGenerations(Checker& c);
void TestInterRtTable(Checker& c);
void TestInterRtExpiry(Checker& c);
void TestDiscoveryRetry(Checker& c);
void TestZoneNodeMovesAway(Checker& c);
void TestZoneNodeStays(Checker& c);
void TestDataSendFailure(Checker& c);
void TestInterRtHitAtPeripheral(Checker& c);
void TestTravelledBound(Checker& c);
void TestInterZoneMalformed(Checker& c);
void TestDataWire(Checker& c);
void TestInterZoneDeterminism(Checker& c);
void TestRepairSuccess(Checker& c);
void TestRepairFailure(Checker& c);
void TestRepairSources(Checker& c);
void TestNotificationInvalid(Checker& c);
void TestSpliceLoop(Checker& c);
void TestRepairAtTransitRelay(Checker& c);
void TestNoPheromoneIsLinkFailure(Checker& c);
void TestRepairDeterminism(Checker& c);
void TestKindNames(Checker& c);
void TestObserverIntraZone(Checker& c);
void TestObserverControl(Checker& c);
void TestObserverRoutesAndState(Checker& c);
void TestObserverData(Checker& c);
void TestObserverRepair(Checker& c);
void TestObserverSilent(Checker& c);

inline const std::vector<NamedTest>& AllTests() {
    static const std::vector<NamedTest> tests = {
        {"wire-roundtrip", TestWireRoundtrip},
        {"wire-fails-closed", TestWireFailsClosed},
        {"wire-fuzz", TestWireFuzz},
        {"intrart-update", TestTableUpdate},
        {"edge-time", TestEdgeTime},
        {"backward-deposits", TestBackwardDeposits},
        {"flood-line", TestFloodLine},
        {"flood-duplicate-copies", TestFloodDuplicateCopies},
        {"zone-after-one-period", TestZoneAfterOnePeriod},
        {"neighbour-loss", TestNeighbourLoss},
        {"selection", TestSelection},
        {"bounds", TestBounds},
        {"malformed-ants", TestMalformedAnts},
        {"determinism", TestDeterminism},
        {"bordercast-fig41", TestBordercastFig41},
        {"bordercast-skip-best-neighbour", TestBordercastSkipByBestNeighbour},
        {"query-detection", TestQueryDetection},
        {"query-overhearing", TestQueryOverhearing},
        {"query-delay", TestQueryDelay},
        {"discovery-fig41", TestDiscoveryFig41},
        {"intrazone-data", TestIntraZoneData},
        {"destination-generations", TestDestinationGenerations},
        {"interrt-table", TestInterRtTable},
        {"interrt-expiry", TestInterRtExpiry},
        {"discovery-retry", TestDiscoveryRetry},
        {"zone-node-moves-away", TestZoneNodeMovesAway},
        {"zone-node-stays", TestZoneNodeStays},
        {"data-send-failure", TestDataSendFailure},
        {"interrt-hit-at-peripheral", TestInterRtHitAtPeripheral},
        {"travelled-bound", TestTravelledBound},
        {"interzone-malformed", TestInterZoneMalformed},
        {"data-wire", TestDataWire},
        {"interzone-determinism", TestInterZoneDeterminism},
        {"repair-success", TestRepairSuccess},
        {"repair-failure", TestRepairFailure},
        {"repair-sources", TestRepairSources},
        {"notification-invalid", TestNotificationInvalid},
        {"splice-loop", TestSpliceLoop},
        {"repair-at-transit-relay", TestRepairAtTransitRelay},
        {"no-pheromone-is-link-failure", TestNoPheromoneIsLinkFailure},
        {"repair-determinism", TestRepairDeterminism},
        {"kind-names", TestKindNames},
        {"observer-intrazone", TestObserverIntraZone},
        {"observer-control", TestObserverControl},
        {"observer-routes-and-state", TestObserverRoutesAndState},
        {"observer-data", TestObserverData},
        {"observer-repair", TestObserverRepair},
        {"observer-silent", TestObserverSilent},
    };
    return tests;
}

}  // namespace hopnet::core::test

#endif  // HOPNET_CORE_TEST_CHECK_H
