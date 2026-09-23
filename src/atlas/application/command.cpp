#include "atlas/application/command.hpp"

#include <stdexcept>
#include <utility>
#include <algorithm>

namespace atlas::application {

std::string Revision::normalizedSource() const {
    return project.normalizedJson();
}

ReplaceProjectCommand::ReplaceProjectCommand(
    domain::Project project,
    std::optional<std::uint64_t> expectedRevision)
    : project_(std::move(project)), expectedRevision_(expectedRevision) {}

const char* ReplaceProjectCommand::name() const noexcept {
    return "replace-project";
}

std::optional<std::uint64_t> ReplaceProjectCommand::expectedRevision() const noexcept {
    return expectedRevision_;
}

std::string ReplaceProjectCommand::coalesceKey() const {
    return "replace-project";
}

RepairProjectCommand::RepairProjectCommand(
    domain::Project repairedProject,
    std::uint64_t expectedRevision)
    : repairedProject_(std::move(repairedProject)), expectedRevision_(expectedRevision) {}

const char* RepairProjectCommand::name() const noexcept {
    return "repair-project";
}

std::optional<std::uint64_t> RepairProjectCommand::expectedRevision() const noexcept {
    return expectedRevision_;
}

std::string RepairProjectCommand::coalesceKey() const {
    return {};
}

Revision RepairProjectCommand::apply(const Revision& current) const {
    return Revision{current.number + 1, repairedProject_, current.selection};
}

Revision ReplaceProjectCommand::apply(const Revision& current) const {
    return Revision{current.number + 1, project_, current.selection};
}

CommandProcessor::CommandProcessor(Revision initial)
    : current_(std::move(initial)) {}

const Revision& CommandProcessor::current() const noexcept {
    return current_;
}

void DependencyGraph::addDependency(std::string sourceId, std::string derivedId) {
    edges_.emplace_back(std::move(sourceId), std::move(derivedId));
}

std::vector<std::string> DependencyGraph::dependentsOf(const std::string& sourceId) const {
    std::vector<std::string> result;
    for (const auto& [source, derived] : edges_) {
        if (source == sourceId && std::find(result.begin(), result.end(), derived) == result.end()) {
            result.push_back(derived);
        }
    }
    return result;
}

DestructiveImpact resolveDestructiveImpact(
    std::vector<std::string> affectedIds,
    ImpactResolution resolution) {
    return DestructiveImpact{std::move(affectedIds), resolution != ImpactResolution::cancel};
}

Preview CommandProcessor::preview(const Command& command) const {
    if (const auto expected = command.expectedRevision(); expected && *expected != current_.number) {
        throw std::runtime_error("Command revision is stale.");
    }

    Preview result;
    result.candidate = command.apply(current_);
    result.impactIds.push_back(result.candidate.project.id());
    result.invalidations.push_back(
        Invalidation{result.candidate.project.id(), "project-source", result.candidate.number});
    return result;
}

void CommandProcessor::commit(const Command& command) {
    const auto candidate = preview(command);
    const auto key = command.coalesceKey();
    if (key.empty() || key != lastCoalesceKey_) {
        undoStack_.push_back(current_);
    }
    current_ = candidate.candidate;
    redoStack_.clear();
    lastCoalesceKey_ = key;
}

void CommandProcessor::cancel(const Preview& preview) const noexcept {
    static_cast<void>(preview);
}

bool CommandProcessor::undo() {
    if (undoStack_.empty()) return false;
    redoStack_.push_back(current_);
    current_ = undoStack_.back();
    undoStack_.pop_back();
    lastCoalesceKey_.clear();
    return true;
}

bool CommandProcessor::redo() {
    if (redoStack_.empty()) return false;
    undoStack_.push_back(current_);
    current_ = redoStack_.back();
    redoStack_.pop_back();
    lastCoalesceKey_.clear();
    return true;
}

bool CommandProcessor::canUndo() const noexcept {
    return !undoStack_.empty();
}

bool CommandProcessor::canRedo() const noexcept {
    return !redoStack_.empty();
}

void CommandProcessor::addDiagnostic(Diagnostic diagnostic) {
    diagnostics_.push_back(std::move(diagnostic));
}

const std::vector<Diagnostic>& CommandProcessor::diagnostics() const noexcept {
    return diagnostics_;
}

bool CommandProcessor::acceptResult(const VersionedResult& result) {
    if (result.sourceRevision != current_.number) {
        return false;
    }
    caches_[result.cacheId] = result;
    return true;
}

const std::string* CommandProcessor::cachedResult(const std::string& cacheId) const noexcept {
    const auto found = caches_.find(cacheId);
    return found == caches_.end() ? nullptr : &found->second.payload;
}

const DependencyGraph& CommandProcessor::dependencies() const noexcept {
    return dependencies_;
}

} // namespace atlas::application
