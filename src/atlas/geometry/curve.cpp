#include "atlas/geometry/curve.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <set>
#include <stdexcept>
#include <type_traits>
#include <unordered_map>
#include <utility>

namespace atlas::geometry {

namespace {

constexpr double pi = 3.14159265358979323846;
constexpr unsigned maximumIntegrationDepth = 24;
constexpr unsigned maximumInversionIterations = 64;

Point2D add(Point2D first, Point2D second) {
    return {first.x + second.x, first.y + second.y};
}

Point2D subtract(Point2D first, Point2D second) {
    return {first.x - second.x, first.y - second.y};
}

Point2D multiply(Point2D point, double scalar) {
    return {point.x * scalar, point.y * scalar};
}

double dot(Point2D first, Point2D second) {
    return first.x * second.x + first.y * second.y;
}

double cross(Point2D first, Point2D second) {
    return first.x * second.y - first.y * second.x;
}

double magnitude(Point2D point) {
    return std::hypot(point.x, point.y);
}

Point2D normalized(Point2D point, double epsilon) {
    const auto length = magnitude(point);
    if (!std::isfinite(length) || length <= epsilon) {
        throw std::invalid_argument("Curve tangent is degenerate.");
    }
    return multiply(point, 1.0 / length);
}

bool finite(Point2D point) {
    return std::isfinite(point.x) && std::isfinite(point.y);
}

struct PrimitiveEnds {
    std::string startId;
    std::string endId;
    Point2D start;
    Point2D end;
};

PrimitiveEnds endsOf(const CurvePrimitive& primitive) {
    return std::visit([](const auto& value) -> PrimitiveEnds {
        using T = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<T, CubicBezierPrimitive>) {
            return {value.controlPointIds.front(), value.controlPointIds.back(),
                value.controlPoints.front(), value.controlPoints.back()};
        } else {
            return {value.startControlPointId, value.endControlPointId, value.start, value.end};
        }
    }, primitive);
}

const std::string& primitiveIdOf(const CurvePrimitive& primitive) {
    return std::visit([](const auto& value) -> const std::string& { return value.id; }, primitive);
}

struct ArcData {
    Point2D center;
    double startAngle = 0.0;
    double sweep = 0.0;
};

ArcData arcData(const CircularArcPrimitive& arc, double epsilon) {
    const auto chord = subtract(arc.end, arc.start);
    const auto chordLength = magnitude(chord);
    if (!std::isfinite(arc.radius) || arc.radius <= epsilon || chordLength <= epsilon ||
        arc.radius + epsilon < chordLength * 0.5) {
        throw std::invalid_argument("Fixed-radius arc has an invalid radius or chord.");
    }

    const auto midpoint = multiply(add(arc.start, arc.end), 0.5);
    const auto leftNormal = Point2D{-chord.y / chordLength, chord.x / chordLength};
    const auto halfHeight = std::sqrt(std::max(0.0, arc.radius * arc.radius -
        chordLength * chordLength * 0.25));
    const auto centerSide = arc.side == ArcSide::left ? -1.0 : 1.0;
    const auto center = add(midpoint, multiply(leftNormal, centerSide * halfHeight));
    const auto startAngle = std::atan2(arc.start.y - center.y, arc.start.x - center.x);
    const auto endAngle = std::atan2(arc.end.y - center.y, arc.end.x - center.x);
    auto positiveSweep = std::fmod(endAngle - startAngle + 2.0 * pi, 2.0 * pi);
    if (positiveSweep <= 0.0) {
        throw std::invalid_argument("Fixed-radius arc sweep is not numerically resolvable.");
    }
    const auto middleAngle = startAngle + positiveSweep * 0.5;
    const auto arcMiddle = Point2D{
        center.x + arc.radius * std::cos(middleAngle),
        center.y + arc.radius * std::sin(middleAngle)};
    const auto bulge = cross(chord, subtract(arcMiddle, arc.start));
    const auto wantsLeft = arc.side == ArcSide::left;
    const auto sweep = (wantsLeft ? bulge > 0.0 : bulge < 0.0)
        ? positiveSweep
        : positiveSweep - 2.0 * pi;
    return {center, startAngle, sweep};
}

Point2D bezierPosition(const CubicBezierPrimitive& curve, double t) {
    const auto oneMinusT = 1.0 - t;
    const auto a = oneMinusT * oneMinusT * oneMinusT;
    const auto b = 3.0 * oneMinusT * oneMinusT * t;
    const auto c = 3.0 * oneMinusT * t * t;
    const auto d = t * t * t;
    return {
        a * curve.controlPoints[0].x + b * curve.controlPoints[1].x +
            c * curve.controlPoints[2].x + d * curve.controlPoints[3].x,
        a * curve.controlPoints[0].y + b * curve.controlPoints[1].y +
            c * curve.controlPoints[2].y + d * curve.controlPoints[3].y};
}

Point2D bezierDerivative(const CubicBezierPrimitive& curve, double t) {
    const auto oneMinusT = 1.0 - t;
    const auto first = multiply(subtract(curve.controlPoints[1], curve.controlPoints[0]),
        3.0 * oneMinusT * oneMinusT);
    const auto second = multiply(subtract(curve.controlPoints[2], curve.controlPoints[1]),
        6.0 * oneMinusT * t);
    const auto third = multiply(subtract(curve.controlPoints[3], curve.controlPoints[2]),
        3.0 * t * t);
    return add(add(first, second), third);
}

std::vector<double> quadraticRoots(double a, double b, double c, double epsilon) {
    std::vector<double> roots;
    if (std::abs(a) <= epsilon) {
        if (std::abs(b) > epsilon) roots.push_back(-c / b);
        return roots;
    }
    const auto discriminant = b * b - 4.0 * a * c;
    if (discriminant < 0.0) return roots;
    const auto root = std::sqrt(std::max(0.0, discriminant));
    roots.push_back((-b - root) / (2.0 * a));
    if (root > epsilon) roots.push_back((-b + root) / (2.0 * a));
    return roots;
}

void validateBezierTangent(const CubicBezierPrimitive& curve, double epsilon) {
    const auto p0 = curve.controlPoints[0];
    const auto p1 = curve.controlPoints[1];
    const auto p2 = curve.controlPoints[2];
    const auto p3 = curve.controlPoints[3];
    const auto coefficientA = multiply(add(subtract(p3, multiply(p2, 3.0)),
        add(multiply(p1, 3.0), multiply(p0, -1.0))), 3.0);
    const auto coefficientB = multiply(add(subtract(p2, multiply(p1, 2.0)), p0), 6.0);
    const auto coefficientC = multiply(subtract(p1, p0), 3.0);
    std::vector<double> candidates{0.0, 1.0};
    const auto xRoots = quadraticRoots(coefficientA.x, coefficientB.x, coefficientC.x, epsilon);
    const auto yRoots = quadraticRoots(coefficientA.y, coefficientB.y, coefficientC.y, epsilon);
    candidates.insert(candidates.end(), xRoots.begin(), xRoots.end());
    candidates.insert(candidates.end(), yRoots.begin(), yRoots.end());

    for (const auto candidate : candidates) {
        if (candidate < 0.0 || candidate > 1.0) continue;
        if (magnitude(bezierDerivative(curve, candidate)) <= epsilon) {
            throw std::invalid_argument("Cubic Bezier tangent reaches zero within its domain.");
        }
    }
}

double simpson(double start, double end, double startValue, double middleValue, double endValue) {
    return (end - start) * (startValue + 4.0 * middleValue + endValue) / 6.0;
}

template <typename Function>
double adaptiveSimpson(
    const Function& function,
    double start,
    double end,
    double startValue,
    double middleValue,
    double endValue,
    double whole,
    double tolerance,
    unsigned depth) {
    const auto middle = (start + end) * 0.5;
    const auto leftMiddle = (start + middle) * 0.5;
    const auto rightMiddle = (middle + end) * 0.5;
    const auto leftMiddleValue = function(leftMiddle);
    const auto rightMiddleValue = function(rightMiddle);
    const auto left = simpson(start, middle, startValue, leftMiddleValue, middleValue);
    const auto right = simpson(middle, end, middleValue, rightMiddleValue, endValue);
    const auto difference = left + right - whole;
    if (std::abs(difference) <= 15.0 * tolerance) {
        return left + right + difference / 15.0;
    }
    if (depth == 0) {
        throw std::runtime_error("Cubic Bezier arc-length integration did not converge.");
    }
    return adaptiveSimpson(function, start, middle, startValue, leftMiddleValue, middleValue,
               left, tolerance * 0.5, depth - 1) +
        adaptiveSimpson(function, middle, end, middleValue, rightMiddleValue, endValue,
               right, tolerance * 0.5, depth - 1);
}

double bezierArcLength(const CubicBezierPrimitive& curve, double end, double tolerance) {
    if (end <= 0.0) return 0.0;
    const auto speed = [&](double parameter) {
        const auto value = magnitude(bezierDerivative(curve, parameter));
        if (!std::isfinite(value)) {
            throw std::runtime_error("Cubic Bezier derivative is not finite.");
        }
        return value;
    };
    const auto startValue = speed(0.0);
    const auto endValue = speed(end);
    const auto middle = end * 0.5;
    const auto middleValue = speed(middle);
    const auto whole = simpson(0.0, end, startValue, middleValue, endValue);
    return adaptiveSimpson(speed, 0.0, end, startValue, middleValue, endValue,
        whole, tolerance * 0.25, maximumIntegrationDepth);
}

void validatePrimitive(const CurvePrimitive& primitive, const CurveKernelOptions& options) {
    if (primitiveIdOf(primitive).empty()) {
        throw std::invalid_argument("Curve primitive ID cannot be empty.");
    }
    const auto ends = endsOf(primitive);
    if (ends.startId.empty() || ends.endId.empty() || !finite(ends.start) || !finite(ends.end)) {
        throw std::invalid_argument("Curve primitive endpoints require stable IDs and finite coordinates.");
    }

    std::visit([&](const auto& value) {
        using T = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<T, PolylinePrimitive>) {
            if (distance(value.start, value.end) <= options.coordinateEpsilon) {
                throw std::invalid_argument("Polyline primitive has zero or negligible length.");
            }
        } else if constexpr (std::is_same_v<T, CubicBezierPrimitive>) {
            for (std::size_t index = 0; index < value.controlPoints.size(); ++index) {
                if (value.controlPointIds[index].empty() || !finite(value.controlPoints[index])) {
                    throw std::invalid_argument("Cubic Bezier control points require stable IDs and finite coordinates.");
                }
            }
            if (distance(value.controlPoints.front(), value.controlPoints.back()) <= options.coordinateEpsilon &&
                std::all_of(value.controlPoints.begin(), value.controlPoints.end(), [&](Point2D point) {
                    return distance(point, value.controlPoints.front()) <= options.coordinateEpsilon;
                })) {
                throw std::invalid_argument("Cubic Bezier primitive has zero length.");
            }
            validateBezierTangent(value, options.coordinateEpsilon);
        } else {
            static_cast<void>(arcData(value, options.coordinateEpsilon));
        }
    }, primitive);
}

