#pragma once

#include <string>

namespace atlas::domain {

struct Map {
    std::string id;
};

class Project {
public:
    static Project empty(std::string projectId, std::string mapId);

    const std::string& id() const noexcept;
    const Map& rootMap() const noexcept;
    std::string normalizedJson() const;

private:
    Project(std::string projectId, Map rootMap);

    std::string id_;
    Map rootMap_;
};

} // namespace atlas::domain
