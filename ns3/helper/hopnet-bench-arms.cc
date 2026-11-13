// Copyright (c) 2026 Daniel Joppi (https://github.com/swarm-routing)
// SPDX-License-Identifier: GPL-2.0-only
//
// HOPNET as an arm of swarm-routing/ns3-tools' comparison harness
// (routing-bench, AntHocNet ADR-0027): compiled only when routing-bench is
// installed beside this module (CMakeLists.txt). routing-compare finds this
// file and calls RegisterArms_hopnet() at start-up; the arm's name is `hopnet`.
#include "ns3/hopnet-helper.h"
#include "ns3/routing-bench-arm.h"

namespace ns3 {
namespace hopnet {
namespace {

class HopnetArm : public bench::Arm {
public:
    std::string Name() const override { return "hopnet"; }
    void InstallRouting(InternetStackHelper& internet, bench::RunContext&) override {
        internet.SetRoutingHelper(m_helper);
    }
    int64_t AssignStreams(bench::RunContext& run, int64_t stream) override {
        return m_helper.AssignStreams(run.nodes, stream);
    }
    // InstallRouting only names the helper; the monitors HopnetHelper::Install
    // would add (a WifiLinkMonitor per Wi-Fi node) are added here instead.
    void AfterStack(bench::RunContext& run) override { SwarmMonitorHelper().Install(run.nodes); }

private:
    HopnetHelper m_helper;
};

Ptr<bench::Arm> MakeHopnetArm() {
    return Create<HopnetArm>();
}

}  // namespace
}  // namespace hopnet

namespace bench {

void RegisterArms_hopnet();

void RegisterArms_hopnet() {
    RegisterArm("hopnet", "HOPNET, ant colony routing over zones, on swarm-core (Wang 2007)",
                &hopnet::MakeHopnetArm);
}

}  // namespace bench
}  // namespace ns3
