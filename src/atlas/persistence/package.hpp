#pragma once

#include "atlas/domain/project.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace atlas::persistence {

struct SaveOptions {
    // A zero count disables checkpoint creation; otherwise the newest checkpoints are retained.
    std::size_t checkpointCount = 3;
    enum class FailurePoint {
        none,
        afterStaging,
        afterValidation,
        afterCheckpoint,
        afterBackup,
        diskFull,
        permissionDenied,
        forcedTermination,
        journalTruncated
    };
    FailurePoint failurePoint = FailurePoint::none;
};

struct LoadOptions {
    bool allowNewerSchemaReadOnly = false;
};

struct MigrationReport {
    // These versions describe the package schema, not the Atlas application version.
    int fromSchema = 0;
    int toSchema = 0;
    bool changed = false;
    bool dryRun = false;
    bool succeeded = true;
    std::string error;
    std::vector<std::string> changes;

    std::string toJson() const;
};

class Package {
public:
    // Construct a persistence object without coupling the domain model to filesystem code.
    static Package fromProject(const domain::Project& project);
    // Load and validate a directory package, including its authoritative content hash.
    static Package load(
        const std::filesystem::path& packagePath,
        LoadOptions options = {});
    // Prefer the newest checkpoint and fall back to the current package when none exists.
    static Package recover(const std::filesystem::path& packagePath);
    // Produce a deterministic migration report, optionally applying the supported migration.
    static MigrationReport migrate(
        const std::filesystem::path& packagePath,
        int targetSchema,
        bool dryRun = false);

    // Save through a sibling staging directory before replacing the existing package.
    void save(const std::filesystem::path& packagePath, SaveOptions options = {}) const;

    const domain::Project& project() const noexcept;
    std::string manifestJson() const;

private:
    explicit Package(domain::Project project, bool readOnly = false);

    domain::Project project_;
    bool readOnly_ = false;
};

} // namespace atlas::persistence