double primitiveLength(const CurvePrimitive& primitive, const CurveKernelOptions& options) {
    return std::visit([&](const auto& value) -> double {
        using T = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<T, PolylinePrimitive>) {
            return distance(value.start, value.end);
        } else if constexpr (std::is_same_v<T, CubicBezierPrimitive>) {
            return bezierArcLength(value, 1.0, options.stationEpsilon);
        } else {
            const auto data = arcData(value, options.coordinateEpsilon);
            return std::abs(data.sweep) * value.radius;
        }
    }, primitive);
}

double primitiveLengthTo(const CurvePrimitive& primitive, double parameter, const CurveKernelOptions& options) {
    return std::visit([&](const auto& value) -> double {
        using T = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<T, PolylinePrimitive>) {
            return distance(value.start, value.end) * parameter;
        } else if constexpr (std::is_same_v<T, CubicBezierPrimitive>) {
            return bezierArcLength(value, parameter, options.stationEpsilon);
        } else {
            const auto data = arcData(value, options.coordinateEpsilon);
            return std::abs(data.sweep) * value.radius * parameter;
        }
    }, primitive);
}

Point2D primitiveDerivative(const CurvePrimitive& primitive, double parameter, const CurveKernelOptions& options) {
    return std::visit([&](const auto& value) -> Point2D {
        using T = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<T, PolylinePrimitive>) {
            return subtract(value.end, value.start);
        } else if constexpr (std::is_same_v<T, CubicBezierPrimitive>) {
            return bezierDerivative(value, parameter);
        } else {
            const auto data = arcData(value, options.coordinateEpsilon);
            const auto angle = data.startAngle + data.sweep * parameter;
            const auto direction = data.sweep < 0.0 ? -1.0 : 1.0;
            return multiply({-std::sin(angle), std::cos(angle)}, direction);
        }
    }, primitive);
}

