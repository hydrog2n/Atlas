#include "atlas/geometry/tolerance.hpp"

#include <algorithm>
#include <cmath>

namespace atlas::geometry {

namespace {

constexpr double radiansPerDegree = 3.14159265358979323846 / 180.0;

} // namespace

double distance(Point2D first, Point2D second) noexcept {
    return std::hypot(first.x - second.x, first.y - second.y);
}

Point2D snapToGrid(Point2D point, double gridSpacing, const TolerancePolicy& policy) {
    if (gridSpacing <= policy.coordinateEpsilon) return point;
    return {
        std::round(point.x / gridSpacing) * gridSpacing,
        std::round(point.y / gridSpacing) * gridSpacing};
}

Point2D constrainDistance(Point2D origin, Point2D point, double length) {
    const auto dx = point.x - origin.x;
    const auto dy = point.y - origin.y;
    const auto currentLength = std::hypot(dx, dy);
    if (currentLength == 0.0) return {origin.x + length, origin.y};
    const auto scale = length / currentLength;
    return {origin.x + dx * scale, origin.y + dy * scale};
}

Point2D constrainAngle(Point2D origin, Point2D point, double incrementDegrees) {
    if (incrementDegrees <= 0.0) return point;
    const auto angle = std::atan2(point.y - origin.y, point.x - origin.x);
    const auto increment = incrementDegrees * radiansPerDegree;
    const auto constrained = std::round(angle / increment) * increment;
    const auto length = distance(origin, point);
    return {origin.x + std::cos(constrained) * length, origin.y + std::sin(constrained) * length};
}

} // namespace atlas::geometry
