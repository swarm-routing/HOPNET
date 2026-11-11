// Copyright (c) 2026 Daniel Joppi (https://github.com/swarm-routing)
// SPDX-License-Identifier: MIT OR GPL-2.0-only
//
// What the core tells swarm-core's IRouterObserver (#40), in one place. Every
// helper takes the observer as a pointer and does nothing for null, so a core
// without an observer pays one pointer check and, above all, computes
// nothing: the encoded size of an ant is only taken when somebody listens.
//
// An observer only reads (swarm-core's org rule 5 contract): these helpers
// never change a Decision, a table or a counter of the core.
//
//   control      the ant types are the kinds (kindNames()); the bytes are the
//                encoded size of the ant (encode(), the wire format of
//                hopnet-ant.h). A Decision is "sent" when the core returns it:
//                the adapter carries every returned Decision out.
//   routes       an IntraRT cell (zone node, neighbour) or an InterRT record
//                (destination, next node on its Path) added or removed.
//   data         the message-less decisions of the data entry points.
//   state        IntraRT cells plus InterRT records, and the bytes of what
//                they hold (IntraZone::reportState()).
#ifndef HOPNET_CORE_OBSERVE_H
#define HOPNET_CORE_OBSERVE_H

#include "hopnet-ant.h"
#include "swarm-observer.h"
#include "swarm-route-decision.h"

#include <cstdint>
#include <vector>

namespace hopnet::core {

using swarm::core::IRouterObserver;
using swarm::core::RouteAction;

/// The length of `ant` on the wire.
inline std::uint32_t encodedSize(const Ant& ant) {
    std::vector<std::uint8_t> bytes;
    encode(ant, bytes);
    return static_cast<std::uint32_t>(bytes.size());
}

/// The kind an observer sees for `ant`: its wire type byte.
inline std::uint8_t kindOf(const Ant& ant) {
    return static_cast<std::uint8_t>(ant.type);
}

/// An ant processed here (received from a neighbour).
inline void observeReceived(IRouterObserver* obs, const Ant& ant) {
    if (obs) obs->onControlReceived(kindOf(ant), encodedSize(ant));
}

/// An ant a Decision puts on the medium; other decisions are not control.
inline void observeSent(IRouterObserver* obs, const swarm::core::RouteDecision<Ant>& d) {
    if (!obs || !d.message) return;
    if (d.action == RouteAction::Broadcast || d.action == RouteAction::Unicast) {
        obs->onControlSent(kindOf(*d.message), d.action == RouteAction::Broadcast,
                           encodedSize(*d.message));
    }
}

/// What a data entry point decided for a packet to `dest`: the decisions that
/// carry no ant (Unicast, Queue, Deliver, Drop, DiscardPending).
inline void observeData(IRouterObserver* obs, NodeAddress dest,
                        const swarm::core::RouteDecision<Ant>& d) {
    if (!obs || d.message) return;
    switch (d.action) {
    case RouteAction::Unicast:
    case RouteAction::Queue:
    case RouteAction::Deliver:
    case RouteAction::Drop:
    case RouteAction::DiscardPending:
        obs->onDataDecision(dest, d.action);
        break;
    default:
        break;
    }
}

}  // namespace hopnet::core

#endif  // HOPNET_CORE_OBSERVE_H
