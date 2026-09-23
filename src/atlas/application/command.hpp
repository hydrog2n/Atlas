#pragma once

#include "atlas/domain/project.hpp"

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace atlas::application {

struct Revision {
    std::uint64_t number = 0;
    domain::Project project = domain::Project::empty("project", "root-map");
    nlohmann::json selection = nlohmann::json::object();

    // Return only authoritative project state for undo and deterministic comparisons.
    std::string normalizedSource() const;
};

enum class Severity { error, warning, information };

struct Diagnostic {
    std::string ruleId;
    Severity severity = Severity::information;
    std::vector<std::string> affectedIds;
    std::string message;
    std::uint64_t revision = 0;
    std::vector<std::string> repairIds;
};

struct StationRange {
    double start = 0.0;
    double end = 0.0;

    bool overlaps(const StationRange& other) const noexcept;
};

struct Invalidation {
    std::string sourceId;
    std::string propertyClass;
    std::uint64_t revision = 0;
    std::optional<StationRange> stationRange;
    std::string spatialRegion;
};

std::vector<Invalidation> coalesceInvalidations(std::vector<Invalidation> invalidations);

struct VersionedResult {
    std::string cacheId;
    std::uint64_t sourceRevision = 0;
    std::string payload;
    std::vector<std::string> sourceDependencies;
};

class DependencyGraph {
public:
    // Register a directed source-to-derived dependency.
    void addDependency(
        std::string sourceId,
        std::string derivedId,
        std::optional<StationRange> affectedRange = {});
    // Return each derived product affected by the specified source.
    std::vector<std::string> dependentsOf(const std::string& sourceId) const;
    // Return only dependents whose declared range intersects the invalidation.
    std::vector<std::string> dependentsFor(const Invalidation& invalidation) const;

private:
    struct Edge {
        std::string sourceId;
        std::string derivedId;
        std::optional<StationRange> affectedRange;
    };

    std::vector<Edge> edges_;
};

enum class ReferenceStrength { required, optional };

class ReverseReferenceIndex {
public:
    void addReference(
        std::string targetId,
        std::string dependentId,
        ReferenceStrength strength);
    std::vector<std::string> dependentsOf(const std::string& targetId) const;
    std::vector<std::string> requiredDependentsOf(const std::string& targetId) const;

private:
    struct Reference {
        std::string targetId;
        std::string dependentId;
        ReferenceStrength strength;
    };

    std::vector<Reference> references_;
};

enum class ImpactResolution { cancel, deleteDependents, retarget };

struct DestructiveImpact {
    std::vector<std::string> affectedIds;
    bool resolved = false;
};

DestructiveImpact resolveDestructiveImpact(
    std::vector<std::string> affectedIds,
    ImpactResolution resolution);
DestructiveImpact resolveDestructiveImpact(
    const ReverseReferenceIndex& references,
    const std::string& targetId,
    ImpactResolution resolution);

struct Preview {
    Revision candidate;
    std::vector<std::string> impactIds;
    std::vector<Invalidation> invalidations;
    std::vector<Diagnostic> diagnostics;
};

class Command {
public:
    virtual ~Command() = default;
    // The command name is a stable diagnostic and history identifier.
    virtual const char* name() const noexcept = 0;
    virtual std::optional<std::uint64_t> expectedRevision() const noexcept = 0;
    virtual std::string coalesceKey() const = 0;
    virtual std::vector<Diagnostic> validate(const Revision& current) const;
    virtual Revision apply(const Revision& current) const = 0;
};

class ReplaceProjectCommand final : public Command {
public:
    ReplaceProjectCommand(domain::Project project, std::optional<std::uint64_t> expectedRevision = {});

    const char* name() const noexcept override;
    std::optional<std::uint64_t> expectedRevision() const noexcept override;
    std::string coalesceKey() const override;
    Revision apply(const Revision& current) const override;

private:
    domain::Project project_;
    std::optional<std::uint64_t> expectedRevision_;
};

class RepairProjectCommand final : public Command {
public:
    RepairProjectCommand(domain::Project repairedProject, std::uint64_t expectedRevision);

    const char* name() const noexcept override;
    std::optional<std::uint64_t> expectedRevision() const noexcept override;
    std::string coalesceKey() const override;
    Revision apply(const Revision& current) const override;

private:
    domain::Project repairedProject_;
    std::uint64_t expectedRevision_;
};

class CommandProcessor {
public:
    // Start processing from an immutable authoritative revision snapshot.
    explicit CommandProcessor(Revision initial);

    // Read the current authoritative revision without exposing mutable state.
    const Revision& current() const noexcept;
    // Calculate a candidate revision without changing history or live state.
    Preview preview(const Command& command) const;
    // Validate and atomically install a command result.
    void commit(const Command& command);
    // Explicitly discard a preview; previews never own live state.
    void cancel(const Preview& preview) const noexcept;
    // Restore the previous authoritative revision.
    bool undo();
    // Reapply the most recently undone authoritative revision.
    bool redo();
    bool canUndo() const noexcept;
    bool canRedo() const noexcept;
    // Accept derived work only when it was calculated from the current revision.
    bool acceptResult(const VersionedResult& result);
    void registerCacheDependencies(std::string cacheId, std::vector<std::string> sourceDependencies);
    const std::string* cachedResult(const std::string& cacheId) const noexcept;
    const DependencyGraph& dependencies() const noexcept;

    void addDiagnostic(Diagnostic diagnostic);
    void recordRebuildFailure(
        std::string objectId,
        std::string ruleId,
        std::string message,
        std::vector<std::string> repairIds = {});
    const std::vector<Diagnostic>& diagnostics() const noexcept;

private:
    Revision current_;
    std::vector<Revision> undoStack_;
    std::vector<Revision> redoStack_;
    std::vector<Diagnostic> diagnostics_;
    DependencyGraph dependencies_;
    std::map<std::string, VersionedResult> caches_;
    std::map<std::string, std::vector<std::string>> cacheDependencies_;
    std::string lastCoalesceKey_;
};

} // namespace atlas::application
