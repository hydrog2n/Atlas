#pragma once

#include <nlohmann/json.hpp>

#include <string>

namespace atlas::domain {

// A map is the smallest meaningful container in the project model.
struct Map {
    std::string id;
    nlohmann::json unknownFields = nlohmann::json::object();
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
