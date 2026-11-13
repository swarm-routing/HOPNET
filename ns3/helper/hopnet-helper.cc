// Copyright (c) 2026 Daniel Joppi (https://github.com/swarm-routing)
// SPDX-License-Identifier: GPL-2.0-only
#include "hopnet-helper.h"

namespace ns3 {

HopnetHelper::HopnetHelper() : SwarmRoutingHelper("ns3::hopnet::RoutingProtocol") {}

HopnetHelper* HopnetHelper::Copy() const {
    return new HopnetHelper(*this);
}

}  // namespace ns3