double primitiveCurvature(const CurvePrimitive& primitive, double parameter, const CurveKernelOptions& options) {
    return std::visit([&](const auto& value) -> double {
        using T = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<T, PolylinePrimitive>) {
            return 0.0;
        } else if constexpr (std::is_same_v<T, CubicBezierPrimitive>) {
            const auto firstDerivative = bezierDerivative(value, parameter);
            const auto oneMinusT = 1.0 - parameter;
            const auto secondDerivative = multiply(add(
                multiply(add(subtract(value.controlPoints[2], multiply(value.controlPoints[1], 2.0)),
                    value.controlPoints[0]), oneMinusT),
                multiply(add(subtract(value.controlPoints[3], multiply(value.controlPoints[2], 2.0)),
                    value.controlPoints[1]), parameter)), 6.0);
            const auto speed = magnitude(firstDerivative);
            if (!std::isfinite(speed) || speed <= options.coordinateEpsilon) {
                throw std::invalid_argument("Curve curvature is undefined at a degenerate tangent.");
            }
            return cross(firstDerivative, secondDerivative) / (speed * speed * speed);
        } else {
            const auto data = arcData(value, options.coordinateEpsilon);
            return (data.sweep > 0.0 ? 1.0 : -1.0) / value.radius;
        }
    }, primitive);
}

