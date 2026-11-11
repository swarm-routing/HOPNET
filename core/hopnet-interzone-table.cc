// Copyright (c) 2026 Daniel Joppi (https://github.com/swarm-routing)
// SPDX-License-Identifier: MIT OR GPL-2.0-only
#include "hopnet-interzone-table.h"

#include <algorithm>
#include <utility>

namespace hopnet::core {

InterZoneTable::InterZoneTable(std::size_t capacity, Time lifetime)
    : capacity_(std::max<std::size_t>(capacity, 1)), lifetime_(lifetime) {}

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
    routes_[dest] = InterRoute{.seq = seq,
                               .path = std::move(path),
                               .expire = now + lifetime_,
                               .delay = delay,
                               .replaces = replaces};
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
    return routes_.erase(dest) > 0;
}

std::size_t InterZoneTable::purge(Time now) {
    return std::erase_if(routes_, [now](const auto& kv) { return now >= kv.second.expire; });
}

}  // namespace hopnet::core
