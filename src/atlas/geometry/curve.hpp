#pragma once

#include "atlas/geometry/tolerance.hpp"

#include <array>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace atlas::geometry {

inline constexpr std::string_view curveKernelVersion = "atlas-curve-kernel-1";

enum class ArcSide { left, right };

struct PolylinePrimitive {
    std::string id;
    std::string startControlPointId;
    std::string endControlPointId;
    Point2D start;
    Point2D end;
};

struct CubicBezierPrimitive {
    std::string id;
    std::array<std::string, 4> controlPointIds;
    std::array<Point2D, 4> controlPoints;
};

struct CircularArcPrimitive {
    std::string id;
    std::string startControlPointId;
    std::string endControlPointId;
    Point2D start;
    Point2D end;
    double radius = 0.0;
    ArcSide side = ArcSide::left;
};

using CurvePrimitive = std::variant<PolylinePrimitive, CubicBezierPrimitive, CircularArcPrimitive>;

struct CurveKernelOptions {
    double coordinateEpsilon = 1.0e-9;
    double stationEpsilon = 1.0e-4;
};

struct CurveEvaluation {
    Point2D position;
    Point2D tangent;
    Point2D normal;
};

struct PrimitiveParameter {
    std::string primitiveId;
    double t = 0.0;
};

class CurveKernel {
public:
    explicit CurveKernel(std::vector<CurvePrimitive> primitives, CurveKernelOptions options = {});

    double totalLength() const noexcept;
    const std::vector<CurvePrimitive>& primitives() const noexcept;

    CurveEvaluation evaluateAtStation(double station) const;
    CurveEvaluation evaluateAtPrimitiveParameter(const std::string& primitiveId, double parameter) const;
    double curvatureAtPrimitiveParameter(const std::string& primitiveId, double parameter) const;
    double stationOfPrimitiveParameter(const std::string& primitiveId, double parameter) const;
    PrimitiveParameter primitiveParameterAtStation(double station) const;
    std::vector<CurveEvaluation> sampleByStationInterval(
        double maximumStationStep,
        std::size_t maximumSamples = 1000000) const;

private:
    std::vector<CurvePrimitive> primitives_;
    std::vector<double> primitiveLengths_;
    std::vector<double> primitiveStationStarts_;
    CurveKernelOptions options_;
    double totalLength_ = 0.0;
};

} // namespace atlas::geometry