Point2D primitivePosition(const CurvePrimitive& primitive, double parameter, const CurveKernelOptions& options) {
    return std::visit([&](const auto& value) -> Point2D {
        using T = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<T, PolylinePrimitive>) {
            return add(value.start, multiply(subtract(value.end, value.start), parameter));
        } else if constexpr (std::is_same_v<T, CubicBezierPrimitive>) {
            return bezierPosition(value, parameter);
        } else {
            const auto data = arcData(value, options.coordinateEpsilon);
            const auto angle = data.startAngle + data.sweep * parameter;
            return {data.center.x + value.radius * std::cos(angle),
                data.center.y + value.radius * std::sin(angle)};
        }
    }, primitive);
}

} // namespace

CurveKernel::CurveKernel(std::vector<CurvePrimitive> primitives, CurveKernelOptions options)
    : primitives_(std::move(primitives)), options_(options) {
    if (primitives_.empty()) {
        throw std::invalid_argument("Curve kernel requires at least one primitive.");
    }
    if (!std::isfinite(options_.coordinateEpsilon) || options_.coordinateEpsilon <= 0.0 ||
        !std::isfinite(options_.stationEpsilon) || options_.stationEpsilon <= 0.0) {
        throw std::invalid_argument("Curve kernel tolerances must be finite and positive.");
    }

    std::set<std::string> primitiveIds;
    std::unordered_map<std::string, Point2D> controlPointLocations;
    primitiveLengths_.reserve(primitives_.size());
    primitiveStationStarts_.reserve(primitives_.size());
    PrimitiveEnds previousEnds;
    bool first = true;

    for (const auto& primitive : primitives_) {
        validatePrimitive(primitive, options_);
        if (!primitiveIds.insert(primitiveIdOf(primitive)).second) {
            throw std::invalid_argument("Curve primitive IDs must be unique.");
        }
        const auto ends = endsOf(primitive);
        const auto checkControlPoint = [&](const std::string& id, Point2D point) {
            const auto [found, inserted] = controlPointLocations.emplace(id, point);
            if (!inserted && (found->second.x != point.x || found->second.y != point.y)) {
                throw std::invalid_argument("A stable control-point ID cannot identify different coordinates.");
            }
        };
        std::visit([&](const auto& value) {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, CubicBezierPrimitive>) {
                for (std::size_t index = 0; index < value.controlPoints.size(); ++index) {
                    checkControlPoint(value.controlPointIds[index], value.controlPoints[index]);
                }
            } else {
                checkControlPoint(value.startControlPointId, value.start);
                checkControlPoint(value.endControlPointId, value.end);
            }
        }, primitive);

        if (!first && (previousEnds.endId != ends.startId ||
            previousEnds.end.x != ends.start.x || previousEnds.end.y != ends.start.y)) {
            throw std::invalid_argument("Adjacent primitives must share the exact endpoint ID and coordinates.");
        }
        first = false;
        previousEnds = ends;
        primitiveStationStarts_.push_back(totalLength_);
        const auto length = primitiveLength(primitive, options_);
        if (!std::isfinite(length) || length <= options_.coordinateEpsilon) {
            throw std::invalid_argument("Curve primitive length is invalid.");
        }
        primitiveLengths_.push_back(length);
        totalLength_ += length;
        if (!std::isfinite(totalLength_)) {
            throw std::invalid_argument("Curve total length is not finite.");
        }
    }

    const auto firstEnds = endsOf(primitives_.front());
    if (firstEnds.startId == previousEnds.endId ||
        distance(firstEnds.start, previousEnds.end) <= options_.coordinateEpsilon) {
        throw std::invalid_argument("Closed RoadSpline curves are not supported.");
    }
}

