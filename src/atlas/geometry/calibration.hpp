#pragma once

#include "atlas/geometry/tolerance.hpp"

#include <string>

namespace atlas::geometry {

struct CalibrationTransform {
    bool valid = false;
    double scale = 0.0;
    double rotationRadians = 0.0;
    Point2D sourceOrigin;
    Point2D targetOrigin;
    std::string error;

    Point2D apply(Point2D source) const noexcept;
};

CalibrationTransform calibrateTwoPointReference(
    Point2D sourceFirst,
    Point2D sourceSecond,
    Point2D targetFirst,
    Point2D targetSecond,
    double calibrationTolerance);

} // namespace atlas::geometry
