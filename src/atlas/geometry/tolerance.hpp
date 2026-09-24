#pragma once

namespace atlas::geometry {

struct TolerancePolicy {
    double coordinateEpsilon = 1.0e-9;
    double snapDistance = 0.25;
    double angleEpsilonDegrees = 0.5;
};

struct Point2D {
    double x = 0.0;
    double y = 0.0;
};

double distance(Point2D first, Point2D second) noexcept;
Point2D snapToGrid(Point2D point, double gridSpacing, const TolerancePolicy& policy);
Point2D constrainDistance(Point2D origin, Point2D point, double length);
Point2D constrainAngle(Point2D origin, Point2D point, double incrementDegrees);

} // namespace atlas::geometry
