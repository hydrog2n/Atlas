#include "atlas/domain/project.hpp"

#include <nlohmann/json.hpp>

#include <stdexcept>
#include <utility>

namespace atlas::domain {

namespace {

// Canonical root-map shape used for deterministic serialization.
nlohmann::ordered_json normalizeRootMap(const Map& rootMap) {
    return nlohmann::ordered_json{
        {"id", rootMap.id},
        {"parentMapId", nullptr},
        {"objects", nlohmann::ordered_json::array()},
        {"networkObjects", nlohmann::ordered_json::array()}
    };
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

    nlohmann::json extensions = parsed.value("extensions", nlohmann::json::object());
    if (!extensions.is_object()) {
        throw std::invalid_argument("Project extensions must be an object.");
    }

    return Project(
        parsed["id"].get<std::string>(),
        Map{rootMapJson["id"].get<std::string>()},
        std::move(extensions));
}

// Store the project identity and extension data in the class.
Project::Project(std::string projectId, Map rootMap, nlohmann::json extensions)
    : id_(std::move(projectId)), rootMap_(std::move(rootMap)), extensions_(std::move(extensions)) {}

const std::string& Project::id() const noexcept {
    return id_;
}

const Map& Project::rootMap() const noexcept {
    return rootMap_;
}

const nlohmann::json& Project::extensions() const noexcept {
    return extensions_;
}

// Serialize the project into a canonical JSON format.
std::string Project::normalizedJson() const {
    nlohmann::ordered_json normalized = {
        {"id", id_},
        {"schemaVersion", 1},
        {"units", "m"},
        {"maps", nlohmann::ordered_json::array({ normalizeRootMap(rootMap_) })}
    };

    if (!extensions_.empty()) {
        normalized["extensions"] = extensions_;
    }

    return normalized.dump(2) + "\n";
}

} // namespace atlas::domain
