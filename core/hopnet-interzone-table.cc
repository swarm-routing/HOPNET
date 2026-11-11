// Copyright (c) 2026 Daniel Joppi (https://github.com/swarm-routing)
// SPDX-License-Identifier: MIT OR GPL-2.0-only
#include "hopnet-interzone-table.h"

#include <algorithm>
#include <utility>

namespace hopnet::core {

InterZoneTable::InterZoneTable(std::size_t capacity, Time lifetime,
                               swarm::core::IRouterObserver* observer)
    : capacity_(std::max<std::size_t>(capacity, 1)), lifetime_(lifetime), observer_(observer) {}

bool InterZoneTable::insert(std::uint32_t seq, std::vector<NodeAddress> path, Time delay, Time now,
                            NodeAddress replaces) {
    if (path.size() < 2) return false;
    const NodeAddress dest = path.back();
    auto it = routes_.find(dest);
    if (it != routes_.end()) {
        if (now < it->second.expire && it->second.seq >= seq) return false;  // not newer
    } else if (routes_.size() >= capacity_) {
        purge(now);
        if (routes_.size() >= capacity_) return false;  // full of live records
    }
    const NodeAddress next = path[1];
    InterRoute& slot = routes_[dest];
    const bool created = slot.path.empty();  // a stored record has a Path of two or more
    const NodeAddress oldNext = created ? swarm::core::kInvalidAddress : slot.path[1];
    pathNodes_ = pathNodes_ - slot.path.size() + path.size();
    slot = InterRoute{.seq = seq,
                      .path = std::move(path),
                      .expire = now + lifetime_,
                      .delay = delay,
                      .replaces = replaces};
    if (observer_ && (created || oldNext != next)) {
        if (!created) observer_->onRouteChanged(dest, oldNext, false);
        observer_->onRouteChanged(dest, next, true);
    }
    return true;
}

const InterRoute* InterZoneTable::find(NodeAddress dest, Time now) const {
    const auto it = routes_.find(dest);
    if (it == routes_.end() || now >= it->second.expire) return nullptr;
    return &it->second;
}

bool InterZoneTable::use(NodeAddress dest, Time now) {
    const auto it = routes_.find(dest);
    if (it == routes_.end() || now >= it->second.expire) return false;
    it->second.expire = now + lifetime_;
    return true;
}

bool InterZoneTable::erase(NodeAddress dest) {
    const auto it = routes_.find(dest);
    if (it == routes_.end()) return false;
    forget(it->first, it->second);
    routes_.erase(it);
    return true;
}

std::size_t InterZoneTable::purge(Time now) {
    return std::erase_if(routes_, [&](const auto& kv) {
        if (now < kv.second.expire) return false;
        forget(kv.first, kv.second);
        return true;
    });
}

void InterZoneTable::forget(NodeAddress dest, const InterRoute& route) {
    pathNodes_ -= route.path.size();
    if (observer_) observer_->onRouteChanged(dest, route.path[1], false);
}

}  // namespace hopnet::core
