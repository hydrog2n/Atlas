#include "atlas/persistence/package.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <map>
#include <random>
#include <sstream>

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

namespace {

atlas::domain::Project roadFixture(const std::string& fixtureName) {
    const auto fixturePath = std::filesystem::path(ATLAS_SOURCE_DIR) / "tests" / "fixtures" /
        "compatibility" / fixtureName;
    std::ifstream input(fixturePath, std::ios::binary);
    if (!input) throw std::runtime_error("Unable to open road compatibility fixture.");
    const auto source = std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
    return atlas::domain::Project::fromJson(source);
}

std::string readText(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("Unable to read test package file.");
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

std::string testContentHash(const std::string& content) {
    std::uint64_t hash = 1469598103934665603ULL;
    for (std::size_t index = 0; index < content.size(); ++index) {
        if (content[index] == '\r' && index + 1 < content.size() && content[index + 1] == '\n') continue;
        hash ^= static_cast<unsigned char>(content[index]);
        hash *= 1099511628211ULL;
    }
    std::ostringstream output;
    output << "fnv1a-64:" << std::hex << std::setfill('0') << std::setw(16) << hash;
    return output.str();
}

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

    const auto report = atlas::persistence::Package::migrate(packagePath, 2, true);

    EXPECT_EQ(report.fromSchema, 2);
    EXPECT_EQ(report.toSchema, 2);
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
    manifest["schemaVersion"] = 3;
    auto project = nlohmann::json::parse(readText(packagePath / "project.json"));
    project["schemaVersion"] = 3;
    const auto projectText = project.dump(2) + '\n';
    std::ofstream projectOutput(packagePath / "project.json", std::ios::trunc);
    projectOutput << projectText;
    projectOutput.close();
    manifest["authoritativeFiles"]["project.json"] = testContentHash(projectText);
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

// Verifies the supported schema-1-to-2 migration dry run reports success without changing package bytes or checkpoints.
TEST(Package, R020_004_MigrationDryRunDoesNotChangePackage) {
    TemporaryPackageDirectory temporary;
    const auto packagePath = temporary.path() / "project.atlas";
    atlas::persistence::SaveOptions schemaOneOptions;
    schemaOneOptions.schemaGeneration = 1;
    schemaOneOptions.checkpointCount = 2;
    const auto package = atlas::persistence::Package::fromProject(
        atlas::domain::Project::empty("project", "root-map"));
    package.save(packagePath, schemaOneOptions);
    package.save(packagePath, schemaOneOptions);
    package.save(packagePath, schemaOneOptions);
    const auto snapshot = [&temporary] {
        std::map<std::string, std::string> files;
        for (const auto& entry : std::filesystem::recursive_directory_iterator(temporary.path())) {
            if (!entry.is_regular_file()) continue;
            const auto relativePath = std::filesystem::relative(entry.path(), temporary.path()).generic_string();
            files.emplace(relativePath, readText(entry.path()));
        }
        return files;
    };
    const auto before = snapshot();
    ASSERT_FALSE(before.empty());
    ASSERT_TRUE(std::filesystem::is_directory(packagePath.parent_path() / "project.atlas.checkpoint-0"));
    ASSERT_TRUE(std::filesystem::is_directory(packagePath.parent_path() / "project.atlas.checkpoint-1"));

    const auto report = atlas::persistence::Package::migrate(packagePath, 2, true);

    EXPECT_TRUE(report.succeeded);
    EXPECT_TRUE(report.changed);
    EXPECT_TRUE(report.dryRun);
    EXPECT_EQ(report.fromSchema, 1);
    EXPECT_EQ(report.toSchema, 2);
    EXPECT_EQ(snapshot(), before);
}

// Verifies the migration API rejects unsupported schema downgrades with a failure report.
TEST(Package, R050_006_MigrationRejectsSchemaDowngrade) {
    TemporaryPackageDirectory temporary;
    const auto packagePath = temporary.path() / "project.atlas";
    atlas::persistence::Package::fromProject(
        atlas::domain::Project::empty("project", "root-map")).save(packagePath);
    const auto before = readText(packagePath / "project.json");

    const auto report = atlas::persistence::Package::migrate(packagePath, 1, true);

    EXPECT_FALSE(report.succeeded);
    EXPECT_FALSE(report.error.empty());
    EXPECT_EQ(readText(packagePath / "project.json"), before);
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

// Verifies schema-2 packages round-trip both retained road compatibility fixtures without semantic drift.
TEST(Package, R050_006_MinimalAndSplitRoadFixturesRoundTrip) {
    TemporaryPackageDirectory temporary;
    for (const auto& fixtureName : {"minimal-road.json", "split-road.json"}) {
        const auto project = roadFixture(fixtureName);
        const auto packagePath = temporary.path() / (std::string(fixtureName) + ".atlas");
        atlas::persistence::Package::fromProject(project).save(packagePath);
        const auto loaded = atlas::persistence::Package::load(packagePath);
        EXPECT_EQ(loaded.project().normalizedJson(), project.normalizedJson());
        const auto manifest = nlohmann::json::parse(readText(packagePath / "manifest.json"));
        EXPECT_EQ(manifest["schemaVersion"], 2);
        const auto projectJson = nlohmann::json::parse(readText(packagePath / "project.json"));
        EXPECT_TRUE(projectJson.contains("roadSplines"));
        EXPECT_TRUE(projectJson.contains("roadSegments"));
    }
}


// Verifies seeded variation in road identities, widths, metadata, and unknown fields survives package round trips.
TEST(Package, R050_006_SeededRoadPackageRoundTripsPreserveNormalizedSource) {
    TemporaryPackageDirectory temporary;
    std::mt19937 randomGenerator(0x6050u);
    std::uniform_real_distribution<double> chooseWidth(5.0, 14.0);
    std::uniform_real_distribution<double> chooseLength(20.0, 250.0);

    for (std::size_t sample = 0; sample < 24; ++sample) {

        const auto suffix = std::to_string(sample);
        const auto roadId = "road-" + suffix;
        const auto startAnchorId = roadId + "/start";
        const auto endAnchorId = roadId + "/end";
        const auto length = chooseLength(randomGenerator);
        atlas::domain::RoadSpline road{
            roadId, "root-map", nlohmann::json::array({nlohmann::json{
                {"id", roadId + "/line"}, {"kind", "line"},
                {"startControlPointId", roadId + "/a"}, {"endControlPointId", roadId + "/b"},
                {"start", {{"x", 0.0}, {"y", static_cast<double>(sample)}}},
                {"end", {{"x", length}, {"y", static_cast<double>(sample)}}}}}),
            "start-to-end", {}, nullptr, {{"seed", sample}}, nlohmann::json::array(), {
                {startAnchorId, 0.0, atlas::domain::AnchorAffinity::startLocked,
                    {0.0, roadId + "/line", 0.0, {0.0, static_cast<double>(sample)}}},
                {endAnchorId, length, atlas::domain::AnchorAffinity::endLocked,
                    {1.0, roadId + "/line", 1.0, {length, static_cast<double>(sample)}}}}};
        road.unknownFields = {{"futureRoadField", {{"seed", sample}}}};
        atlas::domain::RoadSegment segment{
            roadId + "/segment", "root-map", roadId, startAnchorId, endAnchorId,
            {{"kind", "uniform-placeholder"}, {"totalWidthMeters", chooseWidth(randomGenerator)},
                {"joinStyle", "round"}}, {{"seed", sample}}};
        segment.unknownFields = {{"futureSegmentField", {{"seed", sample}}}};
        const auto project = atlas::domain::Project::empty("project-" + suffix, "root-map")
            .withRoadSplineAndSegments(std::move(road), {std::move(segment)});
        const auto packagePath = temporary.path() / ("road-" + suffix + ".atlas");
        atlas::persistence::Package::fromProject(project).save(packagePath);

        const auto loaded = atlas::persistence::Package::load(packagePath).project();
        EXPECT_EQ(loaded.normalizedJson(), project.normalizedJson());
    }
}

// Verifies seeded mutations of road identity and ownership records are rejected before package source is accepted.
TEST(Package, R050_006_SeededMalformedRoadOwnershipInputsFailClosed) {
    const auto validSource = nlohmann::json::parse(roadFixture("minimal-road.json").normalizedJson());
    std::mt19937 randomGenerator(0x6F050u);
    std::uniform_int_distribution<std::size_t> chooseMutation(0, 4);
    for (std::size_t sample = 0; sample < 40; ++sample) {
        auto malformedSource = validSource;
        switch (chooseMutation(randomGenerator)) {
        case 0:
            malformedSource["roadSplines"][0]["id"] = "";
            break;
        case 1:
            malformedSource["roadSplines"][0]["mapId"] = "foreign-map";
            break;
        case 2:
            malformedSource["roadSegments"][0]["id"] = "";
            break;
        case 3:
            malformedSource["roadSegments"][0]["roadSplineId"] = "missing-road";
            break;
        default:
            malformedSource["roadSegments"][0]["mapId"] = "foreign-map";
            break;
        }
        EXPECT_THROW(atlas::domain::Project::fromJson(malformedSource.dump()), std::invalid_argument)
            << "seeded sample " << sample;
    }
}

// Verifies schema-1 saves cannot silently discard authoritative road records.
TEST(Package, R050_006_SchemaOneWriterRejectsRoadRecords) {
    TemporaryPackageDirectory temporary;
    const auto packagePath = temporary.path() / "road.atlas";
    atlas::persistence::SaveOptions schemaOneOptions;
    schemaOneOptions.schemaGeneration = 1;

    EXPECT_THROW(atlas::persistence::Package::fromProject(roadFixture("minimal-road.json"))
        .save(packagePath, schemaOneOptions), std::invalid_argument);
    EXPECT_FALSE(std::filesystem::exists(packagePath));
    EXPECT_FALSE(std::filesystem::exists(packagePath.parent_path() / "road.atlas.staging"));
}

// Verifies generation-1-to-2 migration is transactional, reported, and leaves a recoverable generation-1 checkpoint.
TEST(Package, R050_006_MigratesSchemaOneToTwoWithRecoverableCheckpoint) {
    TemporaryPackageDirectory temporary;
    const auto packagePath = temporary.path() / "project.atlas";
    auto sourceJson = nlohmann::json::parse(
        atlas::domain::Project::empty("project", "root-map").normalizedJson());
    sourceJson["extensions"]["future.example"] = {{"migration", "preserved"}};
    sourceJson["futureProjectField"] = {{"value", 17}};
    const auto source = atlas::domain::Project::fromJson(sourceJson.dump());
    atlas::persistence::SaveOptions generationOneOptions;
    generationOneOptions.schemaGeneration = 1;
    atlas::persistence::Package::fromProject(source).save(packagePath, generationOneOptions);
    atlas::persistence::Package::load(packagePath).save(packagePath);
    EXPECT_EQ(nlohmann::json::parse(readText(packagePath / "manifest.json"))["schemaVersion"], 1);
    const auto originalSource = readText(packagePath / "project.json");
    const auto dryRun = atlas::persistence::Package::migrate(packagePath, 2, true);
    EXPECT_TRUE(dryRun.succeeded);
    EXPECT_TRUE(dryRun.dryRun);
    EXPECT_EQ(readText(packagePath / "project.json"), originalSource);

    const auto report = atlas::persistence::Package::migrate(packagePath, 2);
    ASSERT_TRUE(report.succeeded) << report.error;
    EXPECT_TRUE(report.changed);
    EXPECT_EQ(report.fromSchema, 1);
    EXPECT_EQ(report.toSchema, 2);
    EXPECT_NE(report.toJson().find("checkpoint-0"), std::string::npos);
    const auto currentManifest = nlohmann::json::parse(readText(packagePath / "manifest.json"));
    EXPECT_EQ(currentManifest["schemaVersion"], 2);
    const auto currentProject = nlohmann::json::parse(readText(packagePath / "project.json"));
    EXPECT_TRUE(currentProject["roadSplines"].empty());
    EXPECT_TRUE(currentProject["roadSegments"].empty());
    const auto migrated = atlas::persistence::Package::load(packagePath).project();
    EXPECT_EQ(migrated.extensions()["future.example"]["migration"], "preserved");
    EXPECT_EQ(migrated.unknownFields()["futureProjectField"]["value"], 17);
    const auto checkpointManifest = nlohmann::json::parse(readText(
        packagePath.parent_path() / "project.atlas.checkpoint-0" / "manifest.json"));
    EXPECT_EQ(checkpointManifest["schemaVersion"], 1);
    EXPECT_EQ(atlas::persistence::Package::recover(packagePath).project().id(), "project");
}

// Verifies an injected post-backup migration failure restores the exact readable generation-1 package.
TEST(Package, R050_006_FailedSchemaMigrationRestoresLastReadablePackage) {
    TemporaryPackageDirectory temporary;
    const auto packagePath = temporary.path() / "project.atlas";
    const auto source = atlas::domain::Project::empty("project", "root-map");
    atlas::persistence::SaveOptions generationOneOptions;
    generationOneOptions.schemaGeneration = 1;
    atlas::persistence::Package::fromProject(source).save(packagePath, generationOneOptions);
    const auto originalProject = readText(packagePath / "project.json");
    const auto originalManifest = readText(packagePath / "manifest.json");
    atlas::persistence::SaveOptions failureOptions;
    failureOptions.failurePoint = atlas::persistence::SaveOptions::FailurePoint::afterBackup;

    const auto report = atlas::persistence::Package::migrate(packagePath, 2, false, failureOptions);
    EXPECT_FALSE(report.succeeded);
    EXPECT_FALSE(report.error.empty());
    EXPECT_EQ(readText(packagePath / "project.json"), originalProject);
    EXPECT_EQ(readText(packagePath / "manifest.json"), originalManifest);
    EXPECT_EQ(atlas::persistence::Package::load(packagePath).project().id(), "project");
    EXPECT_EQ(atlas::persistence::Package::recover(packagePath).project().id(), "project");
    EXPECT_FALSE(std::filesystem::exists(packagePath.parent_path() / "project.atlas.backup"));
}

// Verifies the package manifest and authoritative project record cannot disagree on schema generation.
TEST(Package, R050_006_ManifestAndProjectSchemaGenerationsMustMatch) {
    TemporaryPackageDirectory temporary;
    const auto packagePath = temporary.path() / "project.atlas";
    atlas::persistence::Package::fromProject(
        atlas::domain::Project::empty("project", "root-map")).save(packagePath);
    auto manifest = nlohmann::json::parse(readText(packagePath / "manifest.json"));
    manifest["schemaVersion"] = 1;
    std::ofstream output(packagePath / "manifest.json", std::ios::trunc);
    output << manifest.dump(2) << '\n';
    output.close();

    EXPECT_THROW(atlas::persistence::Package::load(packagePath), std::invalid_argument);
}

// Verifies the manifest cannot omit a hash for any required project or root-map source file.
TEST(Package, R050_006_ManifestRequiresEveryCoreAuthoritativeHash) {
    TemporaryPackageDirectory temporary;
    const std::vector<std::string> requiredFiles{
        "project.json", "maps/root-map/map.json", "maps/root-map/objects.json", "maps/root-map/network.json"};
    for (std::size_t index = 0; index < requiredFiles.size(); ++index) {
        const auto packagePath = temporary.path() / ("missing-hash-" + std::to_string(index) + ".atlas");
        atlas::persistence::Package::fromProject(
            atlas::domain::Project::empty("project", "root-map")).save(packagePath);
        auto manifest = nlohmann::json::parse(readText(packagePath / "manifest.json"));
        manifest["authoritativeFiles"].erase(requiredFiles[index]);
        std::ofstream output(packagePath / "manifest.json", std::ios::trunc);
        output << manifest.dump(2) << '\n';
        output.close();

        EXPECT_THROW(atlas::persistence::Package::load(packagePath), std::invalid_argument)
            << "missing hash for " << requiredFiles[index];
    }
}

// Verifies manifest hash paths cannot escape the package directory through a rooted path.
TEST(Package, R050_006_ManifestRejectsRootedAuthoritativeHashPath) {
    TemporaryPackageDirectory temporary;
    const auto packagePath = temporary.path() / "rooted-hash.atlas";
    atlas::persistence::Package::fromProject(
        atlas::domain::Project::empty("project", "root-map")).save(packagePath);
    auto manifest = nlohmann::json::parse(readText(packagePath / "manifest.json"));
    manifest["authoritativeFiles"]["C:/outside.json"] = "irrelevant";
    std::ofstream output(packagePath / "manifest.json", std::ios::trunc);
    output << manifest.dump(2) << '\n';
    output.close();

    EXPECT_THROW(atlas::persistence::Package::load(packagePath), std::invalid_argument);
}

// Verifies a generation-1 package cannot contain authoritative road records.
TEST(Package, R050_006_SchemaOnePackageRejectsRoadArrays) {
    TemporaryPackageDirectory temporary;
    const auto packagePath = temporary.path() / "project.atlas";
    const auto project = roadFixture("minimal-road.json");
    auto projectRecord = nlohmann::json::parse(project.normalizedJson());
    projectRecord["schemaVersion"] = 1;
    std::filesystem::create_directories(packagePath);
    std::ofstream projectFile(packagePath / "project.json", std::ios::trunc);
    projectFile << projectRecord.dump(2) << '\n';
    projectFile.close();
    std::ofstream manifestFile(packagePath / "manifest.json", std::ios::trunc);
    manifestFile << R"({"format":"atlas.project","schemaVersion":1,"projectId":"fixture-minimal-road","authoritativeFiles":{}})";
    manifestFile.close();

    EXPECT_THROW(atlas::persistence::Package::load(packagePath), std::invalid_argument);
}

// Verifies malformed migration inputs return a failure report instead of escaping before transactional handling.
TEST(Package, R050_006_MalformedMigrationSourceReturnsReport) {
    TemporaryPackageDirectory temporary;
    const auto packagePath = temporary.path() / "broken.atlas";
    std::filesystem::create_directories(packagePath);
    std::ofstream manifest(packagePath / "manifest.json", std::ios::trunc);
    manifest << "{";
    manifest.close();

    const auto report = atlas::persistence::Package::migrate(packagePath, 2);
    EXPECT_FALSE(report.succeeded);
    EXPECT_FALSE(report.error.empty());
    EXPECT_FALSE(report.changed);
}