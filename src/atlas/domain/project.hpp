#pragma once

#include <nlohmann/json.hpp>

#include <string>

namespace atlas::domain {

struct Map {
    std::string id;
};

class Project {
public:
    static Project empty(std::string projectId, std::string mapId);
    static Project fromJson(const std::string& jsonText);

    const std::string& id() const noexcept;
    const Map& rootMap() const noexcept;
    const nlohmann::json& extensions() const noexcept;
    std::string normalizedJson() const;

private:
    Project(std::string projectId, Map rootMap, nlohmann::json extensions = {});

    std::string id_;
    Map rootMap_;
    nlohmann::json extensions_;
};

} // namespace atlas::domain
