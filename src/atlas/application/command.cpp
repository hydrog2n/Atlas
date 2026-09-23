#include "atlas/application/command.hpp"

#include <stdexcept>
#include <utility>
#include <algorithm>

namespace atlas::application {

bool StationRange::overlaps(const StationRange& other) const noexcept {
    const auto firstStart = std::min(start, end);
    const auto firstEnd = std::max(start, end);
    const auto secondStart = std::min(other.start, other.end);
    const auto secondEnd = std::max(other.start, other.end);
    return firstStart <= secondEnd && secondStart <= firstEnd;
}

std::vector<Invalidation> coalesceInvalidations(std::vector<Invalidation> invalidations) {
    std::vector<Invalidation> result;
    for (auto invalidation : invalidations) {
        bool merged = false;
        for (auto& existing : result) {
            if (existing.sourceId != invalidation.sourceId ||
                existing.propertyClass != invalidation.propertyClass ||
                existing.revision != invalidation.revision ||
                existing.spatialRegion != invalidation.spatialRegion) {
                continue;
            }
            if (!existing.stationRange || !invalidation.stationRange) {
                existing.stationRange.reset();
                merged = true;
                break;
            }
            if (existing.stationRange->overlaps(*invalidation.stationRange)) {
                existing.stationRange = StationRange{
                    std::min({existing.stationRange->start, existing.stationRange->end,
                        invalidation.stationRange->start, invalidation.stationRange->end}),
                    std::max({existing.stationRange->start, existing.stationRange->end,
                        invalidation.stationRange->start, invalidation.stationRange->end})};
                merged = true;
                break;
            }
        }
        if (!merged) result.push_back(std::move(invalidation));
    }
    return result;
}

// Return the authoritative source representation used for deterministic comparisons.
std::string Revision::normalizedSource() const {
    // Selection and caches are intentionally excluded from authoritative comparison.
    return project.normalizedJson();
}

// Store the project and the revision against which this command may run.
ReplaceProjectCommand::ReplaceProjectCommand(
    domain::Project project,
    std::optional<std::uint64_t> expectedRevision)
    : project_(std::move(project)), expectedRevision_(expectedRevision) {}

// Identify the generic project replacement command in history and diagnostics.
const char* ReplaceProjectCommand::name() const noexcept {
    // This command is the generic property-edit placeholder for the foundation slice.
    return "replace-project";
}

// Return the revision precondition supplied by the caller.
std::optional<std::uint64_t> ReplaceProjectCommand::expectedRevision() const noexcept {
    return expectedRevision_;
}

// Group consecutive replacement edits into one undo entry.
std::string ReplaceProjectCommand::coalesceKey() const {
    // Consecutive replacements represent one continuous edit in the history.
    return "replace-project";
}

// Store the repaired project and the revision it is allowed to modify.
RepairProjectCommand::RepairProjectCommand(
    domain::Project repairedProject,
    std::uint64_t expectedRevision)
    : repairedProject_(std::move(repairedProject)), expectedRevision_(expectedRevision) {}

// Identify the repair command in history and diagnostics.
const char* RepairProjectCommand::name() const noexcept {
    // Repairs use the same command path as ordinary mutations.
    return "repair-project";
}

// Return the repair command's required source revision.
std::optional<std::uint64_t> RepairProjectCommand::expectedRevision() const noexcept {
    return expectedRevision_;
}

// Keep repairs as independent history entries instead of coalescing them.
std::string RepairProjectCommand::coalesceKey() const {
    // Repairs remain separate history entries so each repair can be undone independently.
    return {};
}

std::vector<Diagnostic> Command::validate(const Revision&) const {
    return {};
}

// Produce a new revision containing the repaired authoritative project.
Revision RepairProjectCommand::apply(const Revision& current) const {
    return Revision{current.number + 1, repairedProject_, current.selection};
}

// Apply a project replacement to a copied revision.
Revision ReplaceProjectCommand::apply(const Revision& current) const {
    return Revision{current.number + 1, project_, current.selection};
}

// Initialize the processor with the first authoritative revision snapshot.
CommandProcessor::CommandProcessor(Revision initial)
    : current_(std::move(initial)) {}

// Return the current authoritative revision.
const Revision& CommandProcessor::current() const noexcept {
    return current_;
}

// Register a source-to-derived dependency edge.
void DependencyGraph::addDependency(
    std::string sourceId,
    std::string derivedId,
    std::optional<StationRange> affectedRange) {
    // Keep dependency ownership explicit; scheduling policy is intentionally deferred.
    edges_.push_back(Edge{std::move(sourceId), std::move(derivedId), affectedRange});
}

// Find unique derived products that depend on a source record.
std::vector<std::string> DependencyGraph::dependentsOf(const std::string& sourceId) const {
    // Deduplicate results so one source invalidates each derived product once.
    std::vector<std::string> result;
    for (const auto& edge : edges_) {
        if (edge.sourceId == sourceId &&
            std::find(result.begin(), result.end(), edge.derivedId) == result.end()) {
            result.push_back(edge.derivedId);
        }
    }
    return result;
}

std::vector<std::string> DependencyGraph::dependentsFor(const Invalidation& invalidation) const {
    std::vector<std::string> result;
    for (const auto& edge : edges_) {
        if (edge.sourceId != invalidation.sourceId ||
            (edge.affectedRange && invalidation.stationRange &&
                !edge.affectedRange->overlaps(*invalidation.stationRange))) {
            continue;
        }
        if (std::find(result.begin(), result.end(), edge.derivedId) == result.end()) {
            result.push_back(edge.derivedId);
        }
    }
    return result;
}

void ReverseReferenceIndex::addReference(
    std::string targetId,
    std::string dependentId,
    ReferenceStrength strength) {
    references_.push_back(Reference{std::move(targetId), std::move(dependentId), strength});
}

std::vector<std::string> ReverseReferenceIndex::dependentsOf(const std::string& targetId) const {
    std::vector<std::string> result;
    for (const auto& reference : references_) {
        if (reference.targetId == targetId &&
            std::find(result.begin(), result.end(), reference.dependentId) == result.end()) {
            result.push_back(reference.dependentId);
        }
    }
    return result;
}

std::vector<std::string> ReverseReferenceIndex::requiredDependentsOf(
    const std::string& targetId) const {
    std::vector<std::string> result;
    for (const auto& reference : references_) {
        if (reference.targetId == targetId &&
            reference.strength == ReferenceStrength::required &&
            std::find(result.begin(), result.end(), reference.dependentId) == result.end()) {
            result.push_back(reference.dependentId);
        }
    }
    return result;
}

// Resolve a destructive impact explicitly; cancellation never authorizes mutation.
DestructiveImpact resolveDestructiveImpact(
    std::vector<std::string> affectedIds,
    ImpactResolution resolution) {
    return DestructiveImpact{std::move(affectedIds), resolution != ImpactResolution::cancel};
}

DestructiveImpact resolveDestructiveImpact(
    const ReverseReferenceIndex& references,
    const std::string& targetId,
    ImpactResolution resolution) {
    return resolveDestructiveImpact(references.dependentsOf(targetId), resolution);
}

// Validate a command and calculate its candidate revision without mutating state.
Preview CommandProcessor::preview(const Command& command) const {
    // Revision validation happens before apply so stale commands cannot produce candidates.
    // Reject commands created against an older source revision.
    if (const auto expected = command.expectedRevision(); expected && *expected != current_.number) {
        throw std::runtime_error("Command revision is stale.");
    }

    Preview result;
    result.diagnostics = command.validate(current_);
    result.candidate = command.apply(current_);
    result.impactIds.push_back(result.candidate.project.id());
    // A committed candidate always invalidates the changed project source.
    result.invalidations.push_back(Invalidation{
        result.candidate.project.id(), "project-source", result.candidate.number, {}, {}});
    return result;
}

// Install a validated candidate and preserve the previous revision for undo.
void CommandProcessor::commit(const Command& command) {
    // Store the prior source snapshot before installing the candidate for undo.
    const auto candidate = preview(command);
    diagnostics_.insert(diagnostics_.end(), candidate.diagnostics.begin(), candidate.diagnostics.end());
    const auto hasError = std::any_of(candidate.diagnostics.begin(), candidate.diagnostics.end(),
        [](const Diagnostic& diagnostic) { return diagnostic.severity == Severity::error; });
    if (hasError) throw std::runtime_error("Command preconditions are invalid.");
    const auto key = command.coalesceKey();
    // Start a new undo entry only when the edit is not a continuation of the prior one.
    if (key.empty() || key != lastCoalesceKey_) {
        undoStack_.push_back(current_);
    }
    current_ = candidate.candidate;
    redoStack_.clear();
    lastCoalesceKey_ = key;
}

// Discard a preview without touching processor state.
void CommandProcessor::cancel(const Preview& preview) const noexcept {
    // A preview owns no processor state, so cancellation is deliberately a no-op.
    static_cast<void>(preview);
}

// Move the current revision to redo history and restore the previous revision.
bool CommandProcessor::undo() {
    // Moving snapshots between stacks preserves authoritative state exactly.
    // There is nothing to restore when the undo stack is empty.
    if (undoStack_.empty()) return false;
    redoStack_.push_back(current_);
    current_ = undoStack_.back();
    undoStack_.pop_back();
    lastCoalesceKey_.clear();
    return true;
}

// Move the current revision to undo history and restore the next revision.
bool CommandProcessor::redo() {
    // There is nothing to restore when the redo stack is empty.
    if (redoStack_.empty()) return false;
    undoStack_.push_back(current_);
    current_ = redoStack_.back();
    redoStack_.pop_back();
    lastCoalesceKey_.clear();
    return true;
}

// Report whether an undo operation is currently available.
bool CommandProcessor::canUndo() const noexcept {
    // UI callers use this query to enable or disable undo actions.
    return !undoStack_.empty();
}

// Report whether a redo operation is currently available.
bool CommandProcessor::canRedo() const noexcept {
    // UI callers use this query to enable or disable redo actions.
    return !redoStack_.empty();
}

// Store a revision-tagged diagnostic for the application layer.
void CommandProcessor::addDiagnostic(Diagnostic diagnostic) {
    // Diagnostics are revision-tagged records owned by the application layer.
    diagnostics_.push_back(std::move(diagnostic));
}

void CommandProcessor::recordRebuildFailure(
    std::string objectId,
    std::string ruleId,
    std::string message,
    std::vector<std::string> repairIds) {
    diagnostics_.push_back(Diagnostic{
        std::move(ruleId), Severity::error, {std::move(objectId)}, std::move(message),
        current_.number, std::move(repairIds)});
}

// Return diagnostics without transferring ownership to the caller.
const std::vector<Diagnostic>& CommandProcessor::diagnostics() const noexcept {
    return diagnostics_;
}

// Accept derived work only when it was computed from the current revision.
bool CommandProcessor::acceptResult(const VersionedResult& result) {
    // Results from older revisions are discarded rather than replacing current caches.
    // Stale results are discarded without changing current cache state.
    if (result.sourceRevision != current_.number) {
        return false;
    }
    const auto expected = cacheDependencies_.find(result.cacheId);
    if (expected != cacheDependencies_.end()) {
        auto actual = result.sourceDependencies;
        std::sort(actual.begin(), actual.end());
        if (actual != expected->second) return false;
    }
    caches_[result.cacheId] = result;
    return true;
}

void CommandProcessor::registerCacheDependencies(
    std::string cacheId,
    std::vector<std::string> sourceDependencies) {
    std::sort(sourceDependencies.begin(), sourceDependencies.end());
    sourceDependencies.erase(
        std::unique(sourceDependencies.begin(), sourceDependencies.end()), sourceDependencies.end());
    cacheDependencies_[std::move(cacheId)] = std::move(sourceDependencies);
}

// Return the payload of an accepted derived result, if present.
const std::string* CommandProcessor::cachedResult(const std::string& cacheId) const noexcept {
    const auto found = caches_.find(cacheId);
    return found == caches_.end() ? nullptr : &found->second.payload;
}

// Expose the dependency graph used by the correctness-first scheduler boundary.
const DependencyGraph& CommandProcessor::dependencies() const noexcept {
    return dependencies_;
}

} // namespace atlas::application
