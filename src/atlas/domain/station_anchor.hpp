#pragma once

#include "atlas/geometry/curve.hpp"

#include <string>
#include <vector>

namespace atlas::domain {

enum class AnchorAffinity { startLocked, endLocked, geometryLocked, worldLocked, normalized };

struct StationAnchorSignature {
    double normalizedStation = 0.0;
    std::string primitiveId;
    double primitiveT = 0.0;
    geometry::Point2D worldPosition;
};

struct StationAnchor {
    std::string id;
    double resolvedStation = 0.0;
    AnchorAffinity affinity = AnchorAffinity::geometryLocked;
    StationAnchorSignature remapSignature;
};

struct PrimitiveRemapEntry {
    std::string oldPrimitiveId;
    double oldStart = 0.0;
    double oldEnd = 1.0;
    std::string newPrimitiveId;
    double newStart = 0.0;
    double newEnd = 1.0;
};

struct AnchorRemapResult {
    StationAnchor anchor;
    bool resolved = false;
    bool ambiguous = false;
    std::string diagnostic;
};

AnchorRemapResult remapAnchor(
    const StationAnchor& anchor,
    double oldTotalLength,
    const geometry::CurveKernel& editedCurve,
    const std::vector<PrimitiveRemapEntry>& primitiveRemaps,
    double searchDistance,
    double coordinateEpsilon = 1.0e-9);

} // namespace atlas::domain
