#pragma once

#include "atlas/geometry/tolerance.hpp"

#include <string>
#include <vector>

namespace atlas::render {

struct HitTarget {
    std::string objectId;
    std::string type;
    atlas::geometry::Point2D position;
    int precedence = 0;
    bool visible = true;
    bool locked = false;
};

struct HitResult {
    std::string objectId;
    std::string type;
    int precedence = 0;
    double distance = 0.0;
};

class HitTester {
public:
    std::vector<HitResult> orderedHits(
        atlas::geometry::Point2D cursor,
        const std::vector<HitTarget>& targets,
        double tolerance,
        bool includeLocked = false,
        bool includeHidden = false) const;
};

} // namespace atlas::render
