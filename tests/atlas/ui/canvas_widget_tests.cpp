#include "atlas/ui/canvas_widget.hpp"
#include "atlas/application/command.hpp"

#include <QApplication>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPushButton>
#include <QHBoxLayout>
#include <QWidget>

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

// Verifies keyboard-only road drafting emits one canonical polyline source record.
TEST_F(CanvasWidgetTest, RoadToolCreatesPolylineDraftThroughKeyboard) {
    atlas::ui::CanvasWidget canvas;
    canvas.setTool(atlas::ui::CanvasWidget::Tool::road);
    std::optional<atlas::domain::RoadSpline> createdRoad;
    canvas.setRoadCreatedHandler([&](atlas::domain::RoadSpline road) {
        createdRoad = std::move(road);
    });

    QKeyEvent addStart(QEvent::KeyPress, Qt::Key_Space, Qt::NoModifier);
    QApplication::sendEvent(&canvas, &addStart);
    QKeyEvent moveCursor(QEvent::KeyPress, Qt::Key_Right, Qt::NoModifier);
    QApplication::sendEvent(&canvas, &moveCursor);
    QKeyEvent addEnd(QEvent::KeyPress, Qt::Key_Space, Qt::NoModifier);
    QApplication::sendEvent(&canvas, &addEnd);
    QKeyEvent finish(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
    QApplication::sendEvent(&canvas, &finish);

    ASSERT_TRUE(createdRoad.has_value());
    EXPECT_EQ(createdRoad->mapId, "root-map");
    ASSERT_EQ(createdRoad->primitives.size(), 1U);
    EXPECT_EQ(createdRoad->primitives[0]["kind"], "line");
    EXPECT_EQ(createdRoad->primitives[0]["start"]["x"], 0.0);
    EXPECT_EQ(createdRoad->primitives[0]["end"]["x"], 1.0);
}

// Verifies Escape discards an unfinished road draft before it reaches the command layer.
TEST_F(CanvasWidgetTest, EscapeCancelsRoadDraftWithoutSubmittingSource) {
    atlas::ui::CanvasWidget canvas;
    canvas.setTool(atlas::ui::CanvasWidget::Tool::road);
    bool submitted = false;
    canvas.setRoadCreatedHandler([&](atlas::domain::RoadSpline) { submitted = true; });

    QKeyEvent addPoint(QEvent::KeyPress, Qt::Key_Space, Qt::NoModifier);
    QApplication::sendEvent(&canvas, &addPoint);
    QKeyEvent cancel(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
    QApplication::sendEvent(&canvas, &cancel);
    QKeyEvent finish(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
    QApplication::sendEvent(&canvas, &finish);

    EXPECT_FALSE(submitted);
}

// Verifies the Bezier and fixed-radius arc context modes emit the canonical curve primitive records.
TEST_F(CanvasWidgetTest, RoadToolCreatesBezierAndCircularArcDrafts) {
    atlas::ui::CanvasWidget bezierCanvas;
    bezierCanvas.setTool(atlas::ui::CanvasWidget::Tool::road);
    bezierCanvas.setRoadDrawingMode(atlas::ui::CanvasWidget::RoadDrawingMode::bezier);
    std::optional<atlas::domain::RoadSpline> bezierRoad;
    bezierCanvas.setRoadCreatedHandler([&](atlas::domain::RoadSpline road) {
        bezierRoad = std::move(road);
    });
    for (const auto keyCode : {Qt::Key_Space, Qt::Key_Right, Qt::Key_Space, Qt::Key_Up,
             Qt::Key_Space, Qt::Key_Right, Qt::Key_Space, Qt::Key_Return}) {
        QKeyEvent event(QEvent::KeyPress, keyCode, Qt::NoModifier);
        QApplication::sendEvent(&bezierCanvas, &event);
    }
    ASSERT_TRUE(bezierRoad.has_value());
    EXPECT_EQ(bezierRoad->primitives.front()["kind"], "cubic-bezier");
    EXPECT_EQ(bezierRoad->primitives.front()["controlPointIds"].size(), 4U);

    atlas::ui::CanvasWidget arcCanvas;
    arcCanvas.setTool(atlas::ui::CanvasWidget::Tool::road);
    arcCanvas.setRoadDrawingMode(atlas::ui::CanvasWidget::RoadDrawingMode::fixedRadiusArc);
    arcCanvas.setArcRadiusMeters(2.0);
    std::optional<atlas::domain::RoadSpline> arcRoad;
    arcCanvas.setRoadCreatedHandler([&](atlas::domain::RoadSpline road) { arcRoad = std::move(road); });
    QKeyEvent addStart(QEvent::KeyPress, Qt::Key_Space, Qt::NoModifier);
    QApplication::sendEvent(&arcCanvas, &addStart);
    for (int step = 0; step < 4; ++step) {
        QKeyEvent move(QEvent::KeyPress, Qt::Key_Right, Qt::NoModifier);
        QApplication::sendEvent(&arcCanvas, &move);
    }
    QKeyEvent addEnd(QEvent::KeyPress, Qt::Key_Space, Qt::NoModifier);
    QApplication::sendEvent(&arcCanvas, &addEnd);
    QKeyEvent finish(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
    QApplication::sendEvent(&arcCanvas, &finish);
    ASSERT_TRUE(arcRoad.has_value());
    EXPECT_EQ(arcRoad->primitives.front()["kind"], "circular-arc");
    EXPECT_EQ(arcRoad->primitives.front()["radius"], 2.0);
}

// Verifies the Road tool uses canonical grid snapping by default and can preserve precise free coordinates.
TEST_F(CanvasWidgetTest, RoadDraftGridSnapCanBeToggled) {
    const auto createAtOffset = [](bool snapToGrid) {
        atlas::ui::CanvasWidget canvas;
        canvas.setTool(atlas::ui::CanvasWidget::Tool::road);
        canvas.setRoadSnapToGrid(snapToGrid);
        std::optional<atlas::domain::RoadSpline> created;
        canvas.setRoadCreatedHandler([&](atlas::domain::RoadSpline road) { created = std::move(road); });
        QKeyEvent addStart(QEvent::KeyPress, Qt::Key_Space, Qt::NoModifier);
        QApplication::sendEvent(&canvas, &addStart);
        for (int step = 0; step < 14; ++step) {
            QKeyEvent move(QEvent::KeyPress, Qt::Key_Right, Qt::ShiftModifier);
            QApplication::sendEvent(&canvas, &move);
        }
        QKeyEvent addEnd(QEvent::KeyPress, Qt::Key_Space, Qt::NoModifier);
        QApplication::sendEvent(&canvas, &addEnd);
        QKeyEvent finish(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
        QApplication::sendEvent(&canvas, &finish);
        return created->primitives.front()["end"]["x"].get<double>();
    };

    EXPECT_DOUBLE_EQ(createAtOffset(true), 1.0);
    EXPECT_DOUBLE_EQ(createAtOffset(false), 1.4);
}

// Verifies Tab and Shift+Tab make overlapping road owners reachable in stable-ID order.
TEST_F(CanvasWidgetTest, TabCyclesOverlappingRoadSplinesDeterministically) {
    const auto makeRoad = [](std::string roadId) {
        return atlas::domain::RoadSpline{
            std::move(roadId), "root-map",
            nlohmann::json::array({nlohmann::json{
                {"id", "line"}, {"kind", "line"}, {"startControlPointId", "start"},
                {"endControlPointId", "end"}, {"start", {{"x", 0.0}, {"y", 0.0}}},
                {"end", {{"x", 4.0}, {"y", 0.0}}}}}),
            "start-to-end", {}, nullptr, nlohmann::json::object(), nlohmann::json::array(), {}};
    };
    atlas::application::CommandProcessor processor({
        0, atlas::domain::Project::empty("project", "root-map"), {}});
    processor.commit(atlas::application::CreateRoadSplineCommand(makeRoad("road-b"), 0));
    processor.commit(atlas::application::CreateRoadSplineCommand(
        makeRoad("road-a"), processor.current().number));
    atlas::ui::CanvasWidget canvas;
    canvas.setProject(processor.current().project);
    QMouseEvent click(QEvent::MouseButtonPress, QPointF(0.5, 0.5),
        Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&canvas, &click);
    EXPECT_EQ(canvas.selection().primaryId(), "road-a");

    QKeyEvent next(QEvent::KeyPress, Qt::Key_Tab, Qt::NoModifier);
    QApplication::sendEvent(&canvas, &next);
    EXPECT_EQ(canvas.selection().primaryId(), "road-b");
    QKeyEvent previous(QEvent::KeyPress, Qt::Key_Tab, Qt::ShiftModifier);
    QApplication::sendEvent(&canvas, &previous);
    EXPECT_EQ(canvas.selection().primaryId(), "road-a");
}

// Verifies the status callback reports live world coordinates for pointer movement.
TEST_F(CanvasWidgetTest, PointerMovementReportsWorldCoordinates) {
    atlas::ui::CanvasWidget canvas;
    QString status;
    canvas.setStatusHandler([&](QString message) { status = std::move(message); });
    QMouseEvent move(QEvent::MouseMove, QPointF(0.5, 0.5), Qt::NoButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(&canvas, &move);

    EXPECT_NE(status.indexOf(QStringLiteral("Coordinates: 0.000 m, 0.000 m")), -1);
}

// Verifies Ctrl+Tab exits canvas overlap cycling and returns focus to the next accessible control.
TEST_F(CanvasWidgetTest, ControlTabLeavesCanvasForNextControl) {
    QWidget container;
    auto* layout = new QHBoxLayout(&container);
    auto* canvas = new atlas::ui::CanvasWidget(&container);
    auto* nextControl = new QPushButton(QStringLiteral("Next control"), &container);
    layout->addWidget(canvas);
    layout->addWidget(nextControl);
    container.show();
    canvas->setFocus();
    QApplication::processEvents();
    QKeyEvent event(QEvent::KeyPress, Qt::Key_Tab, Qt::ControlModifier);
    QApplication::sendEvent(canvas, &event);

    EXPECT_EQ(QApplication::focusWidget(), nextControl);
}