double CurveKernel::totalLength() const noexcept {
    return totalLength_;
}

const std::vector<CurvePrimitive>& CurveKernel::primitives() const noexcept {
    return primitives_;
}

CurveEvaluation CurveKernel::evaluateAtStation(double station) const {
    const auto parameter = primitiveParameterAtStation(station);
    const auto found = std::find_if(primitives_.begin(), primitives_.end(), [&](const CurvePrimitive& primitive) {
        return primitiveIdOf(primitive) == parameter.primitiveId;
    });
    const auto position = primitivePosition(*found, parameter.t, options_);
    const auto tangent = normalized(primitiveDerivative(*found, parameter.t, options_), options_.coordinateEpsilon);
    return {position, tangent, {-tangent.y, tangent.x}};
}

CurveEvaluation CurveKernel::evaluateAtPrimitiveParameter(
    const std::string& primitiveId, double parameter) const {
    if (!std::isfinite(parameter) || parameter < 0.0 || parameter > 1.0) {
        throw std::out_of_range("Primitive parameter must be in [0, 1].");
    }
    const auto found = std::find_if(primitives_.begin(), primitives_.end(), [&](const CurvePrimitive& primitive) {
        return primitiveIdOf(primitive) == primitiveId;
    });
    if (found == primitives_.end()) throw std::invalid_argument("Unknown curve primitive ID.");
    const auto tangent = normalized(primitiveDerivative(*found, parameter, options_), options_.coordinateEpsilon);
    return {primitivePosition(*found, parameter, options_), tangent, {-tangent.y, tangent.x}};
}

double CurveKernel::curvatureAtPrimitiveParameter(
    const std::string& primitiveId, double parameter) const {
    if (!std::isfinite(parameter) || parameter < 0.0 || parameter > 1.0) {
        throw std::out_of_range("Primitive parameter must be in [0, 1].");
    }
    const auto found = std::find_if(primitives_.begin(), primitives_.end(), [&](const CurvePrimitive& primitive) {
        return primitiveIdOf(primitive) == primitiveId;
    });
    if (found == primitives_.end()) throw std::invalid_argument("Unknown curve primitive ID.");
    return primitiveCurvature(*found, parameter, options_);
}

