#include "atlas/persistence/package.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>

// Persistence tests cover canonical package layout, defensive loading,
// atomic saves, recovery, migration, limits, and compatibility fixtures.
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

// Verifies that saving and loading preserves project data and the manifest.
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

// Verifies that a manifest with the wrong project identity is rejected.
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

// Verifies that replacement saves use staging and remove staging afterward.
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

// Verifies that package saves reject paths that traverse outside the target.
TEST(Package, R020_002_SaveRejectsTraversalPath) {
    const auto project = atlas::domain::Project::empty("project", "root-map");

    EXPECT_THROW(
        atlas::persistence::Package::fromProject(project).save(
            std::filesystem::temp_directory_path() / "atlas" / ".." / "outside.atlas"),
        std::invalid_argument);
}

// Verifies that saves create the required canonical package directories.
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

// Verifies that changed authoritative content fails manifest validation.
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

// Verifies that recovery loads the newest valid checkpoint.
TEST(Package, R020_003_RecoveryLoadsLatestCheckpoint) {
    TemporaryPackageDirectory temporary;
    const auto packagePath = temporary.path() / "project.atlas";
    atlas::persistence::Package::fromProject(
        atlas::domain::Project::empty("first", "root-map")).save(packagePath);
    atlas::persistence::Package::fromProject(
        atlas::domain::Project::empty("second", "root-map")).save(packagePath);

    EXPECT_EQ(atlas::persistence::Package::recover(packagePath).project().id(), "first");
}

// Verifies that migration reports an unchanged current schema accurately.
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

// Verifies that unsafe root-map directory names are rejected.
TEST(Package, R020_002_SaveRejectsUnsafeRootMapDirectoryName) {
    const auto project = atlas::domain::Project::empty("project", "bad/map");

    EXPECT_THROW(
        atlas::persistence::Package::fromProject(project).save(
            std::filesystem::temp_directory_path() / "project.atlas"),
        std::invalid_argument);
}

// Verifies that injected save failures preserve the previous package.
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

// Verifies that schema-zero packages migrate to the current schema.
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

// Verifies that newer schemas can be inspected but not rewritten.
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
    atlas::persistence::LoadOptions loadOptions;
    loadOptions.allowNewerSchemaReadOnly = true;
    const auto newer = atlas::persistence::Package::load(packagePath, loadOptions);
    EXPECT_THROW(newer.save(packagePath), std::runtime_error);
}

// Verifies that filesystem failure simulations preserve the previous package.
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

// Verifies that failed migrations produce a report and preserve the package.
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

// Verifies that recovery rejects a corrupt checkpoint.
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

// Verifies that malformed input is rejected without creating authoritative data.
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

// Verifies that packages missing authoritative files are rejected.
TEST(Package, R020_002_MissingAuthoritativeFileIsRejected) {
    TemporaryPackageDirectory temporary;
    const auto packagePath = temporary.path() / "project.atlas";
    atlas::persistence::Package::fromProject(
        atlas::domain::Project::empty("project", "root-map")).save(packagePath);
    std::filesystem::remove(packagePath / "project.json");

    EXPECT_THROW(atlas::persistence::Package::load(packagePath), std::invalid_argument);
}

// Verifies that oversized JSON input is rejected by package limits.
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

// Verifies that repeated save and load cycles remain deterministic.
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

