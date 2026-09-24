#include "atlas/application/selection.hpp"

#include <algorithm>

namespace atlas::application {

void SelectionState::clear() noexcept {
    selectedIds_.clear();
    primaryId_.clear();
}

void SelectionState::select(std::string objectId, bool additive) {
    if (!additive) selectedIds_.clear();
    if (std::find(selectedIds_.begin(), selectedIds_.end(), objectId) == selectedIds_.end()) {
        selectedIds_.push_back(objectId);
    }
    primaryId_ = std::move(objectId);
}

bool SelectionState::cycle(const std::vector<std::string>& candidates, bool reverse) {
    if (candidates.empty()) return false;
    auto current = std::find(candidates.begin(), candidates.end(), primaryId_);
    std::size_t index = current == candidates.end()
        ? (reverse ? candidates.size() - 1 : 0)
        : static_cast<std::size_t>(std::distance(candidates.begin(), current));
    if (current != candidates.end()) {
        index = reverse ? (index + candidates.size() - 1) % candidates.size()
                        : (index + 1) % candidates.size();
    }
    select(candidates[index]);
    return true;
}

const std::vector<std::string>& SelectionState::selectedIds() const noexcept {
    return selectedIds_;
}

const std::string& SelectionState::primaryId() const noexcept {
    return primaryId_;
}

} // namespace atlas::application
