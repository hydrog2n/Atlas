#include "atlas/domain/project.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <initializer_list>
#include <set>
#include <stdexcept>
#include <utility>

namespace atlas::domain {

namespace {

constexpr const char* geometryPolicyExtensionKey = "atlas.geometryPolicy";
constexpr double defaultMiterLimitRatio = 4.0;

nlohmann::json unknownRecordFields(
    const nlohmann::json& record,
    std::initializer_list<const char*> knownFields) {
    nlohmann::json unknown = nlohmann::json::object();
    for (const auto& [key, value] : record.items()) {
        const auto known = std::any_of(knownFields.begin(), knownFields.end(), [&](const char* field) {
            return key == field;
        });
        if (!known) unknown[key] = value;
    }
    return unknown;
}

nlohmann::json defaultGeometryPolicy() {
    return {{"miterLimitRatio", defaultMiterLimitRatio}};
}

void validateGeometryPolicy(const nlohmann::json& policy) {
    if (!policy.is_object() || !policy.contains("miterLimitRatio") ||
        !policy["miterLimitRatio"].is_number()) {
        throw std::invalid_argument("Project geometry policy requires miterLimitRatio.");
    }
    const auto ratio = policy["miterLimitRatio"].get<double>();
    if (!std::isfinite(ratio) || ratio < 1.0) {
        throw std::invalid_argument("Project miterLimitRatio must be finite and at least 1.0.");
    }
}

// Canonical root-map shape used for deterministic serialization.
nlohmann::ordered_json normalizeRootMap(const Map& rootMap) {
    nlohmann::ordered_json normalized = {
        {"id", rootMap.id},
        {"type", "core.Map"},
        {"parentMapId", nullptr},
        {"objects", nlohmann::ordered_json::array()},
        {"networkObjects", nlohmann::ordered_json::array()},
        {"defaultDisplayLayerId", rootMap.defaultDisplayLayerId},
        {"defaultSpatialLevelId", rootMap.defaultSpatialLevelId},
        {"displayLayers", nlohmann::ordered_json::array()},
        {"spatialLevels", nlohmann::ordered_json::array()}
    };

    for (const auto& layer : rootMap.displayLayers) {
        normalized["displayLayers"].push_back({
            {"id", layer.id}, {"name", layer.name}, {"visible", layer.visible},
            {"locked", layer.locked}, {"opacity", layer.opacity}});
    }
    for (const auto& level : rootMap.spatialLevels) {
        normalized["spatialLevels"].push_back({{"id", level.id}, {"name", level.name}});
    }
    auto objects = rootMap.objects;
    std::sort(objects.begin(), objects.end(), [](const MapObject& left, const MapObject& right) {
        return left.id() < right.id();
    });
    for (const auto& object : objects) {
        normalized["objects"].push_back(object.normalizedJson());
    }

    for (const auto& [key, value] : rootMap.unknownFields.items()) {
        normalized[key] = value;
    }
    return normalized;
}

} // namespace

nlohmann::json RoadSpline::normalizedJson() const {
    nlohmann::json normalized{
        {"id", id}, {"mapId", mapId}, {"primitives", primitives},
        {"direction", direction}, {"segmentIds", segmentIds},
        {"styleRef", styleRef}, {"metadata", metadata}, {"networkRefs", networkRefs},
        {"stationAnchors", nlohmann::json::array()}};
    for (const auto& anchor : stationAnchors) {
        normalized["stationAnchors"].push_back({
            {"id", anchor.id}, {"resolvedStation", anchor.resolvedStation},
            {"affinity", static_cast<int>(anchor.affinity)},
            {"remapSignature", {
                {"normalizedStation", anchor.remapSignature.normalizedStation},
                {"primitiveId", anchor.remapSignature.primitiveId},
                {"primitiveT", anchor.remapSignature.primitiveT},
                {"worldPosition", {{"x", anchor.remapSignature.worldPosition.x},
                    {"y", anchor.remapSignature.worldPosition.y}}}}}});
    }
    for (const auto& [key, value] : unknownFields.items()) normalized[key] = value;
    return normalized;
}

RoadSpline RoadSpline::fromJson(const nlohmann::json& record) {
    if (!record.is_object() || !record.contains("id") || !record["id"].is_string() ||
        record["id"].get<std::string>().empty() || !record.contains("mapId") ||
        !record["mapId"].is_string() || record["mapId"].get<std::string>().empty()) {
        throw std::invalid_argument("RoadSpline requires non-empty id and mapId.");
    }
    RoadSpline road{
        record["id"], record["mapId"], record.value("primitives", nlohmann::json::array()),
        record.value("direction", "start-to-end"), record.value("segmentIds", std::vector<std::string>{}),
        record.value("styleRef", nlohmann::json(nullptr)),
        record.value("metadata", nlohmann::json::object()),
        record.value("networkRefs", nlohmann::json::array()), {}};
    road.unknownFields = unknownRecordFields(record, {
        "id", "mapId", "primitives", "direction", "segmentIds", "styleRef", "metadata",
        "networkRefs", "stationAnchors"});
    for (const auto& anchorJson : record.value("stationAnchors", nlohmann::json::array())) {
        StationAnchor anchor;
        anchor.id = anchorJson.value("id", "");
        anchor.resolvedStation = anchorJson.value("resolvedStation", 0.0);
        anchor.affinity = static_cast<AnchorAffinity>(anchorJson.value("affinity", 2));
        const auto signature = anchorJson.value("remapSignature", nlohmann::json::object());
        anchor.remapSignature.normalizedStation = signature.value("normalizedStation", 0.0);
        anchor.remapSignature.primitiveId = signature.value("primitiveId", "");
        anchor.remapSignature.primitiveT = signature.value("primitiveT", 0.0);
        const auto position = signature.value("worldPosition", nlohmann::json::object());
        anchor.remapSignature.worldPosition = {position.value("x", 0.0), position.value("y", 0.0)};
        road.stationAnchors.push_back(std::move(anchor));
    }
    return road;
}

nlohmann::json RoadSegment::normalizedJson() const {
    nlohmann::json normalized = {
        {"id", id}, {"mapId", mapId}, {"roadSplineId", roadSplineId},
        {"startAnchorId", startAnchorId}, {"endAnchorId", endAnchorId},
        {"crossSectionState", crossSectionState}, {"styleOverrides", styleOverrides},
        {"schemaProperties", schemaProperties}, {"spatialReferences", spatialReferences},
        {"boundaryAttachments", boundaryAttachments}, {"metadata", metadata}, {"lineage", lineage},
        {"stationAttachments", stationAttachments}};
    for (const auto& [key, value] : unknownFields.items()) normalized[key] = value;
    return normalized;
}

RoadSegment RoadSegment::fromJson(const nlohmann::json& record) {
    if (!record.is_object() || !record.contains("id") || !record["id"].is_string() ||
        record["id"].get<std::string>().empty() || !record.contains("mapId") ||
        !record["mapId"].is_string() || record["mapId"].get<std::string>().empty() ||
        !record.contains("roadSplineId") || !record["roadSplineId"].is_string() ||
        record["roadSplineId"].get<std::string>().empty()) {
        throw std::invalid_argument("RoadSegment requires id, mapId, and roadSplineId.");
    }
    RoadSegment segment{
        record["id"], record["mapId"], record["roadSplineId"],
        record.value("startAnchorId", ""), record.value("endAnchorId", ""),
        record.value("crossSectionState", nlohmann::json::object()),
        record.value("lineage", nlohmann::json::object()),
        record.value("styleOverrides", nlohmann::json::object()),
        record.value("schemaProperties", nlohmann::json::object()),
        record.value("spatialReferences", nlohmann::json::array()),
        record.value("boundaryAttachments", nlohmann::json::array()),
        record.value("metadata", nlohmann::json::object()),
        record.value("stationAttachments", nlohmann::json::array())};
    segment.unknownFields = unknownRecordFields(record, {
        "id", "mapId", "roadSplineId", "startAnchorId", "endAnchorId", "crossSectionState",
        "lineage", "styleOverrides", "schemaProperties", "spatialReferences", "boundaryAttachments",
        "metadata", "stationAttachments"});
    return segment;
}

MapObject::MapObject(nlohmann::json record)
    : record_(std::move(record)),
      id_(record_.value("id", "")),
      type_(record_.value("type", "")),
      name_(record_.value("name", "")),
      primaryDisplayLayerId_(record_.value("primaryDisplayLayerId", "default-layer")),
      spatialLevelId_(record_.contains("spatialLevelId") && !record_["spatialLevelId"].is_null()
          ? std::optional<std::string>(record_["spatialLevelId"].get<std::string>())
          : std::nullopt),
      tags_(record_.value("tags", std::vector<std::string>{})) {}

MapObject MapObject::create(
    std::string id,
    std::string type,
    nlohmann::json geometry) {
    return MapObject(nlohmann::json{
        {"id", std::move(id)},
        {"type", std::move(type)},
        {"geometry", std::move(geometry)},
        {"name", ""},
        {"primaryDisplayLayerId", "default-layer"},
        {"spatialLevelId", nullptr},
        {"tags", nlohmann::json::array()},
        {"visible", true},
        {"locked", false}});
}

MapObject MapObject::fromJson(const nlohmann::json& record) {
    if (!record.is_object() || !record.contains("id") || !record["id"].is_string() ||
        record["id"].get<std::string>().empty() || !record.contains("type") ||
        !record["type"].is_string() || record["type"].get<std::string>().empty()) {
        throw std::invalid_argument("MapObject requires non-empty id and type.");
    }
    if (record["type"] == "core.ReferenceImage" && record.contains("geometry") &&
        record["geometry"].is_object() && record["geometry"].contains("path")) {
        const auto path = record["geometry"]["path"].get<std::string>();
        if (path.empty() || path.front() == '/' || path.front() == '\\' ||
            path.find(':') != std::string::npos || path.find("..") != std::string::npos) {
            throw std::invalid_argument("Reference image path must remain project-relative.");
        }
    }
    return MapObject(record);
}

const std::string& MapObject::id() const noexcept { return id_; }
const std::string& MapObject::type() const noexcept { return type_; }
const std::string& MapObject::name() const noexcept { return name_; }
const nlohmann::json& MapObject::geometry() const noexcept { return record_.at("geometry"); }
const std::string& MapObject::primaryDisplayLayerId() const noexcept { return primaryDisplayLayerId_; }
const std::optional<std::string>& MapObject::spatialLevelId() const noexcept { return spatialLevelId_; }
const std::vector<std::string>& MapObject::tags() const noexcept { return tags_; }
bool MapObject::visible() const noexcept { return record_.value("visible", true); }
bool MapObject::locked() const noexcept { return record_.value("locked", false); }

MapObject MapObject::withGeometry(nlohmann::json geometry) const {
    auto record = record_;
    record["geometry"] = std::move(geometry);
    return MapObject(std::move(record));
}

MapObject MapObject::withName(std::string name) const {
    auto record = record_;
    record["name"] = std::move(name);
    return MapObject(std::move(record));
}

MapObject MapObject::withDisplayLayer(std::string layerId) const {
    auto record = record_;
    record["primaryDisplayLayerId"] = std::move(layerId);
    return MapObject(std::move(record));
}

MapObject MapObject::withSpatialLevel(std::optional<std::string> levelId) const {
    auto record = record_;
    record["spatialLevelId"] = levelId ? nlohmann::json(*levelId) : nlohmann::json(nullptr);
    return MapObject(std::move(record));
}

MapObject MapObject::withVisibility(bool visible) const {
    auto record = record_;
    record["visible"] = visible;
    return MapObject(std::move(record));
}

MapObject MapObject::withLocked(bool locked) const {
    auto record = record_;
    record["locked"] = locked;
    return MapObject(std::move(record));
}

nlohmann::json MapObject::normalizedJson() const {
    return record_;
}

const MapObject* Map::findObject(const std::string& objectId) const noexcept {
    const auto found = std::find_if(objects.begin(), objects.end(), [&](const MapObject& object) {
        return object.id() == objectId;
    });
    return found == objects.end() ? nullptr : &*found;
}

Map Map::withObject(MapObject object) const {
    auto result = *this;
    const auto found = std::find_if(result.objects.begin(), result.objects.end(), [&](const MapObject& existing) {
        return existing.id() == object.id();
    });
    if (found == result.objects.end()) result.objects.push_back(std::move(object));
    else *found = std::move(object);
    return result;
}

Map Map::withoutObject(const std::string& objectId) const {
    auto result = *this;
    result.objects.erase(std::remove_if(result.objects.begin(), result.objects.end(), [&](const MapObject& object) {
        return object.id() == objectId;
    }), result.objects.end());
    return result;
}

void Map::validateObjectReferences() const {
    for (const auto& object : objects) {
        const auto layer = std::find_if(displayLayers.begin(), displayLayers.end(), [&](const DisplayLayer& candidate) {
            return candidate.id == object.primaryDisplayLayerId();
        });
        if (layer == displayLayers.end()) {
            throw std::invalid_argument("MapObject references an unknown DisplayLayer.");
        }
        if (object.spatialLevelId()) {
            const auto level = std::find_if(spatialLevels.begin(), spatialLevels.end(), [&](const SpatialLevel& candidate) {
                return candidate.id == *object.spatialLevelId();
            });
            if (level == spatialLevels.end()) {
                throw std::invalid_argument("MapObject references an unknown SpatialLevel.");
            }
        }
    }
}

// Create a valid default project object.
Project Project::empty(std::string projectId, std::string mapId) {
    Map rootMap{std::move(mapId)};
    rootMap.displayLayers.push_back({"default-layer", "Default", true, false, 1.0});
    rootMap.spatialLevels.push_back({"default-level", "Ground"});
    return Project(std::move(projectId), std::move(rootMap), {}, {},
        {{geometryPolicyExtensionKey, defaultGeometryPolicy()}});
}

// Parse project JSON and validate the minimum required fields.
Project Project::fromJson(const std::string& jsonText) {
    const auto parsed = nlohmann::json::parse(jsonText);

    if (!parsed.contains("id") || !parsed["id"].is_string() || parsed["id"].get<std::string>().empty()) {
        throw std::invalid_argument("Project id is required.");
    }
    if (!parsed.contains("maps") || !parsed["maps"].is_array() || parsed["maps"].empty()) {
        throw std::invalid_argument("Project must contain at least one map.");
    }

    const auto rootMapJson = parsed["maps"][0];
    if (!rootMapJson.contains("id") || !rootMapJson["id"].is_string() || rootMapJson["id"].get<std::string>().empty()) {
        throw std::invalid_argument("Root map id is required.");
    }

    nlohmann::json rootMapUnknownFields = nlohmann::json::object();
    for (const auto& [key, value] : rootMapJson.items()) {
        if (key != "id" && key != "type" && key != "parentMapId" &&
            key != "objects" && key != "networkObjects" && key != "defaultDisplayLayerId" &&
            key != "defaultSpatialLevelId" && key != "displayLayers" && key != "spatialLevels") {
            rootMapUnknownFields[key] = value;
        }
    }

    Map rootMap{rootMapJson["id"].get<std::string>(), std::move(rootMapUnknownFields)};
    rootMap.defaultDisplayLayerId = rootMapJson.value("defaultDisplayLayerId", "default-layer");
    rootMap.defaultSpatialLevelId = rootMapJson.value("defaultSpatialLevelId", "default-level");
    for (const auto& layerJson : rootMapJson.value("displayLayers", nlohmann::json::array())) {
        rootMap.displayLayers.push_back({
            layerJson.value("id", ""), layerJson.value("name", ""),
            layerJson.value("visible", true), layerJson.value("locked", false),
            layerJson.value("opacity", 1.0)});
    }
    for (const auto& levelJson : rootMapJson.value("spatialLevels", nlohmann::json::array())) {
        rootMap.spatialLevels.push_back({levelJson.value("id", ""), levelJson.value("name", "")});
    }
    for (const auto& objectJson : rootMapJson.value("objects", nlohmann::json::array())) {
        const auto object = MapObject::fromJson(objectJson);
        if (rootMap.findObject(object.id()) != nullptr) {
            throw std::invalid_argument("Map contains duplicate MapObject IDs.");
        }
        rootMap.objects.push_back(object);
    }
    if (rootMap.displayLayers.empty()) rootMap.displayLayers.push_back({"default-layer", "Default", true, false, 1.0});
    if (rootMap.spatialLevels.empty()) rootMap.spatialLevels.push_back({"default-level", "Ground"});
    rootMap.validateObjectReferences();

    std::vector<RoadSpline> roadSplines;
    for (const auto& roadJson : parsed.value("roadSplines", nlohmann::json::array())) {
        const auto road = RoadSpline::fromJson(roadJson);
        if (std::any_of(roadSplines.begin(), roadSplines.end(), [&](const RoadSpline& existing) {
            return existing.id == road.id;
        })) {
            throw std::invalid_argument("Project contains duplicate RoadSpline IDs.");
        }
        if (road.mapId != rootMap.id) throw std::invalid_argument("RoadSpline has invalid Map ownership.");
        roadSplines.push_back(road);
    }
    std::vector<RoadSegment> roadSegments;
    for (const auto& segmentJson : parsed.value("roadSegments", nlohmann::json::array())) {
        const auto segment = RoadSegment::fromJson(segmentJson);
        if (std::any_of(roadSegments.begin(), roadSegments.end(), [&](const RoadSegment& existing) {
            return existing.id == segment.id;
        })) {
            throw std::invalid_argument("Project contains duplicate RoadSegment IDs.");
        }
        if (segment.mapId != rootMap.id || std::none_of(roadSplines.begin(), roadSplines.end(), [&](const RoadSpline& road) {
            return road.id == segment.roadSplineId;
        })) {
            throw std::invalid_argument("RoadSegment has invalid RoadSpline ownership.");
        }
        roadSegments.push_back(segment);
    }

    nlohmann::json extensions = parsed.value("extensions", nlohmann::json::object());
    if (!extensions.is_object()) {
        throw std::invalid_argument("Project extensions must be an object.");
    }
    if (!extensions.contains(geometryPolicyExtensionKey)) {
        extensions[geometryPolicyExtensionKey] = defaultGeometryPolicy();
    }
    validateGeometryPolicy(extensions[geometryPolicyExtensionKey]);

    // Keep fields outside the current schema instead of silently dropping them.
    nlohmann::json unknownFields = nlohmann::json::object();
    for (const auto& [key, value] : parsed.items()) {
        if (key != "id" && key != "schemaVersion" && key != "units" && key != "maps" &&
            key != "roadSplines" && key != "roadSegments" && key != "extensions") {
            unknownFields[key] = value;
        }
    }

    return Project(
        parsed["id"].get<std::string>(),
        std::move(rootMap),
        std::move(roadSplines),
        std::move(roadSegments),
        std::move(extensions),
        std::move(unknownFields));
}

// Store the project identity and extension data in the class.
Project::Project(
    std::string projectId,
    Map rootMap,
    std::vector<RoadSpline> roadSplines,
    std::vector<RoadSegment> roadSegments,
    nlohmann::json extensions,
    nlohmann::json unknownFields)
    : id_(std::move(projectId)),
      rootMap_(std::move(rootMap)),
            roadSplines_(std::move(roadSplines)),
            roadSegments_(std::move(roadSegments)),
      extensions_(std::move(extensions)),
      unknownFields_(std::move(unknownFields)) {}

const std::string& Project::id() const noexcept {
    // The project ID is stable identity, not a display label.
    return id_;
}

const Map& Project::rootMap() const noexcept {
    // Return the owning root map without copying authoritative data.
    return rootMap_;
}

const nlohmann::json& Project::extensions() const noexcept {
    // Extensions are recognized opaque project-level data.
    return extensions_;
}

const nlohmann::json& Project::geometryPolicy() const {
    return extensions_.at(geometryPolicyExtensionKey);
}

const nlohmann::json& Project::unknownFields() const noexcept {
    // Unknown fields remain available for lossless round trips.
    return unknownFields_;
}

const std::vector<RoadSpline>& Project::roadSplines() const noexcept {
    return roadSplines_;
}

const std::vector<RoadSegment>& Project::roadSegments() const noexcept {
    return roadSegments_;
}

Project Project::withRootMap(Map rootMap) const {
    rootMap.validateObjectReferences();
    return Project(id_, std::move(rootMap), roadSplines_, roadSegments_, extensions_, unknownFields_);
}

Project Project::withGeometryPolicy(nlohmann::json policy) const {
    validateGeometryPolicy(policy);
    auto extensions = extensions_;
    extensions[geometryPolicyExtensionKey] = std::move(policy);
    return Project(id_, rootMap_, roadSplines_, roadSegments_, std::move(extensions), unknownFields_);
}

Project Project::withRoadSpline(RoadSpline roadSpline) const {
    if (roadSpline.id.empty() || roadSpline.mapId != rootMap_.id) {
        throw std::invalid_argument("RoadSpline requires a non-empty ID owned by the root Map.");
    }
    auto roadSplines = roadSplines_;
    if (std::any_of(roadSplines.begin(), roadSplines.end(), [&](const RoadSpline& existing) {
        return existing.id == roadSpline.id;
    })) {
        throw std::invalid_argument("RoadSpline ID must be unique.");
    }
    roadSplines.push_back(std::move(roadSpline));
    return Project(id_, rootMap_, std::move(roadSplines), roadSegments_, extensions_, unknownFields_);
}

Project Project::withRoadSplineAndSegments(RoadSpline roadSpline, std::vector<RoadSegment> segments) const {
    if (roadSpline.id.empty() || roadSpline.mapId != rootMap_.id) {
        throw std::invalid_argument("RoadSpline requires a non-empty ID owned by the root Map.");
    }
    if (std::any_of(roadSplines_.begin(), roadSplines_.end(), [&](const RoadSpline& existing) {
        return existing.id == roadSpline.id;
    })) {
        throw std::invalid_argument("RoadSpline ID must be unique.");
    }
    roadSpline.segmentIds.clear();
    auto allSegments = roadSegments_;
    for (const auto& segment : segments) {
        if (segment.id.empty() || segment.mapId != rootMap_.id || segment.roadSplineId != roadSpline.id ||
            std::any_of(allSegments.begin(), allSegments.end(), [&](const RoadSegment& existing) {
                return existing.id == segment.id;
            })) {
            throw std::invalid_argument("Initial RoadSegment has invalid ownership or duplicate ID.");
        }
        roadSpline.segmentIds.push_back(segment.id);
        allSegments.push_back(segment);
    }
    if (segments.size() != 1) {
        throw std::invalid_argument("New RoadSpline requires exactly one initial full-domain RoadSegment.");
    }
    auto roads = roadSplines_;
    roads.push_back(std::move(roadSpline));
    return Project(id_, rootMap_, std::move(roads), std::move(allSegments), extensions_, unknownFields_);
}

Project Project::withReplacedRoadSpline(RoadSpline roadSpline) const {
    if (roadSpline.id.empty() || roadSpline.mapId != rootMap_.id) {
        throw std::invalid_argument("RoadSpline requires a non-empty ID owned by the root Map.");
    }
    auto roadSplines = roadSplines_;
    const auto found = std::find_if(roadSplines.begin(), roadSplines.end(), [&](const RoadSpline& existing) {
        return existing.id == roadSpline.id;
    });
    if (found == roadSplines.end()) throw std::invalid_argument("RoadSpline does not exist.");
    *found = std::move(roadSpline);
    return Project(id_, rootMap_, std::move(roadSplines), roadSegments_, extensions_, unknownFields_);
}

Project Project::withReplacedRoadTopology(RoadSpline roadSpline, std::vector<RoadSegment> segments) const {
    if (roadSpline.id.empty() || roadSpline.mapId != rootMap_.id) {
        throw std::invalid_argument("RoadSpline requires a non-empty ID owned by the root Map.");
    }
    auto roads = roadSplines_;
    const auto road = std::find_if(roads.begin(), roads.end(), [&](const RoadSpline& existing) {
        return existing.id == roadSpline.id;
    });
    if (road == roads.end()) throw std::invalid_argument("RoadSpline does not exist.");

    auto allSegments = roadSegments_;
    allSegments.erase(std::remove_if(allSegments.begin(), allSegments.end(), [&](const RoadSegment& segment) {
        return segment.roadSplineId == roadSpline.id;
    }), allSegments.end());
    roadSpline.segmentIds.clear();
    for (const auto& segment : segments) {
        if (segment.id.empty() || segment.mapId != rootMap_.id || segment.roadSplineId != roadSpline.id ||
            std::any_of(allSegments.begin(), allSegments.end(), [&](const RoadSegment& existing) {
                return existing.id == segment.id;
            })) {
            throw std::invalid_argument("Replacement RoadSegment has invalid ownership or duplicate ID.");
        }
        roadSpline.segmentIds.push_back(segment.id);
        allSegments.push_back(segment);
    }
    if (segments.empty()) throw std::invalid_argument("RoadSpline topology requires at least one RoadSegment.");
    *road = std::move(roadSpline);
    return Project(id_, rootMap_, std::move(roads), std::move(allSegments), extensions_, unknownFields_);
}

Project Project::withoutRoadSpline(const std::string& roadSplineId) const {
    if (std::any_of(roadSegments_.begin(), roadSegments_.end(), [&](const RoadSegment& segment) {
        return segment.roadSplineId == roadSplineId;
    })) {
        throw std::invalid_argument("RoadSpline has dependent RoadSegments.");
    }
    auto roadSplines = roadSplines_;
    const auto originalSize = roadSplines.size();
    roadSplines.erase(std::remove_if(roadSplines.begin(), roadSplines.end(), [&](const RoadSpline& road) {
        return road.id == roadSplineId;
    }), roadSplines.end());
    if (roadSplines.size() == originalSize) throw std::invalid_argument("RoadSpline does not exist.");
    return Project(id_, rootMap_, std::move(roadSplines), roadSegments_, extensions_, unknownFields_);
}

Project Project::withoutRoadSplineAndSegments(const std::string& roadSplineId) const {
    auto roadSplines = roadSplines_;
    const auto originalSize = roadSplines.size();
    roadSplines.erase(std::remove_if(roadSplines.begin(), roadSplines.end(), [&](const RoadSpline& road) {
        return road.id == roadSplineId;
    }), roadSplines.end());
    if (roadSplines.size() == originalSize) throw std::invalid_argument("RoadSpline does not exist.");
    auto roadSegments = roadSegments_;
    roadSegments.erase(std::remove_if(roadSegments.begin(), roadSegments.end(), [&](const RoadSegment& segment) {
        return segment.roadSplineId == roadSplineId;
    }), roadSegments.end());
    return Project(id_, rootMap_, std::move(roadSplines), std::move(roadSegments), extensions_, unknownFields_);
}

Project Project::withRoadSegment(RoadSegment roadSegment) const {
    if (roadSegment.id.empty() || roadSegment.mapId != rootMap_.id || roadSegment.roadSplineId.empty()) {
        throw std::invalid_argument("RoadSegment requires an ID, owning Map, and RoadSpline reference.");
    }
    const auto parent = std::find_if(roadSplines_.begin(), roadSplines_.end(), [&](const RoadSpline& roadSpline) {
        return roadSpline.id == roadSegment.roadSplineId;
    });
    if (parent == roadSplines_.end()) {
        throw std::invalid_argument("RoadSegment references an unknown RoadSpline.");
    }
    if (std::any_of(roadSegments_.begin(), roadSegments_.end(), [&](const RoadSegment& existing) {
        return existing.id == roadSegment.id;
    })) {
        throw std::invalid_argument("RoadSegment ID must be unique.");
    }
    auto roadSegments = roadSegments_;
    roadSegments.push_back(std::move(roadSegment));
    return Project(id_, rootMap_, roadSplines_, std::move(roadSegments), extensions_, unknownFields_);
}

// Serialize the project into a canonical JSON format.
std::string Project::normalizedJson(int schemaGeneration) const {
    if (schemaGeneration < 1 || schemaGeneration > 2) {
        throw std::invalid_argument("Unsupported project schema generation.");
    }
    if (schemaGeneration == 1 && (!roadSplines_.empty() || !roadSegments_.empty())) {
        throw std::invalid_argument("Schema generation 1 cannot represent RoadSpline or RoadSegment records.");
    }
    nlohmann::ordered_json normalized = {
        {"id", id_},
        {"type", "core.Project"},
        {"schemaVersion", schemaGeneration},
        {"units", "m"},
        {"maps", nlohmann::ordered_json::array({ normalizeRootMap(rootMap_) })}
    };

    if (schemaGeneration >= 2) {
        normalized["roadSplines"] = nlohmann::ordered_json::array();
        normalized["roadSegments"] = nlohmann::ordered_json::array();
        auto roadSplines = roadSplines_;
        std::sort(roadSplines.begin(), roadSplines.end(), [](const RoadSpline& left, const RoadSpline& right) {
            return left.id < right.id;
        });
        for (const auto& roadSpline : roadSplines) normalized["roadSplines"].push_back(roadSpline.normalizedJson());

        auto roadSegments = roadSegments_;
        std::sort(roadSegments.begin(), roadSegments.end(), [](const RoadSegment& left, const RoadSegment& right) {
            return left.id < right.id;
        });
        for (const auto& roadSegment : roadSegments) normalized["roadSegments"].push_back(roadSegment.normalizedJson());
    }

    if (!extensions_.empty()) {
        normalized["extensions"] = extensions_;
    }

    // Unknown data is written back under its original key; this code does not reinterpret it.
    for (const auto& [key, value] : unknownFields_.items()) {
        normalized[key] = value;
    }

    return normalized.dump(2) + "\n";
}

} // namespace atlas::domain
