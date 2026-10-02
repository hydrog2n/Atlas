#pragma once

#include "atlas/geometry/curve.hpp"

#include <cstddef>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace atlas::geometry {

inline constexpr char roadEnvelopeAlgorithmVersion[] = "atlas-road-envelope-3";
inline constexpr char roadEnvelopeToleranceVersion[] = "atlas-road-envelope-tolerance-3";

enum class JoinStyle { round, miter, bevel };
enum class EnvelopeIssueCode {
    cusp, tightRadius, selfIntersection, orientationInversion, miterLimitExceeded,
    crossSectionDiscontinuity, resourceLimit
};

struct EnvelopeIssue {
    EnvelopeIssueCode code;
    double station = 0.0;
    std::string message;
};

struct RoadEnvelopeOptions {
    double totalWidthMeters = 0.0;
    double maximumStationStep = 0.25;
    double maximumChordErrorMeters = 0.01;
    std::size_t maximumSamples = 512;
    std::size_t maximumPreviewVertices = 2048;
    std::size_t maximumWorkItems = 1000000;
    JoinStyle joinStyle = JoinStyle::round;
    double miterLimit = 0.0;
    double stationEpsilon = 1.0e-4;
    double coordinateEpsilon = 1.0e-9;
};

struct RoadEnvelopeResult {
    bool valid = false;
    std::vector<Point2D> polygon;
    std::vector<Point2D> boundedPreview;
    std::vector<EnvelopeIssue> issues;
    std::string algorithmVersion = roadEnvelopeAlgorithmVersion;
    std::string toleranceVersion = roadEnvelopeToleranceVersion;
    std::string deterministicHash;
    double maximumMeasuredChordDeviationMeters = 0.0;
};

struct RoadEnvelopeCacheLookup {
    std::shared_ptr<const RoadEnvelopeResult> result;
    bool cacheHit = false;
};

class RoadEnvelopeCache {
public:
    explicit RoadEnvelopeCache(std::size_t maximumEntries = 64);
    RoadEnvelopeCacheLookup getOrGenerate(
        const std::string& authoritativeSourceKey,
        const CurveKernel& curve,
        const RoadEnvelopeOptions& options);
    std::size_t size() const noexcept;
    void clear() noexcept;

private:
    std::size_t maximumEntries_;
    std::map<std::string, std::shared_ptr<const RoadEnvelopeResult>> entries_;
};

RoadEnvelopeResult generateRoadEnvelope(
    const CurveKernel& curve,
    const RoadEnvelopeOptions& options);

} // namespace atlas::geometry
