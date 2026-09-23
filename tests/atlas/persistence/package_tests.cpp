#include "atlas/persistence/package.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>

namespace {

class TemporaryPackageDirectory {
public:
    TemporaryPackageDirectory()
        : path_(std::filesystem::temp_directory_path() /
                ("atlas-persistence-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()))) {}

    ~TemporaryPackageDirectory() {
        std::error_code error;
        std::filesystem::remove_all(path_, error);
    }

    const std::filesystem::path& path() const noexcept {
        return path_;
    }

private:
    std::filesystem::path path_;
};

} // namespace

TEST(Package, R020_001_SaveLoadPreservesProjectAndManifest) {
    TemporaryPackageDirectory temporary;
    const auto packagePath = temporary.path() / "project.atlas";
    const auto original = atlas::domain::Project::fromJson(R"({
        "id": "project",
        "schemaVersion": 1,
        "units": "m",
        "maps": [
            {"id": "root-map", "parentMapId": null, "objects": [], "networkObjects": []}
        ],
        "extensions": {"future.example": {"enabled": true}},
        "unknownRecord": {"nested": [1, 2, 3]}
    })");

    atlas::persistence::Package::fromProject(original).save(packagePath);

    const auto loaded = atlas::persistence::Package::load(packagePath);
    EXPECT_EQ(loaded.project().normalizedJson(), original.normalizedJson());
    EXPECT_EQ(loaded.manifestJson(), atlas::persistence::Package::fromProject(original).manifestJson());
    EXPECT_TRUE(std::filesystem::exists(packagePath / "manifest.json"));
    EXPECT_TRUE(std::filesystem::exists(packagePath / "project.json"));
}

TEST(Package, R020_002_LoadRejectsManifestProjectMismatch) {
    TemporaryPackageDirectory temporary;
    const auto packagePath = temporary.path() / "project.atlas";
    const auto project = atlas::domain::Project::empty("project", "root-map");
    atlas::persistence::Package::fromProject(project).save(packagePath);

    std::ofstream manifest(packagePath / "manifest.json", std::ios::trunc);
    manifest << R"({"format":"atlas.project","schemaVersion":1,"projectId":"other"})";
    manifest.close();

    EXPECT_THROW(atlas::persistence::Package::load(packagePath), std::invalid_argument);
}

TEST(Package, R020_003_SaveReplacesExistingPackageThroughStaging) {
    TemporaryPackageDirectory temporary;
    const auto packagePath = temporary.path() / "project.atlas";
    atlas::persistence::Package::fromProject(
        atlas::domain::Project::empty("first", "root-map")).save(packagePath);
    atlas::persistence::Package::fromProject(
        atlas::domain::Project::empty("second", "root-map")).save(packagePath);

    EXPECT_EQ(atlas::persistence::Package::load(packagePath).project().id(), "second");
    EXPECT_FALSE(std::filesystem::exists(packagePath.parent_path() / "project.atlas.staging"));
}

TEST(Package, R020_002_SaveRejectsTraversalPath) {
    const auto project = atlas::domain::Project::empty("project", "root-map");

    EXPECT_THROW(
        atlas::persistence::Package::fromProject(project).save(
            std::filesystem::temp_directory_path() / "atlas" / ".." / "outside.atlas"),
        std::invalid_argument);
}

TEST(Package, R020_001_SaveCreatesCanonicalPackageDirectories) {
    TemporaryPackageDirectory temporary;
    const auto packagePath = temporary.path() / "project.atlas";
    atlas::persistence::Package::fromProject(
        atlas::domain::Project::empty("project", "root-map")).save(packagePath);

    EXPECT_TRUE(std::filesystem::exists(packagePath / "maps" / "root-map" / "map.json"));
    EXPECT_TRUE(std::filesystem::exists(packagePath / "maps" / "root-map" / "objects.json"));
    EXPECT_TRUE(std::filesystem::exists(packagePath / "maps" / "root-map" / "network.json"));
    EXPECT_TRUE(std::filesystem::exists(packagePath / "cache"));
}

TEST(Package, R020_002_LoadRejectsChangedAuthoritativeContent) {
    TemporaryPackageDirectory temporary;
    const auto packagePath = temporary.path() / "project.atlas";
    atlas::persistence::Package::fromProject(
        atlas::domain::Project::empty("project", "root-map")).save(packagePath);

    std::ofstream project(packagePath / "project.json", std::ios::trunc);
    project << "{\"id\":\"tampered\"}";
    project.close();

    EXPECT_THROW(atlas::persistence::Package::load(packagePath), std::invalid_argument);
}

TEST(Package, R020_003_RecoveryLoadsLatestCheckpoint) {
    TemporaryPackageDirectory temporary;
    const auto packagePath = temporary.path() / "project.atlas";
    atlas::persistence::Package::fromProject(
        atlas::domain::Project::empty("first", "root-map")).save(packagePath);
    atlas::persistence::Package::fromProject(
        atlas::domain::Project::empty("second", "root-map")).save(packagePath);

    EXPECT_EQ(atlas::persistence::Package::recover(packagePath).project().id(), "first");
}

TEST(Package, R020_004_MigrationReportsCurrentSchema) {
    TemporaryPackageDirectory temporary;
    const auto packagePath = temporary.path() / "project.atlas";
    atlas::persistence::Package::fromProject(
        atlas::domain::Project::empty("project", "root-map")).save(packagePath);

    const auto report = atlas::persistence::Package::migrate(packagePath, 1, true);

    EXPECT_EQ(report.fromSchema, 1);
    EXPECT_EQ(report.toSchema, 1);
    EXPECT_FALSE(report.changed);
    EXPECT_TRUE(report.dryRun);
    EXPECT_NE(report.toJson().find("already uses"), std::string::npos);
}

TEST(Package, R020_002_SaveRejectsUnsafeRootMapDirectoryName) {
    const auto project = atlas::domain::Project::empty("project", "bad/map");

    EXPECT_THROW(
        atlas::persistence::Package::fromProject(project).save(
            std::filesystem::temp_directory_path() / "project.atlas"),
        std::invalid_argument);
}

TEST(Package, R020_003_InjectedSaveFailuresPreservePreviousPackage) {
    TemporaryPackageDirectory temporary;
    const auto packagePath = temporary.path() / "project.atlas";
    atlas::persistence::Package::fromProject(
        atlas::domain::Project::empty("first", "root-map")).save(packagePath);

    for (const auto failurePoint : {
             atlas::persistence::SaveOptions::FailurePoint::afterStaging,
             atlas::persistence::SaveOptions::FailurePoint::afterValidation,
             atlas::persistence::SaveOptions::FailurePoint::afterCheckpoint,
             atlas::persistence::SaveOptions::FailurePoint::afterBackup}) {
        atlas::persistence::SaveOptions options;
        options.failurePoint = failurePoint;
        EXPECT_THROW(
            atlas::persistence::Package::fromProject(
                atlas::domain::Project::empty("second", "root-map")).save(packagePath, options),
            std::runtime_error);
        EXPECT_EQ(atlas::persistence::Package::load(packagePath).project().id(), "first");
    }
}

TEST(Package, R020_004_MigratesSchemaZeroToCurrentSchema) {
    TemporaryPackageDirectory temporary;
    const auto packagePath = temporary.path() / "project.atlas";
    atlas::persistence::Package::fromProject(
        atlas::domain::Project::empty("project", "root-map")).save(packagePath);

    std::ifstream manifestInput(packagePath / "manifest.json");
    auto manifest = nlohmann::json::parse(manifestInput);
    manifestInput.close();
    manifest["schemaVersion"] = 0;
    std::ofstream manifestOutput(packagePath / "manifest.json", std::ios::trunc);
    manifestOutput << manifest.dump(2) << '\n';
    manifestOutput.close();

    const auto report = atlas::persistence::Package::migrate(packagePath, 1);

    EXPECT_TRUE(report.changed);
    EXPECT_EQ(atlas::persistence::Package::load(packagePath).project().id(), "project");
}

TEST(Package, R020_002_NewerSchemaCanBeLoadedReadOnly) {
    TemporaryPackageDirectory temporary;
    const auto packagePath = temporary.path() / "project.atlas";
    atlas::persistence::Package::fromProject(
        atlas::domain::Project::empty("project", "root-map")).save(packagePath);

    std::ifstream manifestInput(packagePath / "manifest.json");
    auto manifest = nlohmann::json::parse(manifestInput);
    manifestInput.close();
    manifest["schemaVersion"] = 2;
    std::ofstream manifestOutput(packagePath / "manifest.json", std::ios::trunc);
    manifestOutput << manifest.dump(2) << '\n';
    manifestOutput.close();

    EXPECT_THROW(atlas::persistence::Package::load(packagePath), std::invalid_argument);
    const auto newer = atlas::persistence::Package::load(packagePath, {.allowNewerSchemaReadOnly = true});
    EXPECT_THROW(newer.save(packagePath), std::runtime_error);
}

TEST(Package, R020_003_FilesystemFailureSimulationPreservesPreviousPackage) {
    TemporaryPackageDirectory temporary;
    const auto packagePath = temporary.path() / "project.atlas";
    atlas::persistence::Package::fromProject(
        atlas::domain::Project::empty("first", "root-map")).save(packagePath);

    for (const auto failurePoint : {
             atlas::persistence::SaveOptions::FailurePoint::diskFull,
             atlas::persistence::SaveOptions::FailurePoint::permissionDenied,
             atlas::persistence::SaveOptions::FailurePoint::forcedTermination,
             atlas::persistence::SaveOptions::FailurePoint::journalTruncated}) {
        atlas::persistence::SaveOptions options;
        options.failurePoint = failurePoint;
        EXPECT_THROW(
            atlas::persistence::Package::fromProject(
                atlas::domain::Project::empty("second", "root-map")).save(packagePath, options),
            std::runtime_error);
        EXPECT_EQ(atlas::persistence::Package::load(packagePath).project().id(), "first");
    }
}

TEST(Package, R020_004_MigrationFailureProducesReport) {
    TemporaryPackageDirectory temporary;
    const auto packagePath = temporary.path() / "project.atlas";
    atlas::persistence::Package::fromProject(
        atlas::domain::Project::empty("project", "root-map")).save(packagePath);

    const auto report = atlas::persistence::Package::migrate(packagePath, 99);

    EXPECT_FALSE(report.succeeded);
    EXPECT_FALSE(report.error.empty());
    EXPECT_EQ(atlas::persistence::Package::load(packagePath).project().id(), "project");
}

TEST(Package, R020_003_CorruptCheckpointIsRejected) {
    TemporaryPackageDirectory temporary;
    const auto packagePath = temporary.path() / "project.atlas";
    atlas::persistence::Package::fromProject(
        atlas::domain::Project::empty("first", "root-map")).save(packagePath);
    atlas::persistence::Package::fromProject(
        atlas::domain::Project::empty("second", "root-map")).save(packagePath);

    std::ofstream checkpoint(packagePath.parent_path() / "project.atlas.checkpoint-0" / "project.json", std::ios::trunc);
    checkpoint << "{\"broken\":true}";
    checkpoint.close();

    EXPECT_THROW(atlas::persistence::Package::recover(packagePath), std::invalid_argument);
}

TEST(Package, R020_002_MalformedPackageIsRejectedWithoutMutation) {
    TemporaryPackageDirectory temporary;
    const auto packagePath = temporary.path() / "project.atlas";
    std::filesystem::create_directories(packagePath);

    std::ofstream manifest(packagePath / "manifest.json");
    manifest << "{not-json}";
    manifest.close();

    EXPECT_THROW(atlas::persistence::Package::load(packagePath), std::invalid_argument);
    EXPECT_FALSE(std::filesystem::exists(packagePath / "project.json"));
}

TEST(Package, R020_002_MissingAuthoritativeFileIsRejected) {
    TemporaryPackageDirectory temporary;
    const auto packagePath = temporary.path() / "project.atlas";
    atlas::persistence::Package::fromProject(
        atlas::domain::Project::empty("project", "root-map")).save(packagePath);
    std::filesystem::remove(packagePath / "project.json");

    EXPECT_THROW(atlas::persistence::Package::load(packagePath), std::invalid_argument);
}

TEST(Package, R020_002_OversizedJsonIsRejected) {
    TemporaryPackageDirectory temporary;
    const auto packagePath = temporary.path() / "project.atlas";
    std::filesystem::create_directories(packagePath);
    std::ofstream manifest(packagePath / "manifest.json", std::ios::binary);
    manifest << '{' << '"' << "padding" << '"' << ':' << '"'
             << std::string(8 * 1024 * 1024, 'x') << '"' << '}';
    manifest.close();

    EXPECT_THROW(atlas::persistence::Package::load(packagePath), std::invalid_argument);
}

TEST(Package, R020_001_RepeatedSaveLoadIsDeterministic) {
    TemporaryPackageDirectory temporary;
    const auto packagePath = temporary.path() / "project.atlas";
    const auto project = atlas::domain::Project::empty("project", "root-map");

    atlas::persistence::Package::fromProject(project).save(packagePath);
    const auto first = atlas::persistence::Package::load(packagePath).project().normalizedJson();
    atlas::persistence::Package::fromProject(
        atlas::persistence::Package::load(packagePath).project()).save(packagePath);
    const auto second = atlas::persistence::Package::load(packagePath).project().normalizedJson();

    EXPECT_EQ(first, second);
}

TEST(Package, R020_005_RetainedCompatibilityFixtureResavesWithoutDrift) {
    const auto fixture = std::filesystem::path(ATLAS_SOURCE_DIR) /
        "fixtures" / "compatibility" / "empty-project.atlas";
    const auto original = atlas::persistence::Package::load(fixture);
    const auto temporary = std::filesystem::temp_directory_path() / "atlas-fixture-replay.atlas";

    std::error_code cleanupError;
    std::filesystem::remove_all(temporary, cleanupError);
    atlas::persistence::Package::fromProject(original.project()).save(temporary);

    const auto replayed = atlas::persistence::Package::load(temporary);
    EXPECT_EQ(replayed.project().normalizedJson(), original.project().normalizedJson());

    std::filesystem::remove_all(temporary, cleanupError);
}

TEST(Package, R020_001_ManifestListsExistingAuthoritativeFiles) {
    TemporaryPackageDirectory temporary;
    const auto packagePath = temporary.path() / "project.atlas";
    atlas::persistence::Package::fromProject(
        atlas::domain::Project::empty("project", "root-map")).save(packagePath);

    const auto manifest = nlohmann::json::parse(
        atlas::persistence::Package::fromProject(
            atlas::domain::Project::empty("project", "root-map")).manifestJson());
    for (const auto& [relativePath, hash] : manifest["authoritativeFiles"].items()) {
        static_cast<void>(hash);
        EXPECT_TRUE(std::filesystem::exists(packagePath / relativePath));
    }
}

TEST(Package, R020_003_CheckpointRotationHonorsConfiguredCount) {
    TemporaryPackageDirectory temporary;
    const auto packagePath = temporary.path() / "project.atlas";
    atlas::persistence::Package::fromProject(
        atlas::domain::Project::empty("one", "root-map")).save(packagePath, {.checkpointCount = 2});
    atlas::persistence::Package::fromProject(
        atlas::domain::Project::empty("two", "root-map")).save(packagePath, {.checkpointCount = 2});
    atlas::persistence::Package::fromProject(
        atlas::domain::Project::empty("three", "root-map")).save(packagePath, {.checkpointCount = 2});

    EXPECT_TRUE(std::filesystem::is_directory(packagePath.parent_path() / "project.atlas.checkpoint-0"));
    EXPECT_TRUE(std::filesystem::is_directory(packagePath.parent_path() / "project.atlas.checkpoint-1"));
    EXPECT_FALSE(std::filesystem::exists(packagePath.parent_path() / "project.atlas.checkpoint-2"));
}

TEST(Package, R020_004_MigrationDryRunDoesNotChangePackage) {
    TemporaryPackageDirectory temporary;
    const auto packagePath = temporary.path() / "project.atlas";
    atlas::persistence::Package::fromProject(
        atlas::domain::Project::empty("project", "root-map")).save(packagePath);
    const auto before = atlas::persistence::Package::load(packagePath).project().normalizedJson();

    const auto report = atlas::persistence::Package::migrate(packagePath, 1, true);

    EXPECT_TRUE(report.dryRun);
    EXPECT_EQ(atlas::persistence::Package::load(packagePath).project().normalizedJson(), before);
}

TEST(Package, R020_001_CacheDirectoryIsDisposable) {
    TemporaryPackageDirectory temporary;
    const auto packagePath = temporary.path() / "project.atlas";
    atlas::persistence::Package::fromProject(
        atlas::domain::Project::empty("project", "root-map")).save(packagePath);
    std::ofstream cache(packagePath / "cache" / "derived.bin", std::ios::binary);
    cache << "disposable";
    cache.close();
    std::filesystem::remove_all(packagePath / "cache");

    EXPECT_EQ(atlas::persistence::Package::load(packagePath).project().id(), "project");
}