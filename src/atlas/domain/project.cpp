#include "atlas/domain/project.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace atlas::domain {

namespace {

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
    return Project(std::move(projectId), std::move(rootMap));
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

    nlohmann::json extensions = parsed.value("extensions", nlohmann::json::object());
    if (!extensions.is_object()) {
        throw std::invalid_argument("Project extensions must be an object.");
    }

    // Keep fields outside the current schema instead of silently dropping them.
    nlohmann::json unknownFields = nlohmann::json::object();
    for (const auto& [key, value] : parsed.items()) {
        if (key != "id" && key != "schemaVersion" && key != "units" && key != "maps" && key != "extensions") {
            unknownFields[key] = value;
        }
    }

    return Project(
        parsed["id"].get<std::string>(),
        std::move(rootMap),
        std::move(extensions),
        std::move(unknownFields));
}

// Store the project identity and extension data in the class.
Project::Project(
    std::string projectId,
    Map rootMap,
    nlohmann::json extensions,
    nlohmann::json unknownFields)
    : id_(std::move(projectId)),
      rootMap_(std::move(rootMap)),
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

const nlohmann::json& Project::unknownFields() const noexcept {
    // Unknown fields remain available for lossless round trips.
    return unknownFields_;
}

Project Project::withRootMap(Map rootMap) const {
    rootMap.validateObjectReferences();
    return Project(id_, std::move(rootMap), extensions_, unknownFields_);
}

// Serialize the project into a canonical JSON format.
std::string Project::normalizedJson() const {
    nlohmann::ordered_json normalized = {
        {"id", id_},
        {"type", "core.Project"},
        {"schemaVersion", 1},
        {"units", "m"},
        {"maps", nlohmann::ordered_json::array({ normalizeRootMap(rootMap_) })}
    };

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
