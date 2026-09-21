#include "atlas/domain/project.hpp"

#include <nlohmann/json.hpp>
#include <utility>

namespace atlas::domain {

Project Project::empty(std::string projectId, std::string mapId) {
    return Project(std::move(projectId), Map{std::move(mapId)});
}

Project::Project(std::string projectId, Map rootMap)
    : id_(std::move(projectId)), rootMap_(std::move(rootMap)) {}

const std::string& Project::id() const noexcept {
    return id_;
}

const Map& Project::rootMap() const noexcept {
    return rootMap_;
}

std::string Project::normalizedJson() const {
    nlohmann::ordered_json normalized = {
        {"id", id_},
        {"schemaVersion", 1},
        {"units", "m"},
        {"maps", nlohmann::ordered_json::array({
            nlohmann::ordered_json{
                {"id", rootMap_.id},
                {"parentMapId", nullptr},
                {"objects", nlohmann::ordered_json::array()},
                {"networkObjects", nlohmann::ordered_json::array()}
            }
        })}
    };
    return normalized.dump(2) + "\n";
}

} // namespace atlas::domain
