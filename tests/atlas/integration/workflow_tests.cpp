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
