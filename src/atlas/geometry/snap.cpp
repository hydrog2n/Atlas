#include "atlas/geometry/snap.hpp"

#include <algorithm>

namespace atlas::geometry {

SnapEngine::SnapEngine(TolerancePolicy policy)
    : policy_(policy) {}

const TolerancePolicy& SnapEngine::policy() const noexcept {
    return policy_;
}

int snapPriority(SnapKind kind) noexcept {
    switch (kind) {
    case SnapKind::point: return 0;
    case SnapKind::endpoint: return 1;
    case SnapKind::midpoint: return 2;
    case SnapKind::tangent: return 3;
    case SnapKind::perpendicular: return 4;
    case SnapKind::angle: return 5;
    case SnapKind::distance: return 6;
    case SnapKind::grid: return 7;
    }
    return 8;
}

std::vector<SnapCandidate> SnapEngine::candidates(
    Point2D cursor,
    const std::vector<SnapTarget>& targets) const {
    std::vector<SnapCandidate> result;
    for (const auto& target : targets) {
        const auto targetDistance = distance(cursor, target.position);
        if (targetDistance <= policy_.snapDistance + policy_.coordinateEpsilon) {
            result.push_back({target.id, target.kind, target.position, targetDistance});
        }
    }
    std::sort(result.begin(), result.end(), [](const SnapCandidate& left, const SnapCandidate& right) {
        const auto leftPriority = snapPriority(left.kind);
        const auto rightPriority = snapPriority(right.kind);
        if (leftPriority != rightPriority) return leftPriority < rightPriority;
        if (left.distance != right.distance) return left.distance < right.distance;
        return left.targetId < right.targetId;
    });
    return result;
}

} // namespace atlas::geometry
