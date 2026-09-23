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

struct Invalidation {
    std::string sourceId;
    std::string propertyClass;
    std::uint64_t revision = 0;
};

struct VersionedResult {
    std::string cacheId;
    std::uint64_t sourceRevision = 0;
    std::string payload;
};

class DependencyGraph {
public:
    void addDependency(std::string sourceId, std::string derivedId);
    std::vector<std::string> dependentsOf(const std::string& sourceId) const;

private:
    std::vector<std::pair<std::string, std::string>> edges_;
};

enum class ImpactResolution { cancel, deleteDependents, retarget };

struct DestructiveImpact {
    std::vector<std::string> affectedIds;
    bool resolved = false;
};

DestructiveImpact resolveDestructiveImpact(
    std::vector<std::string> affectedIds,
    ImpactResolution resolution);

struct Preview {
    Revision candidate;
    std::vector<std::string> impactIds;
    std::vector<Invalidation> invalidations;
};

class Command {
public:
    virtual ~Command() = default;
    virtual const char* name() const noexcept = 0;
    virtual std::optional<std::uint64_t> expectedRevision() const noexcept = 0;
    virtual std::string coalesceKey() const = 0;
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
    explicit CommandProcessor(Revision initial);

    const Revision& current() const noexcept;
    Preview preview(const Command& command) const;
    void commit(const Command& command);
    void cancel(const Preview& preview) const noexcept;
    bool undo();
    bool redo();
    bool canUndo() const noexcept;
    bool canRedo() const noexcept;
    bool acceptResult(const VersionedResult& result);
    const std::string* cachedResult(const std::string& cacheId) const noexcept;
    const DependencyGraph& dependencies() const noexcept;

    void addDiagnostic(Diagnostic diagnostic);
    const std::vector<Diagnostic>& diagnostics() const noexcept;

private:
    Revision current_;
    std::vector<Revision> undoStack_;
    std::vector<Revision> redoStack_;
    std::vector<Diagnostic> diagnostics_;
    DependencyGraph dependencies_;
    std::map<std::string, VersionedResult> caches_;
    std::string lastCoalesceKey_;
};

} // namespace atlas::application