// Verifies that the retained compatibility fixture resaves without semantic drift.
TEST(Package, R020_005_RetainedCompatibilityFixtureResavesWithoutDrift) {
    const auto sourceRoot =
#ifdef ATLAS_SOURCE_DIR
        std::filesystem::path(ATLAS_SOURCE_DIR);
#else
        std::filesystem::path(__FILE__).parent_path().parent_path().parent_path().parent_path();
#endif
    const auto fixture = sourceRoot /
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

// Verifies that the manifest lists files that exist in the package.
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

// Verifies that checkpoint rotation honors the configured retention count.
TEST(Package, R020_003_CheckpointRotationHonorsConfiguredCount) {
    TemporaryPackageDirectory temporary;
    const auto packagePath = temporary.path() / "project.atlas";
    atlas::persistence::SaveOptions saveOptions;
    saveOptions.checkpointCount = 2;
    atlas::persistence::Package::fromProject(
        atlas::domain::Project::empty("one", "root-map")).save(packagePath, saveOptions);
    atlas::persistence::Package::fromProject(
        atlas::domain::Project::empty("two", "root-map")).save(packagePath, saveOptions);
    atlas::persistence::Package::fromProject(
        atlas::domain::Project::empty("three", "root-map")).save(packagePath, saveOptions);

    EXPECT_TRUE(std::filesystem::is_directory(packagePath.parent_path() / "project.atlas.checkpoint-0"));
    EXPECT_TRUE(std::filesystem::is_directory(packagePath.parent_path() / "project.atlas.checkpoint-1"));
    EXPECT_FALSE(std::filesystem::exists(packagePath.parent_path() / "project.atlas.checkpoint-2"));
}

// Verifies that migration dry runs leave the package unchanged.
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

// Verifies that disposable cache data is not required for loading.
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

// Verifies that generic objects, layers, and levels survive package save/load.
TEST(Package, R040_003_GenericObjectsRoundTripThroughPackage) {
    TemporaryPackageDirectory temporary;
    const auto packagePath = temporary.path() / "project.atlas";
    const auto base = atlas::domain::Project::empty("project", "root-map");
    const auto object = atlas::domain::MapObject::create(
        "point-1", "core.Point", {{"x", 12.0}, {"y", 8.0}})
        .withDisplayLayer("annotations")
        .withSpatialLevel("upper");
    auto map = base.rootMap();
    map.displayLayers.push_back({"annotations", "Annotations", true, false, 1.0});
    map.spatialLevels.push_back({"upper", "Upper"});
    const auto project = base.withRootMap(map.withObject(object));

    atlas::persistence::Package::fromProject(project).save(packagePath);
    const auto loaded = atlas::persistence::Package::load(packagePath).project();

    ASSERT_NE(loaded.rootMap().findObject("point-1"), nullptr);
    EXPECT_EQ(loaded.rootMap().findObject("point-1")->type(), "core.Point");
    EXPECT_EQ(loaded.rootMap().findObject("point-1")->primaryDisplayLayerId(), "annotations");
    EXPECT_EQ(*loaded.rootMap().findObject("point-1")->spatialLevelId(), "upper");
    EXPECT_TRUE(std::filesystem::exists(packagePath / "maps" / "root-map" / "objects.json"));
}

// Verifies that unknown generic object types remain readable and preserved.
TEST(Package, R040_003_UnknownGenericObjectTypeIsPreserved) {
    const auto project = atlas::domain::Project::fromJson(R"({
        "id": "project",
        "schemaVersion": 1,
        "units": "m",
        "maps": [{
            "id": "root-map",
            "objects": [{"id": "future-1", "type": "future.Custom", "geometry": {"value": 7}}]
        }]
    })");

    ASSERT_NE(project.rootMap().findObject("future-1"), nullptr);
    EXPECT_EQ(project.rootMap().findObject("future-1")->type(), "future.Custom");
    EXPECT_NE(project.normalizedJson().find("future.Custom"), std::string::npos);
}

// Verifies that every supported generic object family survives package persistence.
TEST(Package, R040_003_AllGenericObjectFamiliesRoundTrip) {
    TemporaryPackageDirectory temporary;
    const auto packagePath = temporary.path() / "project.atlas";
    const auto base = atlas::domain::Project::empty("project", "root-map");
    auto map = base.rootMap();
    const std::vector<std::string> types{
        "core.Point", "core.Polyline", "core.Polygon", "core.Rectangle",
        "core.Circle", "core.Text", "core.ReferenceImage", "core.Guide"};
    for (std::size_t index = 0; index < types.size(); ++index) {
        map.objects.push_back(atlas::domain::MapObject::create(
            "object-" + std::to_string(index), types[index]));
    }

    atlas::persistence::Package::fromProject(base.withRootMap(map)).save(packagePath);
    const auto loaded = atlas::persistence::Package::load(packagePath).project();

    ASSERT_EQ(loaded.rootMap().objects.size(), types.size());
    for (std::size_t index = 0; index < types.size(); ++index) {
        EXPECT_EQ(loaded.rootMap().findObject("object-" + std::to_string(index))->type(), types[index]);
    }
}

// Verifies that tampering with the authoritative object record is detected by its manifest hash.
TEST(Package, R040_003_TamperedObjectRecordIsRejected) {
    TemporaryPackageDirectory temporary;
    const auto packagePath = temporary.path() / "project.atlas";
    const auto project = atlas::domain::Project::empty("project", "root-map");
    atlas::persistence::Package::fromProject(project).save(packagePath);

    std::ofstream objects(packagePath / "maps" / "root-map" / "objects.json", std::ios::trunc);
    objects << "[{\"id\":\"tampered\",\"type\":\"core.Point\"}]\n";
    objects.close();

    EXPECT_THROW(atlas::persistence::Package::load(packagePath), std::invalid_argument);
}

// Verifies that reference-image paths cannot escape the project package root.
TEST(Package, R040_003_UnsafeReferenceImagePathIsRejected) {
    EXPECT_THROW(atlas::domain::MapObject::fromJson({
        {"id", "reference"}, {"type", "core.ReferenceImage"},
        {"geometry", {{"path", "../outside.png"}}}}), std::invalid_argument);
}