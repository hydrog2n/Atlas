#include "atlas/application/command.hpp"

#include <stdexcept>
#include <utility>
#include <algorithm>
#include <cmath>
#include <set>

namespace atlas::application {

namespace {

const domain::RoadSpline& findRoadSpline(
    const domain::Project& project,
    const std::string& roadSplineId) {
    const auto found = std::find_if(project.roadSplines().begin(), project.roadSplines().end(),
        [&](const domain::RoadSpline& road) { return road.id == roadSplineId; });
    if (found == project.roadSplines().end()) throw std::invalid_argument("RoadSpline does not exist.");
    return *found;
}

geometry::CurveKernel curveForRoad(const domain::RoadSpline& road) {
    std::vector<geometry::CurvePrimitive> primitives;
    if (!road.primitives.is_array() || road.primitives.empty()) {
        throw std::invalid_argument("RoadSpline requires at least one curve primitive.");
    }
    for (const auto& item : road.primitives) {
        if (!item.is_object()) throw std::invalid_argument("RoadSpline primitive must be an object.");
        const auto kind = item.value("kind", "");
        if (kind == "line" || kind == "polyline") {
            primitives.push_back(geometry::PolylinePrimitive{
                item.value("id", ""), item.value("startControlPointId", ""),
                item.value("endControlPointId", ""),
                {item["start"].value("x", 0.0), item["start"].value("y", 0.0)},
                {item["end"].value("x", 0.0), item["end"].value("y", 0.0)}});
        } else if (kind == "cubic-bezier") {
            const auto ids = item.at("controlPointIds");
            const auto points = item.contains("controlPoints") ? item.at("controlPoints") : item.at("points");
            if (!ids.is_array() || ids.size() != 4 || !points.is_array() || points.size() != 4) {
                throw std::invalid_argument("Cubic Bezier source requires four control-point IDs and positions.");
            }
            std::array<std::string, 4> controlPointIds;
            std::array<geometry::Point2D, 4> controlPoints;
            for (std::size_t index = 0; index < 4; ++index) {
                controlPointIds[index] = ids[index].get<std::string>();
                controlPoints[index] = {points[index].value("x", 0.0), points[index].value("y", 0.0)};
            }
            primitives.push_back(geometry::CubicBezierPrimitive{
                item.value("id", ""), std::move(controlPointIds), std::move(controlPoints)});
        } else if (kind == "circular-arc") {
            primitives.push_back(geometry::CircularArcPrimitive{
                item.value("id", ""), item.value("startControlPointId", ""),
                item.value("endControlPointId", ""),
                {item["start"].value("x", 0.0), item["start"].value("y", 0.0)},
                {item["end"].value("x", 0.0), item["end"].value("y", 0.0)},
                item.value("radius", 0.0), item.value("side", "left") == "left"
                    ? geometry::ArcSide::left : geometry::ArcSide::right});
        } else {
            throw std::invalid_argument("RoadSpline primitive kind is unsupported.");
        }
    }
    return geometry::CurveKernel(std::move(primitives));
}

domain::StationAnchor makeAnchor(
    const geometry::CurveKernel& curve,
    std::string id,
    double station) {
    const auto parameter = curve.primitiveParameterAtStation(station);
    const auto evaluated = curve.evaluateAtStation(station);
    return {std::move(id), station, domain::AnchorAffinity::geometryLocked,
        {station / curve.totalLength(), parameter.primitiveId, parameter.t, evaluated.position}};
}

double anchorStation(const domain::RoadSpline& road, const std::string& anchorId) {
    const auto found = std::find_if(road.stationAnchors.begin(), road.stationAnchors.end(),
        [&](const domain::StationAnchor& anchor) { return anchor.id == anchorId; });
    if (found == road.stationAnchors.end()) throw std::invalid_argument("RoadSegment references a missing StationAnchor.");
    return found->resolvedStation;
}

std::vector<domain::RoadSegment> roadSegmentsFor(
    const domain::Project& project,
    const domain::RoadSpline& road) {
    std::vector<domain::RoadSegment> result;
    for (const auto& id : road.segmentIds) {
        const auto found = std::find_if(project.roadSegments().begin(), project.roadSegments().end(),
            [&](const domain::RoadSegment& segment) { return segment.id == id; });
        if (found == project.roadSegments().end() || found->roadSplineId != road.id) {
            throw std::invalid_argument("RoadSpline segment ID list is inconsistent.");
        }
        result.push_back(*found);
    }
    return result;
}

bool roadSourceChanged(
    const domain::Project& previousProject,
    const domain::Project& candidateProject,
    const domain::RoadSpline& candidateRoad) {
    if (previousProject.geometryPolicy() != candidateProject.geometryPolicy()) return true;
    const auto previousRoad = std::find_if(previousProject.roadSplines().begin(), previousProject.roadSplines().end(),
        [&](const domain::RoadSpline& road) { return road.id == candidateRoad.id; });
    if (previousRoad == previousProject.roadSplines().end() ||
        previousRoad->normalizedJson() != candidateRoad.normalizedJson()) {
        return true;
    }
    const auto segmentRecords = [&](const domain::Project& project) {
        auto records = nlohmann::json::array();
        for (const auto& segment : project.roadSegments()) {
            if (segment.roadSplineId == candidateRoad.id) records.push_back(segment.normalizedJson());
        }
        return records;
    };
    return segmentRecords(previousProject) != segmentRecords(candidateProject);
}

std::string envelopeRuleId(geometry::EnvelopeIssueCode code) {
    switch (code) {
    case geometry::EnvelopeIssueCode::cusp:
    case geometry::EnvelopeIssueCode::tightRadius:
    case geometry::EnvelopeIssueCode::selfIntersection:
    case geometry::EnvelopeIssueCode::orientationInversion:
        return "GEOM-CORE-002";
    case geometry::EnvelopeIssueCode::miterLimitExceeded:
        return "GEOM-CORE-004";
    case geometry::EnvelopeIssueCode::crossSectionDiscontinuity:
        return "GEOM-CORE-003";
    case geometry::EnvelopeIssueCode::resourceLimit:
        return "GEOM-CORE-003";
    }
    return "GEOM-CORE-003";
}

void addBlockingEnvelopeSourceDiagnostic(
    Preview& preview,
    const domain::RoadSpline& road,
    std::string message) {
    preview.diagnostics.push_back(Diagnostic{
        "ROAD-SOURCE-ENVELOPE-INPUT", Severity::error, {road.id}, std::move(message),
        preview.candidate.number, {road.id}, true});
}

void deriveChangedRoadEnvelopes(
    const Revision& current,
    Preview& preview,
    geometry::RoadEnvelopeCache& envelopeCache) {
    for (const auto& road : preview.candidate.project.roadSplines()) {
        if (!roadSourceChanged(current.project, preview.candidate.project, road)) continue;
        geometry::CurveKernel curve = [&]() {
            try {
                return curveForRoad(road);
            } catch (const std::exception& error) {
                addBlockingEnvelopeSourceDiagnostic(preview, road,
                    std::string("RoadSpline source cannot be evaluated: ") + error.what());
                throw;
            }
        }();
        const auto segments = roadSegmentsFor(preview.candidate.project, road);
        if (segments.empty()) {
            addBlockingEnvelopeSourceDiagnostic(preview, road,
                "RoadSpline has no cross-section state from which to derive an envelope.");
            continue;
        }

        geometry::RoadEnvelopeOptions options;
        options.miterLimit = preview.candidate.project.geometryPolicy()["miterLimitRatio"].get<double>();
        std::optional<double> totalWidth;
        bool crossSectionDiscontinuity = false;
        bool sourceInputValid = true;
        for (const auto& segment : segments) {
            const auto& crossSection = segment.crossSectionState;
            if (!crossSection.is_object() || !crossSection.contains("totalWidthMeters") ||
                !crossSection["totalWidthMeters"].is_number()) {
                addBlockingEnvelopeSourceDiagnostic(preview, road,
                    "RoadSegment requires a persisted totalWidthMeters value.");
                sourceInputValid = false;
                break;
            }
            const auto segmentWidth = crossSection["totalWidthMeters"].get<double>();
            if (!std::isfinite(segmentWidth) || segmentWidth <= 0.0) {
                addBlockingEnvelopeSourceDiagnostic(preview, road,
                    "RoadSegment totalWidthMeters must be finite and positive.");
                sourceInputValid = false;
                break;
            }
            if (totalWidth && std::abs(*totalWidth - segmentWidth) > 1.0e-9) {
                crossSectionDiscontinuity = true;
            }
            totalWidth = segmentWidth;
            const auto joinStyle = crossSection.value("joinStyle", "round");
            const auto segmentJoinStyle = joinStyle == "round" ? geometry::JoinStyle::round :
                joinStyle == "bevel" ? geometry::JoinStyle::bevel : geometry::JoinStyle::miter;
            if (joinStyle != "round" && joinStyle != "bevel" && joinStyle != "miter") {
                addBlockingEnvelopeSourceDiagnostic(preview, road,
                    "RoadSegment joinStyle must be round, bevel, or miter.");
                sourceInputValid = false;
                break;
            }
            if (options.joinStyle != geometry::JoinStyle::round && options.joinStyle != segmentJoinStyle) {
                crossSectionDiscontinuity = true;
            }
            if (joinStyle == "round") options.joinStyle = geometry::JoinStyle::round;
            else if (joinStyle == "bevel") options.joinStyle = geometry::JoinStyle::bevel;
            else if (joinStyle == "miter") {
                options.joinStyle = geometry::JoinStyle::miter;
            }
        }
        if (!sourceInputValid || !totalWidth) continue;
        options.totalWidthMeters = *totalWidth;
        geometry::RoadEnvelopeCacheLookup envelopeLookup;
        try {
            nlohmann::json sourceKey{
                {"road", road.normalizedJson()},
                {"geometryPolicy", preview.candidate.project.geometryPolicy()},
                {"segments", nlohmann::json::array()}};
            for (const auto& segment : segments) sourceKey["segments"].push_back(segment.normalizedJson());
            envelopeLookup = envelopeCache.getOrGenerate(sourceKey.dump(), curve, options);
        } catch (const std::exception& error) {
            addBlockingEnvelopeSourceDiagnostic(preview, road,
                std::string("Road envelope options are structurally invalid: ") + error.what());
            continue;
        }
        auto envelope = *envelopeLookup.result;
        if (crossSectionDiscontinuity) {
            envelope.valid = false;
            envelope.polygon.clear();
            envelope.issues.push_back({geometry::EnvelopeIssueCode::crossSectionDiscontinuity, 0.0,
                "A uniform envelope cannot represent differing segment width or join states; the preview is bounded and invalid."});
        }
        for (const auto& issue : envelope.issues) {
            preview.diagnostics.push_back(Diagnostic{
                envelopeRuleId(issue.code), Severity::error, {road.id}, issue.message,
                preview.candidate.number, {road.id}, false});
        }
        preview.roadEnvelopes.push_back({road.id, std::move(envelope), envelopeLookup.cacheHit});
        StationRange affectedRange{0.0, curve.totalLength()};
        for (const auto& invalidation : preview.invalidations) {
            if (invalidation.sourceId == road.id && invalidation.propertyClass == "road-segmentation" &&
                invalidation.stationRange) {
                affectedRange = *invalidation.stationRange;
                break;
            }
        }
        preview.invalidations.push_back(Invalidation{
            road.id, "road-envelope", preview.candidate.number, affectedRange, {}});
    }
}

void validateRoadCoverage(const domain::RoadSpline& road, const geometry::CurveKernel& curve,
    const std::vector<domain::RoadSegment>& segments) {
    if (segments.empty() || segments.size() != road.segmentIds.size()) {
        throw std::invalid_argument("RoadSpline must have ordered RoadSegment coverage.");
    }
    const auto epsilon = 1.0e-4;
    if (std::abs(anchorStation(road, segments.front().startAnchorId)) > epsilon ||
        std::abs(anchorStation(road, segments.back().endAnchorId) - curve.totalLength()) > epsilon) {
        throw std::invalid_argument("RoadSegment coverage must span the complete RoadSpline domain.");
    }
    for (std::size_t index = 0; index < segments.size(); ++index) {
        const auto start = anchorStation(road, segments[index].startAnchorId);
        const auto end = anchorStation(road, segments[index].endAnchorId);
        if (end - start <= epsilon) throw std::invalid_argument("RoadSegment interval must have positive length.");
        if (index > 0 && std::abs(anchorStation(road, segments[index - 1].endAnchorId) - start) > epsilon) {
            throw std::invalid_argument("RoadSegment intervals contain a gap or overlap.");
        }
    }
}

void reverseCrossSectionState(nlohmann::json& state) {
    if (!state.is_object() || !state.contains("elements") || !state["elements"].is_array()) return;
    std::vector<nlohmann::json*> centerElements;
    for (auto& element : state["elements"]) {
        if (!element.is_object()) continue;
        const auto group = element.value("group", "");
        if (group == "left") element["group"] = "right";
        else if (group == "right") element["group"] = "left";
        const auto direction = element.value("travelDirection", "");
        if (direction == "forward") element["travelDirection"] = "reverse";
        else if (direction == "reverse") element["travelDirection"] = "forward";
        if (group == "center" && element.contains("orderKey") && element["orderKey"].is_string()) {
            centerElements.push_back(&element);
        }
    }

    std::sort(centerElements.begin(), centerElements.end(), [](const auto* first, const auto* second) {
        return (*first)["orderKey"].get<std::string>() < (*second)["orderKey"].get<std::string>();
    });
    std::vector<std::string> centerOrderKeys;
    centerOrderKeys.reserve(centerElements.size());
    for (const auto* element : centerElements) centerOrderKeys.push_back((*element)["orderKey"].get<std::string>());
    std::reverse(centerOrderKeys.begin(), centerOrderKeys.end());
    for (std::size_t index = 0; index < centerElements.size(); ++index) {
        (*centerElements[index])["orderKey"] = centerOrderKeys[index];
    }
}

struct SplitCrossSectionState {
    nlohmann::json upstream;
    nlohmann::json downstream;
    std::map<std::string, std::string> downstreamElementIds;
};

SplitCrossSectionState splitCrossSectionState(
    const nlohmann::json& sourceState,
    const std::string& downstreamSegmentId,
    const domain::Project& project) {
    if (!sourceState.is_object() || !sourceState.contains("elements")) {
        return {sourceState, sourceState, {}};
    }
    if (!sourceState["elements"].is_array()) {
        throw std::invalid_argument("Cross-section elements must be an array.");
    }

    std::set<std::string> existingElementIds;
    for (const auto& segment : project.roadSegments()) {
        const auto& elements = segment.crossSectionState.value("elements", nlohmann::json::array());
        if (!elements.is_array()) continue;
        for (const auto& element : elements) {
            if (element.is_object() && element.contains("id") && element["id"].is_string()) {
                existingElementIds.insert(element["id"].get<std::string>());
            }
        }
    }

    auto upstreamState = sourceState;
    auto downstreamState = sourceState;
    downstreamState["elements"] = nlohmann::json::array();
    std::set<std::string> sourceElementIds;
    std::map<std::string, std::string> downstreamElementIds;
    for (const auto& element : sourceState["elements"]) {
        if (!element.is_object() || !element.contains("id") || !element["id"].is_string() ||
            element["id"].get<std::string>().empty()) {
            throw std::invalid_argument("Cross-section elements require stable non-empty IDs.");
        }
        const auto elementId = element["id"].get<std::string>();
        if (!sourceElementIds.insert(elementId).second) {
            throw std::invalid_argument("Cross-section element IDs must be unique within a segment.");
        }
        auto downstreamElement = element;
        const auto downstreamElementId = downstreamSegmentId + "/element/" + elementId;
        if (existingElementIds.contains(downstreamElementId)) {
            throw std::invalid_argument("Generated downstream cross-section element ID already exists.");
        }
        downstreamElement["id"] = downstreamElementId;
        downstreamElementIds[elementId] = downstreamElementId;
        auto lineage = downstreamElement.value("lineage", nlohmann::json::object());
        if (!lineage.is_object()) {
            throw std::invalid_argument("Cross-section element lineage must be an object.");
        }
        lineage["continuesFrom"] = elementId;
        downstreamElement["lineage"] = std::move(lineage);
        downstreamState["elements"].push_back(std::move(downstreamElement));
    }
    return {std::move(upstreamState), std::move(downstreamState), std::move(downstreamElementIds)};
}

void remapCrossSectionElementReferences(
    nlohmann::json& value,
    const std::map<std::string, std::string>& elementIds) {
    if (value.is_array()) {
        for (auto& item : value) remapCrossSectionElementReferences(item, elementIds);
        return;
    }
    if (!value.is_object()) return;
    for (auto iterator = value.begin(); iterator != value.end(); ++iterator) {
        if (iterator.key() == "crossSectionElementId" && iterator.value().is_string()) {
            const auto found = elementIds.find(iterator.value().get<std::string>());
            if (found != elementIds.end()) iterator.value() = found->second;
        } else if (iterator.key() == "crossSectionElementIds" && iterator.value().is_array()) {
            for (auto& id : iterator.value()) {
                if (!id.is_string()) continue;
                const auto found = elementIds.find(id.get<std::string>());
                if (found != elementIds.end()) id = found->second;
            }
        } else {
            remapCrossSectionElementReferences(iterator.value(), elementIds);
        }
    }
}

std::set<std::string> crossSectionElementIds(const nlohmann::json& state) {
    std::set<std::string> ids;
    if (!state.is_object() || !state.contains("elements") || !state["elements"].is_array()) return ids;
    for (const auto& element : state["elements"]) {
        if (element.is_object() && element.contains("id") && element["id"].is_string()) {
            ids.insert(element["id"].get<std::string>());
        }
    }
    return ids;
}

void collectCrossSectionElementReferences(
    const nlohmann::json& value,
    std::set<std::string>& references) {
    if (value.is_array()) {
        for (const auto& item : value) collectCrossSectionElementReferences(item, references);
        return;
    }
    if (!value.is_object()) return;
    for (auto iterator = value.begin(); iterator != value.end(); ++iterator) {
        if (iterator.key() == "crossSectionElementId" && iterator.value().is_string()) {
            references.insert(iterator.value().get<std::string>());
        } else if (iterator.key() == "crossSectionElementIds" && iterator.value().is_array()) {
            for (const auto& id : iterator.value()) {
                if (id.is_string()) references.insert(id.get<std::string>());
            }
        } else {
            collectCrossSectionElementReferences(iterator.value(), references);
        }
    }
}

void validateAttachmentElementReferences(
    const nlohmann::json& attachments,
    const nlohmann::json& state) {
    std::set<std::string> references;
    collectCrossSectionElementReferences(attachments, references);
    const auto availableIds = crossSectionElementIds(state);
    for (const auto& reference : references) {
        if (!availableIds.contains(reference)) {
            throw std::invalid_argument("Station attachment references a missing CrossSectionElement.");
        }
    }
}

bool crossSectionStatesEquivalent(
    const nlohmann::json& upstream,
    const nlohmann::json& downstream) {
    if (upstream == downstream) return true;
    if (!upstream.is_object() || !downstream.is_object() ||
        !upstream.contains("elements") || !downstream.contains("elements") ||
        !upstream["elements"].is_array() || !downstream["elements"].is_array() ||
        upstream["elements"].size() != downstream["elements"].size()) {
        return false;
    }
    auto upstreamState = upstream;
    auto downstreamState = downstream;
    const auto upstreamElements = upstreamState["elements"];
    const auto downstreamElements = downstreamState["elements"];
    upstreamState.erase("elements");
    downstreamState.erase("elements");
    if (upstreamState != downstreamState) return false;

    for (std::size_t index = 0; index < upstreamElements.size(); ++index) {
        auto upstreamElement = upstreamElements[index];
        auto downstreamElement = downstreamElements[index];
        if (!upstreamElement.is_object() || !downstreamElement.is_object() ||
            !upstreamElement.contains("id") || !upstreamElement["id"].is_string() ||
            !downstreamElement.contains("id") || !downstreamElement["id"].is_string()) {
            return false;
        }
        const auto upstreamId = upstreamElement["id"].get<std::string>();
        const auto downstreamLineage = downstreamElement.value("lineage", nlohmann::json::object());
        if (!downstreamLineage.is_object() ||
            downstreamLineage.value("continuesFrom", "") != upstreamId) {
            return false;
        }
        upstreamElement.erase("id");
        upstreamElement.erase("lineage");
        downstreamElement.erase("id");
        downstreamElement.erase("lineage");
        if (upstreamElement != downstreamElement) return false;
    }
    return true;
}

std::map<std::string, std::string> downstreamElementRemaps(
    const nlohmann::json& upstream,
    const nlohmann::json& downstream) {
    std::map<std::string, std::string> remaps;
    if (!upstream.is_object() || !downstream.is_object() ||
        !upstream.contains("elements") || !downstream.contains("elements") ||
        !upstream["elements"].is_array() || !downstream["elements"].is_array() ||
        upstream["elements"].size() != downstream["elements"].size()) {
        return remaps;
    }
    for (std::size_t index = 0; index < upstream["elements"].size(); ++index) {
        const auto& upstreamElement = upstream["elements"][index];
        const auto& downstreamElement = downstream["elements"][index];
        if (!upstreamElement.is_object() || !downstreamElement.is_object() ||
            !upstreamElement.contains("id") || !upstreamElement["id"].is_string() ||
            !downstreamElement.contains("id") || !downstreamElement["id"].is_string()) {
            return {};
        }
        const auto downstreamLineage = downstreamElement.value("lineage", nlohmann::json::object());
        if (!downstreamLineage.is_object() ||
            downstreamLineage.value("continuesFrom", "") != upstreamElement["id"].get<std::string>()) {
            return {};
        }
        remaps[downstreamElement["id"].get<std::string>()] = upstreamElement["id"].get<std::string>();
    }
    return remaps;
}

nlohmann::json combineStationAttachments(
    const nlohmann::json& upstream,
    const nlohmann::json& downstream,
    const std::map<std::string, std::string>& upstreamElementIds,
    const std::map<std::string, std::string>& downstreamElementIds,
    const nlohmann::json& mergedCrossSectionState) {
    if (!upstream.is_array() || !downstream.is_array()) {
        throw std::invalid_argument("Station attachments must be arrays.");
    }
    nlohmann::json combined = nlohmann::json::array();
    std::set<std::string> attachmentIds;
    const std::array<std::pair<const nlohmann::json*, const std::map<std::string, std::string>*>, 2> sources{{
        {&upstream, &upstreamElementIds}, {&downstream, &downstreamElementIds}}};
    for (const auto& [attachments, elementIds] : sources) {
        for (auto attachment : *attachments) {
            if (!attachment.is_object() || !attachment.contains("id") || !attachment["id"].is_string() ||
                attachment["id"].get<std::string>().empty() ||
                !attachmentIds.insert(attachment["id"].get<std::string>()).second) {
                throw std::invalid_argument("Merged station attachment IDs must be valid and unique.");
            }
            remapCrossSectionElementReferences(attachment, *elementIds);
            combined.push_back(std::move(attachment));
        }
    }
    std::sort(combined.begin(), combined.end(), [](const auto& left, const auto& right) {
        return left["id"].get<std::string>() < right["id"].get<std::string>();
    });
    validateAttachmentElementReferences(combined, mergedCrossSectionState);
    return combined;
}

std::pair<nlohmann::json, nlohmann::json> splitStationAttachments(
    const nlohmann::json& sourceAttachments,
    const domain::RoadSpline& road,
    double startStation,
    double endStation,
    double splitStation,
    bool includesTerminalStation) {
    if (!sourceAttachments.is_array()) {
        throw std::invalid_argument("Station attachments must be an array.");
    }
    auto upstreamAttachments = nlohmann::json::array();
    auto downstreamAttachments = nlohmann::json::array();
    std::set<std::string> attachmentIds;
    for (const auto& attachment : sourceAttachments) {
        if (!attachment.is_object() || !attachment.contains("id") || !attachment["id"].is_string() ||
            attachment["id"].get<std::string>().empty() || !attachment.contains("stationAnchorId") ||
            !attachment["stationAnchorId"].is_string()) {
            throw std::invalid_argument("Station attachments require an ID and StationAnchor reference.");
        }
        const auto attachmentId = attachment["id"].get<std::string>();
        if (!attachmentIds.insert(attachmentId).second) {
            throw std::invalid_argument("Station attachment IDs must be unique within a segment.");
        }
        const auto station = anchorStation(road, attachment["stationAnchorId"].get<std::string>());
        if (station < startStation || station > endStation ||
            (!includesTerminalStation && station == endStation)) {
            throw std::invalid_argument("Station attachment is outside its owning segment interval.");
        }
        (station < splitStation ? upstreamAttachments : downstreamAttachments).push_back(attachment);
    }
    return {std::move(upstreamAttachments), std::move(downstreamAttachments)};
}

std::pair<nlohmann::json, nlohmann::json> repartitionStationAttachments(
    const nlohmann::json& upstream,
    const nlohmann::json& downstream,
    const domain::RoadSpline& road,
    double pairStart,
    double newBoundary,
    double pairEnd,
    const std::map<std::string, std::string>& downstreamToUpstreamIds) {
    if (!upstream.is_array() || !downstream.is_array()) {
        throw std::invalid_argument("Station attachments must be arrays.");
    }
    auto upstreamResult = nlohmann::json::array();
    auto downstreamResult = nlohmann::json::array();
    std::set<std::string> attachmentIds;
    const auto appendAttachments = [&](const nlohmann::json& source, bool sourceIsDownstream) {
        for (auto attachment : source) {
            if (!attachment.is_object() || !attachment.contains("id") || !attachment["id"].is_string() ||
                attachment["id"].get<std::string>().empty() || !attachment.contains("stationAnchorId") ||
                !attachment["stationAnchorId"].is_string() ||
                !attachmentIds.insert(attachment["id"].get<std::string>()).second) {
                throw std::invalid_argument("Station attachment IDs and StationAnchor references must be valid.");
            }
            const auto station = anchorStation(road, attachment["stationAnchorId"].get<std::string>());
            if (station < pairStart || station > pairEnd) {
                throw std::invalid_argument("Station attachment is outside the moved boundary's adjacent intervals.");
            }
            const auto belongsDownstream = station >= newBoundary;
            if (sourceIsDownstream != belongsDownstream) {
                std::map<std::string, std::string> elementRemaps = downstreamToUpstreamIds;
                if (belongsDownstream) {
                    elementRemaps.clear();
                    for (const auto& [downstreamId, upstreamId] : downstreamToUpstreamIds) {
                        elementRemaps[upstreamId] = downstreamId;
                    }
                }
                remapCrossSectionElementReferences(attachment, elementRemaps);
            }
            (belongsDownstream ? downstreamResult : upstreamResult).push_back(std::move(attachment));
        }
    };
    appendAttachments(upstream, false);
    appendAttachments(downstream, true);
    const auto sortById = [](nlohmann::json& attachments) {
        std::sort(attachments.begin(), attachments.end(), [](const auto& left, const auto& right) {
            return left["id"].get<std::string>() < right["id"].get<std::string>();
        });
    };
    sortById(upstreamResult);
    sortById(downstreamResult);
    return {std::move(upstreamResult), std::move(downstreamResult)};
}

domain::RoadSpline& roadById(std::vector<domain::RoadSpline>& roads, const std::string& id) {
    const auto found = std::find_if(roads.begin(), roads.end(), [&](const domain::RoadSpline& road) {
        return road.id == id;
    });
    if (found == roads.end()) throw std::invalid_argument("RoadSpline does not exist.");
    return *found;
}

domain::RoadSpline replaceEndpoint(domain::RoadSpline road, geometry::Point2D endpoint) {
    if (!road.primitives.is_array() || road.primitives.empty() || !road.primitives.back().is_object() ||
        !road.primitives.back().contains("end") || !road.primitives.back()["end"].is_object()) {
        throw std::invalid_argument("RoadSpline endpoint editing requires a line-like primitive source record.");
    }
    road.primitives.back()["end"]["x"] = endpoint.x;
    road.primitives.back()["end"]["y"] = endpoint.y;
    return road;
}

double sourceLength(const domain::RoadSpline& road) {
    double length = 0.0;
    for (const auto& primitive : road.primitives) {
        if (!primitive.is_object() || !primitive.contains("start") || !primitive.contains("end")) {
            throw std::invalid_argument("RoadSpline station edits require line-like primitive source records.");
        }
        const auto start = primitive["start"];
        const auto end = primitive["end"];
        length += geometry::distance(
            {start.value("x", 0.0), start.value("y", 0.0)},
            {end.value("x", 0.0), end.value("y", 0.0)});
    }
    if (!std::isfinite(length) || length <= 0.0) throw std::invalid_argument("RoadSpline source length is invalid.");
    return length;
}

std::vector<domain::AnchorRemapResult> remapRoadEditAnchors(
    const domain::RoadSpline& original,
    const domain::RoadSpline& edited,
    const std::map<std::string, double>& stationResolutions) {
    const auto originalCurve = curveForRoad(original);
    const auto editedCurve = curveForRoad(edited);
    std::vector<domain::AnchorRemapResult> results;
    std::vector<std::string> appliedResolutions;
    for (const auto& originalAnchor : original.stationAnchors) {
        const auto retained = std::any_of(edited.stationAnchors.begin(), edited.stationAnchors.end(),
            [&](const auto& candidate) { return candidate.id == originalAnchor.id; });
        if (!retained) continue;
        auto result = domain::remapAnchor(originalAnchor, originalCurve.totalLength(),
            editedCurve, {}, 0.0);
        const auto resolution = stationResolutions.find(originalAnchor.id);
        if (!result.resolved && resolution != stationResolutions.end() &&
            std::isfinite(resolution->second) && resolution->second >= 0.0 &&
            resolution->second <= editedCurve.totalLength()) {
            const auto parameter = editedCurve.primitiveParameterAtStation(resolution->second);
            const auto evaluation = editedCurve.evaluateAtStation(resolution->second);
            result.anchor.resolvedStation = resolution->second;
            result.anchor.remapSignature.normalizedStation = resolution->second / editedCurve.totalLength();
            result.anchor.remapSignature.primitiveId = parameter.primitiveId;
            result.anchor.remapSignature.primitiveT = parameter.t;
            result.anchor.remapSignature.worldPosition = evaluation.position;
            result.resolved = true;
            result.ambiguous = false;
            result.diagnostic.clear();
            appliedResolutions.push_back(originalAnchor.id);
        }
        results.push_back(std::move(result));
    }
    for (const auto& [anchorId, station] : stationResolutions) {
        if (std::find(appliedResolutions.begin(), appliedResolutions.end(), anchorId) ==
            appliedResolutions.end()) {
            throw std::invalid_argument("Anchor station resolution for " + anchorId +
                " does not match an unresolved retained anchor.");
        }
    }
    return results;
}

domain::RoadSpline roadWithRemappedEditAnchors(
    const domain::RoadSpline& original,
    domain::RoadSpline edited,
    const std::vector<domain::AnchorRemapResult>& remaps) {
    std::vector<domain::StationAnchor> remappedAnchors;
    remappedAnchors.reserve(edited.stationAnchors.size());
    for (const auto& remap : remaps) {
        if (remap.resolved) {
            remappedAnchors.push_back(remap.anchor);
        } else {
            const auto originalAnchor = std::find_if(original.stationAnchors.begin(), original.stationAnchors.end(),
                [&](const auto& anchor) { return anchor.id == remap.anchor.id; });
            if (originalAnchor != original.stationAnchors.end()) remappedAnchors.push_back(*originalAnchor);
        }
    }
    for (const auto& candidateAnchor : edited.stationAnchors) {
        const auto existed = std::any_of(original.stationAnchors.begin(), original.stationAnchors.end(),
            [&](const auto& anchor) { return anchor.id == candidateAnchor.id; });
        if (!existed) remappedAnchors.push_back(candidateAnchor);
    }
    edited.stationAnchors = std::move(remappedAnchors);
    return edited;
}

domain::RoadSpline remapEndpointAnchors(
    domain::RoadSpline road,
    double oldLength,
    double newLength,
    ShortenResolution resolution,
    bool reverse) {
    for (auto anchor = road.stationAnchors.begin(); anchor != road.stationAnchors.end();) {
        auto& current = *anchor;
        if (reverse) {
            current.resolvedStation = oldLength - current.resolvedStation;
            current.remapSignature.primitiveT = 1.0 - current.remapSignature.primitiveT;
            if (current.affinity == domain::AnchorAffinity::startLocked) {
                current.affinity = domain::AnchorAffinity::endLocked;
            } else if (current.affinity == domain::AnchorAffinity::endLocked) {
                current.affinity = domain::AnchorAffinity::startLocked;
            }
        }
        if (!reverse && current.affinity == domain::AnchorAffinity::endLocked) {
            current.resolvedStation = newLength;
        }
        if (current.resolvedStation > newLength + 1.0e-4) {
            if (resolution == ShortenResolution::cancel) {
                throw std::invalid_argument("RoadSpline shortening crosses a StationAnchor.");
            }
            if (resolution == ShortenResolution::deleteDependents) {
                anchor = road.stationAnchors.erase(anchor);
                continue;
            }
            current.resolvedStation = newLength;
        }
        current.remapSignature.normalizedStation = current.resolvedStation / newLength;
        ++anchor;
    }
    return road;
}

domain::RoadSpline reverseRoadSource(domain::RoadSpline road) {
    if (!road.primitives.is_array()) throw std::invalid_argument("RoadSpline primitives must be an array.");
    std::reverse(road.primitives.begin(), road.primitives.end());
    for (auto& primitive : road.primitives) {
        if (!primitive.is_object()) throw std::invalid_argument("RoadSpline primitive must be an object.");
        if (primitive.contains("start") && primitive.contains("end")) {
            std::swap(primitive["start"], primitive["end"]);
        }
        if (primitive.contains("startControlPointId") && primitive.contains("endControlPointId")) {
            std::swap(primitive["startControlPointId"], primitive["endControlPointId"]);
        }
        if (primitive.contains("controlPoints") && primitive["controlPoints"].is_array()) {
            std::reverse(primitive["controlPoints"].begin(), primitive["controlPoints"].end());
        }
        if (primitive.contains("controlPointIds") && primitive["controlPointIds"].is_array()) {
            std::reverse(primitive["controlPointIds"].begin(), primitive["controlPointIds"].end());
        }
        if (primitive.value("kind", "") == "circular-arc") {
            primitive["side"] = primitive.value("side", "left") == "left" ? "right" : "left";
        }
    }
    std::reverse(road.segmentIds.begin(), road.segmentIds.end());
    road.direction = road.direction == "start-to-end" ? "end-to-start" : "start-to-end";
    return road;
}

Revision roadRevision(const Revision& current, domain::Project project) {
    return Revision{current.number + 1, std::move(project), current.selection};
}

Invalidation roadSegmentationInvalidation(
    const std::string& roadSplineId,
    std::uint64_t revision,
    double startStation,
    double endStation) {
    return {roadSplineId, "road-segmentation", revision,
        StationRange{startStation, endStation}, {}};
}

} // namespace

bool StationRange::overlaps(const StationRange& other) const noexcept {
    const auto firstStart = std::min(start, end);
    const auto firstEnd = std::max(start, end);
    const auto secondStart = std::min(other.start, other.end);
    const auto secondEnd = std::max(other.start, other.end);
    return firstStart <= secondEnd && secondStart <= firstEnd;
}

std::vector<Invalidation> coalesceInvalidations(std::vector<Invalidation> invalidations) {
    std::vector<Invalidation> result;
    for (auto invalidation : invalidations) {
        bool merged = false;
        for (auto& existing : result) {
            if (existing.sourceId != invalidation.sourceId ||
                existing.propertyClass != invalidation.propertyClass ||
                existing.revision != invalidation.revision ||
                existing.spatialRegion != invalidation.spatialRegion) {
                continue;
            }
            if (!existing.stationRange || !invalidation.stationRange) {
                existing.stationRange.reset();
                merged = true;
                break;
            }
            if (existing.stationRange->overlaps(*invalidation.stationRange)) {
                existing.stationRange = StationRange{
                    std::min({existing.stationRange->start, existing.stationRange->end,
                        invalidation.stationRange->start, invalidation.stationRange->end}),
                    std::max({existing.stationRange->start, existing.stationRange->end,
                        invalidation.stationRange->start, invalidation.stationRange->end})};
                merged = true;
                break;
            }
        }
        if (!merged) result.push_back(std::move(invalidation));
    }
    return result;
}

// Return the authoritative source representation used for deterministic comparisons.
std::string Revision::normalizedSource() const {
    // Selection and caches are intentionally excluded from authoritative comparison.
    return project.normalizedJson();
}

// Store the project and the revision against which this command may run.
ReplaceProjectCommand::ReplaceProjectCommand(
    domain::Project project,
    std::optional<std::uint64_t> expectedRevision)
    : project_(std::move(project)), expectedRevision_(expectedRevision) {}

// Identify the generic project replacement command in history and diagnostics.
const char* ReplaceProjectCommand::name() const noexcept {
    // This command is the generic property-edit placeholder for the foundation slice.
    return "replace-project";
}

// Return the revision precondition supplied by the caller.
std::optional<std::uint64_t> ReplaceProjectCommand::expectedRevision() const noexcept {
    return expectedRevision_;
}

// Group consecutive replacement edits into one undo entry.
std::string ReplaceProjectCommand::coalesceKey() const {
    // Consecutive replacements represent one continuous edit in the history.
    return "replace-project";
}

// Store the repaired project and the revision it is allowed to modify.
RepairProjectCommand::RepairProjectCommand(
    domain::Project repairedProject,
    std::uint64_t expectedRevision)
    : repairedProject_(std::move(repairedProject)), expectedRevision_(expectedRevision) {}

// Identify the repair command in history and diagnostics.
const char* RepairProjectCommand::name() const noexcept {
    // Repairs use the same command path as ordinary mutations.
    return "repair-project";
}

// Return the repair command's required source revision.
std::optional<std::uint64_t> RepairProjectCommand::expectedRevision() const noexcept {
    return expectedRevision_;
}

// Keep repairs as independent history entries instead of coalescing them.
std::string RepairProjectCommand::coalesceKey() const {
    // Repairs remain separate history entries so each repair can be undone independently.
    return {};
}

std::vector<Diagnostic> Command::validate(const Revision&) const {
    return {};
}

std::vector<Invalidation> Command::invalidations(
    const Revision&,
    const Revision& candidate) const {
    return {Invalidation{candidate.project.id(), "project-source", candidate.number, {}, {}}};
}

// Produce a new revision containing the repaired authoritative project.
Revision RepairProjectCommand::apply(const Revision& current) const {
    return Revision{current.number + 1, repairedProject_, current.selection};
}

CreateMapObjectCommand::CreateMapObjectCommand(
    domain::MapObject object,
    std::uint64_t expectedRevision)
    : object_(std::move(object)), expectedRevision_(expectedRevision) {}

const char* CreateMapObjectCommand::name() const noexcept { return "create-map-object"; }
std::optional<std::uint64_t> CreateMapObjectCommand::expectedRevision() const noexcept {
    return expectedRevision_;
}
std::string CreateMapObjectCommand::coalesceKey() const { return {}; }
Revision CreateMapObjectCommand::apply(const Revision& current) const {
    if (current.project.rootMap().findObject(object_.id()) != nullptr) {
        throw std::invalid_argument("MapObject already exists.");
    }
    auto map = current.project.rootMap().withObject(object_);
    return Revision{current.number + 1, current.project.withRootMap(std::move(map)), current.selection};
}

EditMapObjectCommand::EditMapObjectCommand(
    domain::MapObject object,
        std::uint64_t expectedRevision,
        bool coalesceWithPriorEdit)
        : object_(std::move(object)), expectedRevision_(expectedRevision),
            coalesceWithPriorEdit_(coalesceWithPriorEdit) {}

const char* EditMapObjectCommand::name() const noexcept { return "edit-map-object"; }
std::optional<std::uint64_t> EditMapObjectCommand::expectedRevision() const noexcept {
    return expectedRevision_;
}
std::string EditMapObjectCommand::coalesceKey() const {
    return coalesceWithPriorEdit_ ? "edit-map-object:" + object_.id() : std::string{};
}
Revision EditMapObjectCommand::apply(const Revision& current) const {
    if (current.project.rootMap().findObject(object_.id()) == nullptr) {
        throw std::invalid_argument("MapObject does not exist.");
    }
    auto map = current.project.rootMap().withObject(object_);
    return Revision{current.number + 1, current.project.withRootMap(std::move(map)), current.selection};
}

DeleteMapObjectCommand::DeleteMapObjectCommand(
    std::string objectId,
    std::uint64_t expectedRevision)
    : objectId_(std::move(objectId)), expectedRevision_(expectedRevision) {}

const char* DeleteMapObjectCommand::name() const noexcept { return "delete-map-object"; }
std::optional<std::uint64_t> DeleteMapObjectCommand::expectedRevision() const noexcept {
    return expectedRevision_;
}
std::string DeleteMapObjectCommand::coalesceKey() const { return {}; }
Revision DeleteMapObjectCommand::apply(const Revision& current) const {
    if (current.project.rootMap().findObject(objectId_) == nullptr) {
        throw std::invalid_argument("MapObject does not exist.");
    }
    auto map = current.project.rootMap().withoutObject(objectId_);
    return Revision{current.number + 1, current.project.withRootMap(std::move(map)), current.selection};
}

CreateRoadSplineCommand::CreateRoadSplineCommand(
        domain::RoadSpline road, std::uint64_t expectedRevision, double placeholderWidthMeters)
        : road_(std::move(road)), expectedRevision_(expectedRevision),
            placeholderWidthMeters_(placeholderWidthMeters) {}
const char* CreateRoadSplineCommand::name() const noexcept { return "create-road-spline"; }
std::optional<std::uint64_t> CreateRoadSplineCommand::expectedRevision() const noexcept { return expectedRevision_; }
std::string CreateRoadSplineCommand::coalesceKey() const { return {}; }
Revision CreateRoadSplineCommand::apply(const Revision& current) const {
    if (!std::isfinite(placeholderWidthMeters_) || placeholderWidthMeters_ <= 0.0) {
        throw std::invalid_argument("Uniform placeholder width must be finite and positive.");
    }
    auto road = road_;
    if (!road.segmentIds.empty()) {
        throw std::invalid_argument("New RoadSpline must not supply pre-existing RoadSegments.");
    }
    const auto curve = curveForRoad(road);
    const auto startAnchorId = road.id + "/anchor/start";
    const auto endAnchorId = road.id + "/anchor/end";
    if (std::any_of(road.stationAnchors.begin(), road.stationAnchors.end(), [&](const domain::StationAnchor& anchor) {
        return anchor.id == startAnchorId || anchor.id == endAnchorId;
    })) {
        throw std::invalid_argument("Initial RoadSpline anchor IDs collide with generated endpoint anchors.");
    }
    road.stationAnchors.push_back(makeAnchor(curve, startAnchorId, 0.0));
    road.stationAnchors.push_back(makeAnchor(curve, endAnchorId, curve.totalLength()));
    road.stationAnchors.front().affinity = domain::AnchorAffinity::startLocked;
    road.stationAnchors.back().affinity = domain::AnchorAffinity::endLocked;
    const domain::RoadSegment segment{
        road.id + "/segment/initial", road.mapId, road.id, startAnchorId, endAnchorId,
        {{"kind", "uniform-placeholder"}, {"totalWidthMeters", placeholderWidthMeters_}, {"joinStyle", "round"}},
        {{"relation", "initial"}}};
    road.segmentIds = {segment.id};
    validateRoadCoverage(road, curve, {segment});
    const auto project = current.project.withRoadSplineAndSegments(std::move(road), {segment});
    return roadRevision(current, project);
}

EditRoadSplineCommand::EditRoadSplineCommand(
        domain::RoadSpline road,
        std::uint64_t expectedRevision,
        bool coalesceWithPriorEdit,
        std::map<std::string, double> anchorStationResolutions)
        : road_(std::move(road)), expectedRevision_(expectedRevision),
            coalesceWithPriorEdit_(coalesceWithPriorEdit),
            anchorStationResolutions_(std::move(anchorStationResolutions)) {}
const char* EditRoadSplineCommand::name() const noexcept { return "edit-road-spline"; }
std::optional<std::uint64_t> EditRoadSplineCommand::expectedRevision() const noexcept { return expectedRevision_; }
std::string EditRoadSplineCommand::coalesceKey() const {
    return coalesceWithPriorEdit_ ? "edit-road-spline:" + road_.id : std::string{};
}
std::vector<Diagnostic> EditRoadSplineCommand::validate(const Revision& current) const {
    try {
        const auto& original = findRoadSpline(current.project, road_.id);
        const auto remaps = remapRoadEditAnchors(original, road_, anchorStationResolutions_);
        std::vector<std::string> unresolvedIds;
        for (const auto& remap : remaps) {
            if (!remap.resolved) unresolvedIds.push_back(remap.anchor.id);
        }
        if (!unresolvedIds.empty()) {
            return {Diagnostic{"STAT-CORE-003", Severity::error, unresolvedIds,
                "RoadSpline edit cannot unambiguously remap every retained StationAnchor.",
                current.number, {road_.id}, true}};
        }
    } catch (const std::exception& error) {
        return {Diagnostic{"VAL-CORE-002", Severity::error, {road_.id}, error.what(),
            current.number, {road_.id}, true}};
    }
    return {};
}
Revision EditRoadSplineCommand::apply(const Revision& current) const {
    const auto& original = findRoadSpline(current.project, road_.id);
    const auto remaps = remapRoadEditAnchors(original, road_, anchorStationResolutions_);
    auto updated = roadWithRemappedEditAnchors(original, road_, remaps);
    return roadRevision(current, current.project.withReplacedRoadSpline(std::move(updated)));
}

ExtendRoadSplineCommand::ExtendRoadSplineCommand(
    std::string roadSplineId, geometry::Point2D newEnd, std::uint64_t expectedRevision)
    : roadSplineId_(std::move(roadSplineId)), newEnd_(newEnd), expectedRevision_(expectedRevision) {}
const char* ExtendRoadSplineCommand::name() const noexcept { return "extend-road-spline"; }
std::optional<std::uint64_t> ExtendRoadSplineCommand::expectedRevision() const noexcept { return expectedRevision_; }
std::string ExtendRoadSplineCommand::coalesceKey() const { return "extend-road-spline:" + roadSplineId_; }
Revision ExtendRoadSplineCommand::apply(const Revision& current) const {
    return roadRevision(current, current.project.withReplacedRoadSpline(
        replaceEndpoint(findRoadSpline(current.project, roadSplineId_), newEnd_)));
}

ShortenRoadSplineCommand::ShortenRoadSplineCommand(
        std::string roadSplineId, geometry::Point2D newEnd, std::uint64_t expectedRevision,
        ShortenResolution resolution)
        : roadSplineId_(std::move(roadSplineId)), newEnd_(newEnd), expectedRevision_(expectedRevision),
            resolution_(resolution) {}
const char* ShortenRoadSplineCommand::name() const noexcept { return "shorten-road-spline"; }
std::optional<std::uint64_t> ShortenRoadSplineCommand::expectedRevision() const noexcept { return expectedRevision_; }
std::string ShortenRoadSplineCommand::coalesceKey() const { return "shorten-road-spline:" + roadSplineId_; }
Revision ShortenRoadSplineCommand::apply(const Revision& current) const {
    const auto& original = findRoadSpline(current.project, roadSplineId_);
    const auto oldLength = sourceLength(original);
    auto updated = replaceEndpoint(original, newEnd_);
    const auto newLength = sourceLength(updated);
    if (newLength >= oldLength) throw std::invalid_argument("Shorten command must reduce RoadSpline length.");
    updated = remapEndpointAnchors(std::move(updated), oldLength, newLength, resolution_, false);
    return roadRevision(current, current.project.withReplacedRoadSpline(std::move(updated)));
}

ReverseRoadSplineCommand::ReverseRoadSplineCommand(std::string roadSplineId, std::uint64_t expectedRevision)
    : roadSplineId_(std::move(roadSplineId)), expectedRevision_(expectedRevision) {}
const char* ReverseRoadSplineCommand::name() const noexcept { return "reverse-road-spline"; }
std::optional<std::uint64_t> ReverseRoadSplineCommand::expectedRevision() const noexcept { return expectedRevision_; }
std::string ReverseRoadSplineCommand::coalesceKey() const { return {}; }
Revision ReverseRoadSplineCommand::apply(const Revision& current) const {
    const auto& original = findRoadSpline(current.project, roadSplineId_);
    const auto length = sourceLength(original);
    auto reversed = reverseRoadSource(original);
    reversed = remapEndpointAnchors(std::move(reversed), length, length, ShortenResolution::moveDependents, true);
    auto segments = roadSegmentsFor(current.project, original);
    std::reverse(segments.begin(), segments.end());
    for (auto& segment : segments) {
        std::swap(segment.startAnchorId, segment.endAnchorId);
        reverseCrossSectionState(segment.crossSectionState);
    }
    validateRoadCoverage(reversed, curveForRoad(reversed), segments);
    return roadRevision(current,
        current.project.withReplacedRoadTopology(std::move(reversed), std::move(segments)));
}

DeleteRoadSplineCommand::DeleteRoadSplineCommand(
    std::string roadSplineId, std::uint64_t expectedRevision, RoadDeleteResolution resolution)
    : roadSplineId_(std::move(roadSplineId)), expectedRevision_(expectedRevision), resolution_(resolution) {}
const char* DeleteRoadSplineCommand::name() const noexcept { return "delete-road-spline"; }
std::optional<std::uint64_t> DeleteRoadSplineCommand::expectedRevision() const noexcept { return expectedRevision_; }
std::string DeleteRoadSplineCommand::coalesceKey() const { return {}; }
Revision DeleteRoadSplineCommand::apply(const Revision& current) const {
    const auto project = resolution_ == RoadDeleteResolution::deleteOwnedSegments
        ? current.project.withoutRoadSplineAndSegments(roadSplineId_)
        : current.project.withoutRoadSpline(roadSplineId_);
    return roadRevision(current, project);
}

SplitRoadSegmentCommand::SplitRoadSegmentCommand(
    std::string roadSplineId, std::string segmentId, double splitStation, std::uint64_t expectedRevision)
    : roadSplineId_(std::move(roadSplineId)), segmentId_(std::move(segmentId)),
      splitStation_(splitStation), expectedRevision_(expectedRevision) {}
const char* SplitRoadSegmentCommand::name() const noexcept { return "split-road-segment"; }
std::optional<std::uint64_t> SplitRoadSegmentCommand::expectedRevision() const noexcept { return expectedRevision_; }
std::string SplitRoadSegmentCommand::coalesceKey() const { return {}; }
std::vector<Invalidation> SplitRoadSegmentCommand::invalidations(
    const Revision& current, const Revision& candidate) const {
    const auto& road = findRoadSpline(current.project, roadSplineId_);
    const auto segments = roadSegmentsFor(current.project, road);
    const auto segment = std::find_if(segments.begin(), segments.end(), [&](const auto& item) {
        return item.id == segmentId_;
    });
    if (segment == segments.end()) throw std::invalid_argument("RoadSegment does not exist on RoadSpline.");
    return {roadSegmentationInvalidation(roadSplineId_, candidate.number,
        anchorStation(road, segment->startAnchorId), anchorStation(road, segment->endAnchorId))};
}
Revision SplitRoadSegmentCommand::apply(const Revision& current) const {
    auto road = findRoadSpline(current.project, roadSplineId_);
    const auto curve = curveForRoad(road);
    auto segments = roadSegmentsFor(current.project, road);
    const auto segmentPosition = std::find_if(segments.begin(), segments.end(), [&](const domain::RoadSegment& segment) {
        return segment.id == segmentId_;
    });
    if (segmentPosition == segments.end()) throw std::invalid_argument("RoadSegment does not exist on RoadSpline.");
    const auto index = static_cast<std::size_t>(std::distance(segments.begin(), segmentPosition));
    const auto startStation = anchorStation(road, segmentPosition->startAnchorId);
    const auto endStation = anchorStation(road, segmentPosition->endAnchorId);
    const auto epsilon = 1.0e-4;
    if (!std::isfinite(splitStation_) || splitStation_ <= startStation + epsilon ||
        splitStation_ >= endStation - epsilon || splitStation_ >= curve.totalLength() - epsilon) {
        throw std::invalid_argument("RoadSegment split station is within endpoint tolerance or out of range.");
    }

    const auto suffix = "-r" + std::to_string(current.number + 1);
    const auto splitAnchorId = segmentId_ + "/split-anchor" + suffix;
    const auto upstreamId = segmentId_ + "/upstream" + suffix;
    const auto downstreamId = segmentId_ + "/downstream" + suffix;
    if (std::any_of(road.stationAnchors.begin(), road.stationAnchors.end(), [&](const domain::StationAnchor& anchor) {
        return anchor.id == splitAnchorId;
    })) {
        throw std::invalid_argument("Generated split anchor ID already exists.");
    }
    road.stationAnchors.push_back(makeAnchor(curve, splitAnchorId, splitStation_));
    auto splitCrossSection = splitCrossSectionState(
        segmentPosition->crossSectionState, downstreamId, current.project);
    auto [upstreamAttachments, downstreamAttachments] = splitStationAttachments(
        segmentPosition->stationAttachments, road, startStation, endStation, splitStation_,
        index + 1 == segments.size());
    remapCrossSectionElementReferences(downstreamAttachments, splitCrossSection.downstreamElementIds);
    validateAttachmentElementReferences(upstreamAttachments, splitCrossSection.upstream);
    validateAttachmentElementReferences(downstreamAttachments, splitCrossSection.downstream);

    auto upstreamLineage = segmentPosition->lineage.is_object()
        ? segmentPosition->lineage : nlohmann::json::object();
    auto downstreamLineage = upstreamLineage;
    const auto predecessorId = index == 0
        ? nlohmann::json(nullptr) : nlohmann::json(segments[index - 1].id);
    const auto successorId = index + 1 == segments.size()
        ? nlohmann::json(nullptr) : nlohmann::json(segments[index + 1].id);
    upstreamLineage["parentSegmentId"] = segmentId_;
    upstreamLineage["relation"] = "split-upstream";
    upstreamLineage["predecessorSegmentId"] = predecessorId;
    upstreamLineage["successorSegmentId"] = downstreamId;
    downstreamLineage["parentSegmentId"] = segmentId_;
    downstreamLineage["relation"] = "split-downstream";
    downstreamLineage["predecessorSegmentId"] = upstreamId;
    downstreamLineage["successorSegmentId"] = successorId;
    const auto upstream = domain::RoadSegment{
        upstreamId, segmentPosition->mapId, road.id, segmentPosition->startAnchorId,
        splitAnchorId, std::move(splitCrossSection.upstream), std::move(upstreamLineage),
        segmentPosition->styleOverrides, segmentPosition->schemaProperties,
        segmentPosition->spatialReferences, segmentPosition->boundaryAttachments,
        segmentPosition->metadata, std::move(upstreamAttachments)};
    const auto downstream = domain::RoadSegment{
        downstreamId, segmentPosition->mapId, road.id, splitAnchorId,
        segmentPosition->endAnchorId, std::move(splitCrossSection.downstream), std::move(downstreamLineage),
        segmentPosition->styleOverrides, segmentPosition->schemaProperties,
        segmentPosition->spatialReferences, segmentPosition->boundaryAttachments,
        segmentPosition->metadata, std::move(downstreamAttachments)};
    segments.erase(segments.begin() + static_cast<std::ptrdiff_t>(index));
    segments.insert(segments.begin() + static_cast<std::ptrdiff_t>(index), {upstream, downstream});
    road.segmentIds.clear();
    for (const auto& segment : segments) road.segmentIds.push_back(segment.id);
    validateRoadCoverage(road, curve, segments);
    return roadRevision(current, current.project.withReplacedRoadTopology(std::move(road), std::move(segments)));
}

MoveRoadSegmentBoundaryCommand::MoveRoadSegmentBoundaryCommand(
    std::string roadSplineId, std::string boundaryAnchorId, double newStation, std::uint64_t expectedRevision)
    : roadSplineId_(std::move(roadSplineId)), boundaryAnchorId_(std::move(boundaryAnchorId)),
      newStation_(newStation), expectedRevision_(expectedRevision) {}
const char* MoveRoadSegmentBoundaryCommand::name() const noexcept { return "move-road-segment-boundary"; }
std::optional<std::uint64_t> MoveRoadSegmentBoundaryCommand::expectedRevision() const noexcept { return expectedRevision_; }
std::string MoveRoadSegmentBoundaryCommand::coalesceKey() const {
    return "move-road-segment-boundary:" + roadSplineId_ + ":" + boundaryAnchorId_;
}
std::vector<Invalidation> MoveRoadSegmentBoundaryCommand::invalidations(
    const Revision& current, const Revision& candidate) const {
    const auto& road = findRoadSpline(current.project, roadSplineId_);
    const auto segments = roadSegmentsFor(current.project, road);
    for (std::size_t index = 0; index + 1 < segments.size(); ++index) {
        if (segments[index].endAnchorId == boundaryAnchorId_ &&
            segments[index + 1].startAnchorId == boundaryAnchorId_) {
            return {roadSegmentationInvalidation(roadSplineId_, candidate.number,
                anchorStation(road, segments[index].startAnchorId),
                anchorStation(road, segments[index + 1].endAnchorId))};
        }
    }
    throw std::invalid_argument("Boundary anchor must be shared by adjacent RoadSegments.");
}
Revision MoveRoadSegmentBoundaryCommand::apply(const Revision& current) const {
    auto road = findRoadSpline(current.project, roadSplineId_);
    const auto curve = curveForRoad(road);
    auto segments = roadSegmentsFor(current.project, road);
    const auto anchor = std::find_if(road.stationAnchors.begin(), road.stationAnchors.end(), [&](const auto& item) {
        return item.id == boundaryAnchorId_;
    });
    if (anchor == road.stationAnchors.end()) throw std::invalid_argument("RoadSegment boundary anchor does not exist.");
    const auto epsilon = 1.0e-4;
    if (!std::isfinite(newStation_) || newStation_ <= epsilon || newStation_ >= curve.totalLength() - epsilon) {
        throw std::invalid_argument("RoadSegment boundary must remain inside the RoadSpline domain.");
    }
    std::vector<std::size_t> adjacent;
    for (std::size_t index = 0; index < segments.size(); ++index) {
        if (segments[index].endAnchorId == boundaryAnchorId_ || segments[index].startAnchorId == boundaryAnchorId_) {
            adjacent.push_back(index);
        }
    }
    if (adjacent.size() != 2 || adjacent[1] != adjacent[0] + 1 ||
        segments[adjacent[0]].endAnchorId != boundaryAnchorId_ ||
        segments[adjacent[1]].startAnchorId != boundaryAnchorId_) {
        throw std::invalid_argument("Boundary anchor must be shared by adjacent RoadSegments.");
    }
    const auto leftStart = anchorStation(road, segments[adjacent[0]].startAnchorId);
    const auto rightEnd = anchorStation(road, segments[adjacent[1]].endAnchorId);
    if (newStation_ <= leftStart + epsilon || newStation_ >= rightEnd - epsilon) {
        throw std::invalid_argument("Boundary move would create a zero-length or reversed segment.");
    }
    *anchor = makeAnchor(curve, boundaryAnchorId_, newStation_);
    const auto elementIds = downstreamElementRemaps(
        segments[adjacent[0]].crossSectionState, segments[adjacent[1]].crossSectionState);
    auto [upstreamAttachments, downstreamAttachments] = repartitionStationAttachments(
        segments[adjacent[0]].stationAttachments, segments[adjacent[1]].stationAttachments,
        road, leftStart, newStation_, rightEnd, elementIds);
    segments[adjacent[0]].stationAttachments = std::move(upstreamAttachments);
    segments[adjacent[1]].stationAttachments = std::move(downstreamAttachments);
    validateAttachmentElementReferences(
        segments[adjacent[0]].stationAttachments, segments[adjacent[0]].crossSectionState);
    validateAttachmentElementReferences(
        segments[adjacent[1]].stationAttachments, segments[adjacent[1]].crossSectionState);
    validateRoadCoverage(road, curve, segments);
    return roadRevision(current, current.project.withReplacedRoadTopology(std::move(road), std::move(segments)));
}

MergeRoadSegmentsCommand::MergeRoadSegmentsCommand(
    std::string roadSplineId, std::string upstreamSegmentId, std::string downstreamSegmentId,
        std::uint64_t expectedRevision, SegmentMergeResolutions resolutions)
    : roadSplineId_(std::move(roadSplineId)), upstreamSegmentId_(std::move(upstreamSegmentId)),
            downstreamSegmentId_(std::move(downstreamSegmentId)), expectedRevision_(expectedRevision),
            resolutions_(std::move(resolutions)) {}
const char* MergeRoadSegmentsCommand::name() const noexcept { return "merge-road-segments"; }
std::optional<std::uint64_t> MergeRoadSegmentsCommand::expectedRevision() const noexcept { return expectedRevision_; }
std::string MergeRoadSegmentsCommand::coalesceKey() const { return {}; }
std::vector<Invalidation> MergeRoadSegmentsCommand::invalidations(
    const Revision& current, const Revision& candidate) const {
    const auto& road = findRoadSpline(current.project, roadSplineId_);
    const auto segments = roadSegmentsFor(current.project, road);
    const auto upstream = std::find_if(segments.begin(), segments.end(), [&](const auto& item) {
        return item.id == upstreamSegmentId_;
    });
    if (upstream == segments.end() || std::next(upstream) == segments.end() ||
        std::next(upstream)->id != downstreamSegmentId_) {
        throw std::invalid_argument("RoadSegments must be adjacent and ordered for merge.");
    }
    return {roadSegmentationInvalidation(roadSplineId_, candidate.number,
        anchorStation(road, upstream->startAnchorId),
        anchorStation(road, std::next(upstream)->endAnchorId))};
}
Revision MergeRoadSegmentsCommand::apply(const Revision& current) const {
    auto road = findRoadSpline(current.project, roadSplineId_);
    const auto curve = curveForRoad(road);
    auto segments = roadSegmentsFor(current.project, road);
    const auto upstreamPosition = std::find_if(segments.begin(), segments.end(), [&](const auto& segment) {
        return segment.id == upstreamSegmentId_;
    });
    if (upstreamPosition == segments.end() || std::next(upstreamPosition) == segments.end() ||
        std::next(upstreamPosition)->id != downstreamSegmentId_) {
        throw std::invalid_argument("RoadSegments must be adjacent and ordered for merge.");
    }
    const auto downstreamPosition = std::next(upstreamPosition);
    const auto conflicts = conflictingFields(current);
    for (const auto& [field, choice] : resolutions_) {
        static_cast<void>(choice);
        if (std::find(conflicts.begin(), conflicts.end(), field) == conflicts.end()) {
            throw std::invalid_argument("Merge resolution was supplied for a non-conflicting field.");
        }
    }
    for (const auto& field : conflicts) {
        if (!resolutions_.contains(field)) {
            throw std::invalid_argument("Non-equivalent RoadSegments require a resolution for field: " + field);
        }
    }
    if (upstreamPosition->endAnchorId != downstreamPosition->startAnchorId ||
        std::abs(anchorStation(road, upstreamPosition->endAnchorId) -
            anchorStation(road, downstreamPosition->startAnchorId)) > 1.0e-4) {
        throw std::invalid_argument("RoadSegments do not share one compatible boundary anchor.");
    }
    auto merged = *upstreamPosition;
    const auto selectedValue = [&](const std::string& field, const nlohmann::json& upstreamValue,
        const nlohmann::json& downstreamValue) {
        if (field == "crossSectionState" &&
            crossSectionStatesEquivalent(upstreamValue, downstreamValue)) {
            return upstreamValue;
        }
        const auto choice = resolutions_.find(field);
        if (choice == resolutions_.end() || choice->second == SegmentMergeSource::upstream) {
            return upstreamValue;
        }
        return downstreamValue;
    };
    merged.crossSectionState = selectedValue("crossSectionState",
        upstreamPosition->crossSectionState, downstreamPosition->crossSectionState);
    merged.styleOverrides = selectedValue("styleOverrides",
        upstreamPosition->styleOverrides, downstreamPosition->styleOverrides);
    merged.schemaProperties = selectedValue("schemaProperties",
        upstreamPosition->schemaProperties, downstreamPosition->schemaProperties);
    merged.spatialReferences = selectedValue("spatialReferences",
        upstreamPosition->spatialReferences, downstreamPosition->spatialReferences);
    merged.boundaryAttachments = selectedValue("boundaryAttachments",
        upstreamPosition->boundaryAttachments, downstreamPosition->boundaryAttachments);
    merged.metadata = selectedValue("metadata", upstreamPosition->metadata, downstreamPosition->metadata);
    auto downstreamElementIds = downstreamElementRemaps(
        upstreamPosition->crossSectionState, downstreamPosition->crossSectionState);
    std::map<std::string, std::string> upstreamElementIds;
    if (!downstreamElementIds.empty() && merged.crossSectionState == downstreamPosition->crossSectionState) {
        for (const auto& [downstreamId, upstreamId] : downstreamElementIds) {
            upstreamElementIds[upstreamId] = downstreamId;
        }
        downstreamElementIds.clear();
    }
    merged.stationAttachments = combineStationAttachments(
        upstreamPosition->stationAttachments, downstreamPosition->stationAttachments,
        upstreamElementIds, downstreamElementIds, merged.crossSectionState);
    merged.endAnchorId = downstreamPosition->endAnchorId;
    merged.lineage["relation"] = "merge";
    merged.lineage["predecessorSegmentId"] = upstreamPosition->lineage.value(
        "predecessorSegmentId", nlohmann::json(nullptr));
    merged.lineage["successorSegmentId"] = downstreamPosition->lineage.value(
        "successorSegmentId", nlohmann::json(nullptr));
    merged.lineage["mergedFrom"] = {
        {"upstream", upstreamPosition->normalizedJson()},
        {"downstream", downstreamPosition->normalizedJson()}};
    const auto index = static_cast<std::size_t>(std::distance(segments.begin(), upstreamPosition));
    segments[index] = std::move(merged);
    segments.erase(segments.begin() + static_cast<std::ptrdiff_t>(index + 1));
    road.segmentIds.clear();
    for (const auto& segment : segments) road.segmentIds.push_back(segment.id);
    validateRoadCoverage(road, curve, segments);
    return roadRevision(current, current.project.withReplacedRoadTopology(std::move(road), std::move(segments)));
}

std::vector<std::string> MergeRoadSegmentsCommand::conflictingFields(const Revision& current) const {
    const auto& road = findRoadSpline(current.project, roadSplineId_);
    const auto segments = roadSegmentsFor(current.project, road);
    const auto upstream = std::find_if(segments.begin(), segments.end(), [&](const auto& item) {
        return item.id == upstreamSegmentId_;
    });
    if (upstream == segments.end() || std::next(upstream) == segments.end() ||
        std::next(upstream)->id != downstreamSegmentId_) {
        throw std::invalid_argument("RoadSegments must be adjacent and ordered for merge.");
    }
    const auto& downstream = *std::next(upstream);
    std::vector<std::string> conflicts;
    if (!crossSectionStatesEquivalent(upstream->crossSectionState, downstream.crossSectionState)) {
        conflicts.push_back("crossSectionState");
    }
    if (upstream->styleOverrides != downstream.styleOverrides) conflicts.push_back("styleOverrides");
    if (upstream->schemaProperties != downstream.schemaProperties) conflicts.push_back("schemaProperties");
    if (upstream->spatialReferences != downstream.spatialReferences) conflicts.push_back("spatialReferences");
    if (upstream->boundaryAttachments != downstream.boundaryAttachments) conflicts.push_back("boundaryAttachments");
    if (upstream->metadata != downstream.metadata) conflicts.push_back("metadata");
    return conflicts;
}

// Apply a project replacement to a copied revision.
Revision ReplaceProjectCommand::apply(const Revision& current) const {
    return Revision{current.number + 1, project_, current.selection};
}

// Initialize the processor with the first authoritative revision snapshot.
CommandProcessor::CommandProcessor(Revision initial)
    : current_(std::move(initial)) {}

// Return the current authoritative revision.
const Revision& CommandProcessor::current() const noexcept {
    return current_;
}

// Register a source-to-derived dependency edge.
void DependencyGraph::addDependency(
    std::string sourceId,
    std::string derivedId,
    std::optional<StationRange> affectedRange) {
    // Keep dependency ownership explicit; scheduling policy is intentionally deferred.
    edges_.push_back(Edge{std::move(sourceId), std::move(derivedId), affectedRange});
}

// Find unique derived products that depend on a source record.
std::vector<std::string> DependencyGraph::dependentsOf(const std::string& sourceId) const {
    // Deduplicate results so one source invalidates each derived product once.
    std::vector<std::string> result;
    for (const auto& edge : edges_) {
        if (edge.sourceId == sourceId &&
            std::find(result.begin(), result.end(), edge.derivedId) == result.end()) {
            result.push_back(edge.derivedId);
        }
    }
    return result;
}

std::vector<std::string> DependencyGraph::dependentsFor(const Invalidation& invalidation) const {
    std::vector<std::string> result;
    for (const auto& edge : edges_) {
        if (edge.sourceId != invalidation.sourceId ||
            (edge.affectedRange && invalidation.stationRange &&
                !edge.affectedRange->overlaps(*invalidation.stationRange))) {
            continue;
        }
        if (std::find(result.begin(), result.end(), edge.derivedId) == result.end()) {
            result.push_back(edge.derivedId);
        }
    }
    return result;
}

void ReverseReferenceIndex::addReference(
    std::string targetId,
    std::string dependentId,
    ReferenceStrength strength) {
    references_.push_back(Reference{std::move(targetId), std::move(dependentId), strength});
}

std::vector<std::string> ReverseReferenceIndex::dependentsOf(const std::string& targetId) const {
    std::vector<std::string> result;
    for (const auto& reference : references_) {
        if (reference.targetId == targetId &&
            std::find(result.begin(), result.end(), reference.dependentId) == result.end()) {
            result.push_back(reference.dependentId);
        }
    }
    return result;
}

std::vector<std::string> ReverseReferenceIndex::requiredDependentsOf(
    const std::string& targetId) const {
    std::vector<std::string> result;
    for (const auto& reference : references_) {
        if (reference.targetId == targetId &&
            reference.strength == ReferenceStrength::required &&
            std::find(result.begin(), result.end(), reference.dependentId) == result.end()) {
            result.push_back(reference.dependentId);
        }
    }
    return result;
}

// Resolve a destructive impact explicitly; cancellation never authorizes mutation.
DestructiveImpact resolveDestructiveImpact(
    std::vector<std::string> affectedIds,
    ImpactResolution resolution) {
    return DestructiveImpact{std::move(affectedIds), resolution != ImpactResolution::cancel};
}

DestructiveImpact resolveDestructiveImpact(
    const ReverseReferenceIndex& references,
    const std::string& targetId,
    ImpactResolution resolution) {
    return resolveDestructiveImpact(references.dependentsOf(targetId), resolution);
}

// Validate a command and calculate its candidate revision without mutating state.
Preview CommandProcessor::preview(const Command& command) const {
    // Revision validation happens before apply so stale commands cannot produce candidates.
    // Reject commands created against an older source revision.
    if (const auto expected = command.expectedRevision(); expected && *expected != current_.number) {
        throw std::runtime_error("Command revision is stale.");
    }

    Preview result;
    result.diagnostics = command.validate(current_);
    result.candidate = command.apply(current_);
    result.impactIds.push_back(result.candidate.project.id());
    // A committed candidate always invalidates the changed project source.
    result.invalidations = command.invalidations(current_, result.candidate);
    deriveChangedRoadEnvelopes(current_, result, roadEnvelopeCache_);
    return result;
}

// Install a validated candidate and preserve the previous revision for undo.
void CommandProcessor::commit(const Command& command) {
    // Store the prior source snapshot before installing the candidate for undo.
    const auto candidate = preview(command);
    diagnostics_.insert(diagnostics_.end(), candidate.diagnostics.begin(), candidate.diagnostics.end());
    const auto hasError = std::any_of(candidate.diagnostics.begin(), candidate.diagnostics.end(),
        [](const Diagnostic& diagnostic) {
            return diagnostic.severity == Severity::error && diagnostic.blocksCommit;
        });
    if (hasError) throw std::runtime_error("Command preconditions are invalid.");
    const auto key = command.coalesceKey();
    // Start a new undo entry only when the edit is not a continuation of the prior one.
    if (key.empty() || key != lastCoalesceKey_) {
        undoStack_.push_back(HistoryEntry{current_, candidate.invalidations});
    } else if (!undoStack_.empty()) {
        auto& accumulated = undoStack_.back().invalidations;
        for (auto& invalidation : accumulated) invalidation.revision = candidate.candidate.number;
        accumulated.insert(accumulated.end(), candidate.invalidations.begin(), candidate.invalidations.end());
        accumulated = coalesceInvalidations(std::move(accumulated));
    }
    current_ = candidate.candidate;
    lastInvalidations_ = candidate.invalidations;
    redoStack_.clear();
    lastCoalesceKey_ = key;
}

// Discard a preview without touching processor state.
void CommandProcessor::cancel(const Preview& preview) const noexcept {
    // A preview owns no processor state, so cancellation is deliberately a no-op.
    static_cast<void>(preview);
}

// Move the current revision to redo history and restore the previous revision.
bool CommandProcessor::undo() {
    // Moving snapshots between stacks preserves authoritative state exactly.
    // There is nothing to restore when the undo stack is empty.
    if (undoStack_.empty()) return false;
    auto entry = std::move(undoStack_.back());
    undoStack_.pop_back();
    redoStack_.push_back(HistoryEntry{current_, entry.invalidations});
    current_ = std::move(entry.revision);
    lastInvalidations_ = std::move(entry.invalidations);
    for (auto& invalidation : lastInvalidations_) invalidation.revision = current_.number;
    lastInvalidations_ = coalesceInvalidations(std::move(lastInvalidations_));
    lastCoalesceKey_.clear();
    return true;
}

// Move the current revision to undo history and restore the next revision.
bool CommandProcessor::redo() {
    // There is nothing to restore when the redo stack is empty.
    if (redoStack_.empty()) return false;
    auto entry = std::move(redoStack_.back());
    redoStack_.pop_back();
    undoStack_.push_back(HistoryEntry{current_, entry.invalidations});
    current_ = std::move(entry.revision);
    lastInvalidations_ = std::move(entry.invalidations);
    for (auto& invalidation : lastInvalidations_) invalidation.revision = current_.number;
    lastInvalidations_ = coalesceInvalidations(std::move(lastInvalidations_));
    lastCoalesceKey_.clear();
    return true;
}

// Report whether an undo operation is currently available.
bool CommandProcessor::canUndo() const noexcept {
    // UI callers use this query to enable or disable undo actions.
    return !undoStack_.empty();
}

// Report whether a redo operation is currently available.
bool CommandProcessor::canRedo() const noexcept {
    // UI callers use this query to enable or disable redo actions.
    return !redoStack_.empty();
}

const std::vector<Invalidation>& CommandProcessor::lastInvalidations() const noexcept {
    return lastInvalidations_;
}

// Store a revision-tagged diagnostic for the application layer.
void CommandProcessor::addDiagnostic(Diagnostic diagnostic) {
    // Diagnostics are revision-tagged records owned by the application layer.
    diagnostics_.push_back(std::move(diagnostic));
}

void CommandProcessor::recordRebuildFailure(
    std::string objectId,
    std::string ruleId,
    std::string message,
    std::vector<std::string> repairIds) {
    diagnostics_.push_back(Diagnostic{
        std::move(ruleId), Severity::error, {std::move(objectId)}, std::move(message),
        current_.number, std::move(repairIds)});
}

// Return diagnostics without transferring ownership to the caller.
const std::vector<Diagnostic>& CommandProcessor::diagnostics() const noexcept {
    return diagnostics_;
}

// Accept derived work only when it was computed from the current revision.
bool CommandProcessor::acceptResult(const VersionedResult& result) {
    // Results from older revisions are discarded rather than replacing current caches.
    // Stale results are discarded without changing current cache state.
    if (result.sourceRevision != current_.number) {
        return false;
    }
    const auto expected = cacheDependencies_.find(result.cacheId);
    if (expected != cacheDependencies_.end()) {
        auto actual = result.sourceDependencies;
        std::sort(actual.begin(), actual.end());
        if (actual != expected->second) return false;
    }
    caches_[result.cacheId] = result;
    return true;
}

void CommandProcessor::registerCacheDependencies(
    std::string cacheId,
    std::vector<std::string> sourceDependencies) {
    std::sort(sourceDependencies.begin(), sourceDependencies.end());
    sourceDependencies.erase(
        std::unique(sourceDependencies.begin(), sourceDependencies.end()), sourceDependencies.end());
    cacheDependencies_[std::move(cacheId)] = std::move(sourceDependencies);
}

// Return the payload of an accepted derived result, if present.
const std::string* CommandProcessor::cachedResult(const std::string& cacheId) const noexcept {
    const auto found = caches_.find(cacheId);
    return found == caches_.end() ? nullptr : &found->second.payload;
}

// Expose the dependency graph used by the correctness-first scheduler boundary.
const DependencyGraph& CommandProcessor::dependencies() const noexcept {
    return dependencies_;
}

} // namespace atlas::application
