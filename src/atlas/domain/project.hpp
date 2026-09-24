#pragma once

#include <nlohmann/json.hpp>

#include <optional>
#include <string>
#include <vector>

namespace atlas::domain {

struct DisplayLayer {
    std::string id;
    std::string name;
    bool visible = true;
    bool locked = false;
    double opacity = 1.0;
};

struct SpatialLevel {
    std::string id;
    std::string name;
};

class MapObject {
public:
    static MapObject create(
        std::string id,
        std::string type,
        nlohmann::json geometry = nlohmann::json::object());
    static MapObject fromJson(const nlohmann::json& record);

    const std::string& id() const noexcept;
    const std::string& type() const noexcept;
    const std::string& name() const noexcept;
    const nlohmann::json& geometry() const noexcept;
    const std::string& primaryDisplayLayerId() const noexcept;
    const std::optional<std::string>& spatialLevelId() const noexcept;
    const std::vector<std::string>& tags() const noexcept;
    bool visible() const noexcept;
    bool locked() const noexcept;

    MapObject withGeometry(nlohmann::json geometry) const;
    MapObject withName(std::string name) const;
    MapObject withDisplayLayer(std::string layerId) const;
    MapObject withSpatialLevel(std::optional<std::string> levelId) const;
    MapObject withVisibility(bool visible) const;
    MapObject withLocked(bool locked) const;
    nlohmann::json normalizedJson() const;

private:
    explicit MapObject(nlohmann::json record);

    nlohmann::json record_;
    std::string id_;
    std::string type_;
    std::string name_;
    std::string primaryDisplayLayerId_;
    std::optional<std::string> spatialLevelId_;
    std::vector<std::string> tags_;
};

// A map is the smallest meaningful container in the project model.
struct Map {
    std::string id;
    nlohmann::json unknownFields = nlohmann::json::object();
    std::string defaultDisplayLayerId = "default-layer";
    std::string defaultSpatialLevelId = "default-level";
    std::vector<DisplayLayer> displayLayers;
    std::vector<SpatialLevel> spatialLevels;
    std::vector<MapObject> objects;

    const MapObject* findObject(const std::string& objectId) const noexcept;
    Map withObject(MapObject object) const;
    Map withoutObject(const std::string& objectId) const;
    void validateObjectReferences() const;
};

// Project is the root object in the editable world data model.
class Project {
public:
    // Create a valid empty project with a project ID and a root map ID.
    static Project empty(std::string projectId, std::string mapId);

    // Parse project data from JSON and validate the required fields.
    static Project fromJson(const std::string& jsonText);

    // Read-only access to project data.
    const std::string& id() const noexcept;
    const Map& rootMap() const noexcept;
    const nlohmann::json& extensions() const noexcept;
    // Preserve recognized extension data separately from fields Atlas does not understand.
    const nlohmann::json& unknownFields() const noexcept;

    // Return a copy with one authoritative root-map snapshot replaced.
    Project withRootMap(Map rootMap) const;

    // Produce a canonical JSON representation for deterministic output.
    std::string normalizedJson() const;

private:
    // Use the factory methods to create valid project instances.
    Project(
        std::string projectId,
        Map rootMap,
        nlohmann::json extensions = {},
        nlohmann::json unknownFields = {});

    std::string id_;
    Map rootMap_;
    nlohmann::json extensions_;
    // Unknown fields remain opaque so older versions can round-trip newer data.
    nlohmann::json unknownFields_;
};

} // namespace atlas::domain
