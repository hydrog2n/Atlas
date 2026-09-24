#include "atlas/ui/canvas_widget.hpp"

#include <QApplication>
#include <QKeyEvent>

#include <gtest/gtest.h>

namespace {

class CanvasWidgetTest : public ::testing::Test {
protected:
    void SetUp() override {
        if (QApplication::instance() == nullptr) {
            static int argumentCount = 1;
            static char applicationName[] = "atlas_ui_tests";
            static char* arguments[] = {applicationName, nullptr};
            application_ = std::make_unique<QApplication>(argumentCount, arguments);
        }
    }

    static std::unique_ptr<QApplication> application_;
};

std::unique_ptr<QApplication> CanvasWidgetTest::application_;

} // namespace

// Verifies that the canvas exposes an accessible name and keyboard focus policy.
TEST_F(CanvasWidgetTest, CanvasHasAccessibleNameAndStrongFocus) {
    atlas::ui::CanvasWidget canvas;
    canvas.setAccessibleName("Atlas canvas");

    EXPECT_EQ(canvas.accessibleName(), "Atlas canvas");
    EXPECT_EQ(canvas.focusPolicy(), Qt::StrongFocus);
}

// Verifies that arrow-key navigation changes the camera without requiring mouse input.
TEST_F(CanvasWidgetTest, ArrowKeysPanTheCamera) {
    atlas::ui::CanvasWidget canvas;
    const auto before = canvas.camera().center();
    QKeyEvent event(QEvent::KeyPress, Qt::Key_Right, Qt::NoModifier);

    QApplication::sendEvent(&canvas, &event);

    EXPECT_GT(canvas.camera().center().x, before.x);
    EXPECT_TRUE(event.isAccepted());
}

// Verifies that keyboard zoom changes the camera while preserving the canvas interaction path.
TEST_F(CanvasWidgetTest, PlusKeyZoomsTheCamera) {
    atlas::ui::CanvasWidget canvas;
    const auto before = canvas.camera().zoom();
    QKeyEvent event(QEvent::KeyPress, Qt::Key_Plus, Qt::NoModifier);

    QApplication::sendEvent(&canvas, &event);

    EXPECT_GT(canvas.camera().zoom(), before);
    EXPECT_TRUE(event.isAccepted());
}

// Verifies that Tab cycles selectable objects and Escape clears the selection.
TEST_F(CanvasWidgetTest, TabCyclesAndEscapeClearsSelection) {
    auto project = atlas::domain::Project::empty("project", "root-map");
    auto map = project.rootMap();
    map.objects.push_back(atlas::domain::MapObject::create("first", "core.Point", {{"x", 0.0}, {"y", 0.0}}));
    map.objects.push_back(atlas::domain::MapObject::create("second", "core.Point", {{"x", 1.0}, {"y", 1.0}}));
    project = project.withRootMap(map);
    atlas::ui::CanvasWidget canvas;
    canvas.setProject(project);

    QKeyEvent tabEvent(QEvent::KeyPress, Qt::Key_Tab, Qt::NoModifier);
    QApplication::sendEvent(&canvas, &tabEvent);
    ASSERT_FALSE(canvas.selection().primaryId().empty());

    QKeyEvent escapeEvent(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
    QApplication::sendEvent(&canvas, &escapeEvent);
    EXPECT_TRUE(canvas.selection().primaryId().empty());
}
