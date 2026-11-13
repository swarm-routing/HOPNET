// Copyright (c) 2026 Daniel Joppi (https://github.com/swarm-routing)
// SPDX-License-Identifier: GPL-2.0-only
//
// Installs HOPNET (ns3::hopnet::RoutingProtocol) through swarm-core's
// SwarmRoutingHelper: the stack, the protocol and the node's monitors (a
// WifiLinkMonitor per Wi-Fi node, whose MAC samples give the edge time and
// whose retry-limit failures reach OnLinkFailure and OnDataUndelivered).
#ifndef HOPNET_HELPER_H
#define HOPNET_HELPER_H

#include "ns3/swarm-routing-helper.h"

namespace ns3 {

class HopnetHelper : public SwarmRoutingHelper {
public:
    HopnetHelper();
    HopnetHelper* Copy() const override;
};

}  // namespace ns3

#endif  // HOPNET_HELPER_H
