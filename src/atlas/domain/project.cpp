#include "atlas/domain/project.hpp"

#include <nlohmann/json.hpp>

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
        {"networkObjects", nlohmann::ordered_json::array()}
    };

    for (const auto& [key, value] : rootMap.unknownFields.items()) {
        normalized[key] = value;
    }
    return normalized;
}

} // namespace

// Create a valid default project object.
Project Project::empty(std::string projectId, std::string mapId) {
    return Project(std::move(projectId), Map{std::move(mapId)});
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
            key != "objects" && key != "networkObjects") {
            rootMapUnknownFields[key] = value;
        }
    }

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
        Map{rootMapJson["id"].get<std::string>(), std::move(rootMapUnknownFields)},
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
