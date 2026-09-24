#include "atlas/geometry/calibration.hpp"

#include <cmath>

namespace atlas::geometry {

CalibrationTransform calibrateTwoPointReference(
    Point2D sourceFirst,
    Point2D sourceSecond,
    Point2D targetFirst,
    Point2D targetSecond,
    double calibrationTolerance) {
    const auto sourceLength = distance(sourceFirst, sourceSecond);
    const auto targetLength = distance(targetFirst, targetSecond);
    if (sourceLength <= calibrationTolerance || targetLength <= calibrationTolerance) {
        return CalibrationTransform{
            false, 0.0, 0.0, sourceFirst, targetFirst,
            "Calibration points must be separated by the calibration tolerance."};
    }

    const auto sourceAngle = std::atan2(sourceSecond.y - sourceFirst.y, sourceSecond.x - sourceFirst.x);
    const auto targetAngle = std::atan2(targetSecond.y - targetFirst.y, targetSecond.x - targetFirst.x);
    return CalibrationTransform{
        true,
        targetLength / sourceLength,
        targetAngle - sourceAngle,
        sourceFirst,
        targetFirst,
        {}};
}

Point2D CalibrationTransform::apply(Point2D source) const noexcept {
    if (!valid) return source;
    const auto dx = source.x - sourceOrigin.x;
    const auto dy = source.y - sourceOrigin.y;
    const auto cosine = std::cos(rotationRadians);
    const auto sine = std::sin(rotationRadians);
    return {
        targetOrigin.x + scale * (dx * cosine - dy * sine),
        targetOrigin.y + scale * (dx * sine + dy * cosine)};
}

} // namespace atlas::geometry
