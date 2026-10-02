#include "atlas/application/command.hpp"
#include "atlas/persistence/package.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <utility>

namespace {

class TemporaryWorkflowPackage {
public:
    TemporaryWorkflowPackage()
        : path_(std::filesystem::temp_directory_path() /
            ("atlas-workflow-" + std::to_string(
                std::chrono::steady_clock::now().time_since_epoch().count()))) {}

    ~TemporaryWorkflowPackage() {
        std::error_code error;
        std::filesystem::remove_all(path_, error);
    }

    const std::filesystem::path& path() const noexcept { return path_; }

private:
    std::filesystem::path path_;
};

} // namespace

// Verifies that a generic object can be created, edited, saved, reopened, and undone.
TEST(GenericWorkflow, CreateEditSaveReopenAndUndo) {
    TemporaryWorkflowPackage temporary;
    const auto packagePath = temporary.path() / "project.atlas";
    atlas::application::CommandProcessor processor({
        0, atlas::domain::Project::empty("project", "root-map"), {}});

    const auto object = atlas::domain::MapObject::create(
        "point-1", "core.Point", {{"x", 1.0}, {"y", 2.0}})
        .withDisplayLayer("annotations");
    auto map = processor.current().project.rootMap();
    map.displayLayers.push_back({"annotations", "Annotations", true, false, 1.0});
    processor = atlas::application::CommandProcessor({
        0, processor.current().project.withRootMap(std::move(map)), {}});
    processor.commit(atlas::application::CreateMapObjectCommand(object, 0));
    processor.commit(atlas::application::EditMapObjectCommand(
        object.withGeometry({{"x", 3.0}, {"y", 4.0}}), processor.current().number));

    atlas::persistence::Package::fromProject(processor.current().project).save(packagePath);
    const auto reopened = atlas::persistence::Package::load(packagePath).project();
    ASSERT_NE(reopened.rootMap().findObject("point-1"), nullptr);
    EXPECT_DOUBLE_EQ(reopened.rootMap().findObject("point-1")->geometry()["x"], 3.0);
    EXPECT_EQ(reopened.rootMap().findObject("point-1")->primaryDisplayLayerId(), "annotations");

    ASSERT_TRUE(processor.undo());
    EXPECT_DOUBLE_EQ(processor.current().project.rootMap().findObject("point-1")->geometry()["x"], 1.0);
}

// Verifies the v0.5 road workflow remaps anchors, segments, persists schema-2 source, and reverses exactly twice.
TEST(RoadWorkflow, CreateEditRemapSplitMergeSaveReopenReverseTwice) {
    TemporaryWorkflowPackage temporary;
    const auto packagePath = temporary.path() / "road-project.atlas";
    atlas::application::CommandProcessor processor({
        0, atlas::domain::Project::empty("road-project", "root-map"), {}});
    atlas::domain::RoadSpline road{
        "road-1", "root-map", nlohmann::json::array({nlohmann::json{
            {"id", "line"}, {"kind", "line"},
            {"startControlPointId", "a"}, {"endControlPointId", "b"},
            {"start", {{"x", 0.0}, {"y", 0.0}}},
            {"end", {{"x", 10.0}, {"y", 0.0}}}}}),
        "start-to-end", {}, nullptr, nlohmann::json::object(), nlohmann::json::array()};
    processor.commit(atlas::application::CreateRoadSplineCommand(road, processor.current().number));

    auto editedRoad = processor.current().project.roadSplines().front();
    editedRoad.stationAnchors.push_back({
        "anchor-interior", 8.0, atlas::domain::AnchorAffinity::geometryLocked,
        {0.8, "line", 0.8, {8.0, 0.0}}});
    processor.commit(atlas::application::EditRoadSplineCommand(editedRoad, processor.current().number));

    editedRoad = processor.current().project.roadSplines().front();
    editedRoad.primitives[0]["end"]["x"] = 8.0;
    editedRoad.primitives[0]["end"]["y"] = 6.0;
    processor.commit(atlas::application::EditRoadSplineCommand(editedRoad, processor.current().number));
    const auto& remappedAnchor = processor.current().project.roadSplines().front().stationAnchors.back();
    EXPECT_DOUBLE_EQ(remappedAnchor.resolvedStation, 8.0);
    EXPECT_DOUBLE_EQ(remappedAnchor.remapSignature.worldPosition.x, 6.4);
    EXPECT_DOUBLE_EQ(remappedAnchor.remapSignature.worldPosition.y, 4.8);

    auto& currentRoad = processor.current().project.roadSplines().front();
    const auto sourceSegmentId = currentRoad.segmentIds.front();
    processor.commit(atlas::application::SplitRoadSegmentCommand(
        "road-1", sourceSegmentId, 4.0, processor.current().number));
    const auto splitRoad = processor.current().project.roadSplines().front();
    processor.commit(atlas::application::MergeRoadSegmentsCommand(
        "road-1", splitRoad.segmentIds[0], splitRoad.segmentIds[1], processor.current().number));
    const auto beforeSave = processor.current().normalizedSource();

    atlas::persistence::Package::fromProject(processor.current().project).save(packagePath);
    const auto reopened = atlas::persistence::Package::load(packagePath).project();
    EXPECT_EQ(reopened.normalizedJson(), beforeSave);
    const auto manifest = nlohmann::json::parse(atlas::persistence::Package::fromProject(reopened).manifestJson());
    EXPECT_EQ(manifest["schemaVersion"], 2);

    const auto beforeReverse = reopened.normalizedJson();
    processor = atlas::application::CommandProcessor({processor.current().number, reopened, {}});
    processor.commit(atlas::application::ReverseRoadSplineCommand("road-1", processor.current().number));
    processor.commit(atlas::application::ReverseRoadSplineCommand("road-1", processor.current().number));
    EXPECT_EQ(processor.current().normalizedSource(), beforeReverse);
}
