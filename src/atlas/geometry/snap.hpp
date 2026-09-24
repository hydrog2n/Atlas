#pragma once

#include "atlas/geometry/tolerance.hpp"

#include <string>
#include <vector>

namespace atlas::geometry {

enum class SnapKind { point, endpoint, midpoint, tangent, perpendicular, angle, distance, grid };

struct SnapTarget {
    std::string id;
    SnapKind kind = SnapKind::point;
    Point2D position;
};

struct SnapCandidate {
    std::string targetId;
    SnapKind kind = SnapKind::point;
    Point2D position;
    double distance = 0.0;
};

class SnapEngine {
public:
    explicit SnapEngine(TolerancePolicy policy = {});

    const TolerancePolicy& policy() const noexcept;
    std::vector<SnapCandidate> candidates(
        Point2D cursor,
        const std::vector<SnapTarget>& targets) const;

private:
    TolerancePolicy policy_;
};

int snapPriority(SnapKind kind) noexcept;

} // namespace atlas::geometry
