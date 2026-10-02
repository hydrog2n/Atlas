#include "atlas/domain/station_anchor.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace atlas::domain {

namespace {

bool containsPrimitive(const geometry::CurveKernel& curve, const std::string& primitiveId) {
    return std::any_of(curve.primitives().begin(), curve.primitives().end(), [&](const auto& primitive) {
        return std::visit([&](const auto& value) { return value.id == primitiveId; }, primitive);
    });
}

AnchorRemapResult unresolved(
    const StationAnchor& anchor,
    bool ambiguous,
    std::string diagnostic) {
    return {anchor, false, ambiguous, std::move(diagnostic)};
}

AnchorRemapResult resolveAtStation(
    StationAnchor anchor,
    double station,
    const geometry::CurveKernel& curve) {
    if (!std::isfinite(station) || station < 0.0 || station > curve.totalLength()) {
        return unresolved(anchor, false, "Anchor station is outside the edited curve domain.");
    }
    const auto parameter = curve.primitiveParameterAtStation(station);
    const auto evaluation = curve.evaluateAtStation(station);
    anchor.resolvedStation = station;
    anchor.remapSignature.normalizedStation = curve.totalLength() == 0.0
        ? 0.0 : station / curve.totalLength();
    anchor.remapSignature.primitiveId = parameter.primitiveId;
    anchor.remapSignature.primitiveT = parameter.t;
    anchor.remapSignature.worldPosition = evaluation.position;
    return {std::move(anchor), true, false, {}};
}

} // namespace

AnchorRemapResult remapAnchor(
    const StationAnchor& anchor,
    double oldTotalLength,
    const geometry::CurveKernel& editedCurve,
    const std::vector<PrimitiveRemapEntry>& primitiveRemaps,
    double searchDistance,
    double coordinateEpsilon) {
    if (!std::isfinite(oldTotalLength) || oldTotalLength <= 0.0 ||
        !std::isfinite(searchDistance) || searchDistance < 0.0) {
        return unresolved(anchor, false, "Anchor remap inputs are invalid.");
    }

    switch (anchor.affinity) {
    case AnchorAffinity::startLocked:
        return resolveAtStation(anchor, anchor.resolvedStation, editedCurve);
    case AnchorAffinity::endLocked: {
        const auto distanceFromEnd = oldTotalLength - anchor.resolvedStation;
        return resolveAtStation(anchor, editedCurve.totalLength() - distanceFromEnd, editedCurve);
    }
    case AnchorAffinity::normalized:
        return resolveAtStation(anchor,
            anchor.remapSignature.normalizedStation * editedCurve.totalLength(), editedCurve);
    case AnchorAffinity::geometryLocked:
        for (const auto& remap : primitiveRemaps) {
            if (remap.oldPrimitiveId == anchor.remapSignature.primitiveId &&
                anchor.remapSignature.primitiveT >= remap.oldStart - coordinateEpsilon &&
                anchor.remapSignature.primitiveT <= remap.oldEnd + coordinateEpsilon) {
                const auto oldSpan = remap.oldEnd - remap.oldStart;
                if (oldSpan <= coordinateEpsilon) break;
                const auto fraction = (anchor.remapSignature.primitiveT - remap.oldStart) / oldSpan;
                const auto newT = remap.newStart + fraction * (remap.newEnd - remap.newStart);
                if (containsPrimitive(editedCurve, remap.newPrimitiveId)) {
                    return resolveAtStation(anchor,
                        editedCurve.stationOfPrimitiveParameter(remap.newPrimitiveId, newT), editedCurve);
                }
            }
        }
        if (containsPrimitive(editedCurve, anchor.remapSignature.primitiveId)) {
            return resolveAtStation(anchor,
                editedCurve.stationOfPrimitiveParameter(
                    anchor.remapSignature.primitiveId, anchor.remapSignature.primitiveT), editedCurve);
        }
        [[fallthrough]];
    case AnchorAffinity::worldLocked: {
        if (searchDistance < coordinateEpsilon) {
            return unresolved(anchor, false, "World-locked anchor search distance is too small.");
        }
        const auto sampleCount = static_cast<std::size_t>(std::ceil(editedCurve.totalLength() / searchDistance * 8.0)) + 1;
        const auto samples = editedCurve.sampleByStationInterval(searchDistance / 8.0,
            std::min<std::size_t>(sampleCount, 100000));
        const auto projectionTieTolerance = std::max(coordinateEpsilon, searchDistance / 1000.0);
        double bestDistance = std::numeric_limits<double>::infinity();
        double bestStation = 0.0;
        bool tie = false;
        for (std::size_t index = 0; index < samples.size(); ++index) {
            const auto distance = geometry::distance(samples[index].position, anchor.remapSignature.worldPosition);
            if (distance + coordinateEpsilon < bestDistance) {
                bestDistance = distance;
                bestStation = editedCurve.totalLength() * static_cast<double>(index) /
                    static_cast<double>(samples.size() - 1);
                tie = false;
            } else if (std::abs(distance - bestDistance) <= projectionTieTolerance) {
                tie = true;
            }
        }
        if (bestDistance > searchDistance) {
            return unresolved(anchor, false, "No remap candidate is within the configured search distance.");
        }
        if (tie) return unresolved(anchor, true, "Anchor remap has multiple equally valid candidates.");
        return resolveAtStation(anchor, bestStation, editedCurve);
    }
    }
    return unresolved(anchor, false, "Unsupported anchor affinity.");
}

} // namespace atlas::domain