double CurveKernel::stationOfPrimitiveParameter(const std::string& primitiveId, double parameter) const {
    if (!std::isfinite(parameter) || parameter < 0.0 || parameter > 1.0) {
        throw std::out_of_range("Primitive parameter must be in [0, 1].");
    }
    const auto found = std::find_if(primitives_.begin(), primitives_.end(), [&](const CurvePrimitive& primitive) {
        return primitiveIdOf(primitive) == primitiveId;
    });
    if (found == primitives_.end()) {
        throw std::invalid_argument("Unknown curve primitive ID.");
    }
    const auto index = static_cast<std::size_t>(std::distance(primitives_.begin(), found));
    return primitiveStationStarts_[index] + primitiveLengthTo(*found, parameter, options_);
}

PrimitiveParameter CurveKernel::primitiveParameterAtStation(double station) const {
    if (!std::isfinite(station) || station < -options_.stationEpsilon ||
        station > totalLength_ + options_.stationEpsilon) {
        throw std::out_of_range("Station is outside the RoadSpline domain.");
    }
    if (station <= options_.stationEpsilon) {
        return {primitiveIdOf(primitives_.front()), 0.0};
    }
    if (totalLength_ - station <= options_.stationEpsilon) {
        return {primitiveIdOf(primitives_.back()), 1.0};
    }

    for (std::size_t index = 0; index < primitives_.size(); ++index) {
        const auto boundary = primitiveStationStarts_[index] + primitiveLengths_[index];
        if (std::abs(station - boundary) <= options_.stationEpsilon) {
            return {primitiveIdOf(primitives_[index]), 1.0};
        }
    }

    std::size_t index = 0;
    while (index + 1 < primitives_.size() && station > primitiveStationStarts_[index] + primitiveLengths_[index]) {
        ++index;
    }
    const auto localStation = station - primitiveStationStarts_[index];
    const auto& primitive = primitives_[index];
    double lower = 0.0;
    double upper = 1.0;
    auto parameter = std::clamp(localStation / primitiveLengths_[index], lower, upper);

    for (unsigned iteration = 0; iteration < maximumInversionIterations; ++iteration) {
        const auto currentStation = primitiveLengthTo(primitive, parameter, options_);
        const auto residual = currentStation - localStation;
        if (std::abs(residual) <= options_.stationEpsilon) {
            return {primitiveIdOf(primitive), parameter};
        }
        if (residual < 0.0) lower = parameter;
        else upper = parameter;

        const auto speed = magnitude(primitiveDerivative(primitive, parameter, options_));
        const auto newton = speed > options_.coordinateEpsilon ? parameter - residual / speed :
            std::numeric_limits<double>::quiet_NaN();
        parameter = std::isfinite(newton) && newton > lower && newton < upper
            ? newton
            : (lower + upper) * 0.5;
    }
    throw std::runtime_error("Station-to-parameter inversion did not converge within 64 iterations.");
}

std::vector<CurveEvaluation> CurveKernel::sampleByStationInterval(
    double maximumStationStep,
    std::size_t maximumSamples) const {
    if (!std::isfinite(maximumStationStep) || maximumStationStep <= 0.0 || maximumSamples == 0) {
        throw std::invalid_argument("Sampling interval and sample limit must be positive.");
    }
    const auto requestedIntervals = std::ceil(totalLength_ / maximumStationStep);
    if (requestedIntervals + 1.0 > static_cast<double>(maximumSamples)) {
        throw std::length_error("Curve sampling exceeds the configured sample limit.");
    }
    std::vector<CurveEvaluation> samples;
    samples.reserve(static_cast<std::size_t>(requestedIntervals) + 1);
    const auto intervalCount = static_cast<std::size_t>(requestedIntervals);
    for (std::size_t index = 0; index < intervalCount; ++index) {
        const auto station = static_cast<double>(index) * maximumStationStep;
        if (station >= totalLength_) break;
        samples.push_back(evaluateAtStation(station));
    }
    samples.push_back(evaluateAtStation(totalLength_));
    return samples;
}

} // namespace atlas::geometry
