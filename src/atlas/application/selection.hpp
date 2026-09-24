#pragma once

#include <string>
#include <vector>

namespace atlas::application {

class SelectionState {
public:
    void clear() noexcept;
    void select(std::string objectId, bool additive = false);
    bool cycle(const std::vector<std::string>& candidates, bool reverse = false);

    const std::vector<std::string>& selectedIds() const noexcept;
    const std::string& primaryId() const noexcept;

private:
    std::vector<std::string> selectedIds_;
    std::string primaryId_;
};

} // namespace atlas::application
