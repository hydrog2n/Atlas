#include "atlas/ui/canvas_widget.hpp"
#include "atlas/application/command.hpp"

#include <QApplication>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPushButton>
#include <QHBoxLayout>
#include <QWidget>

#include <utility>

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

// Verifies point hit targets preserve a ten-pixel acquisition radius across widely different zoom levels.
TEST_F(CanvasWidgetTest, GenericPointHitRadiusRemainsScreenSizedAcrossZoom) {
    auto project = atlas::domain::Project::empty("project", "root-map");
    project = project.withRootMap(project.rootMap().withObject(atlas::domain::MapObject::create(
        "zoom-point", "core.Point", {{"x", 0.0}, {"y", 0.0}})));
    atlas::ui::CanvasWidget canvas;
    canvas.resize(640, 480);
    canvas.setProject(project);
    canvas.show();
    QApplication::processEvents();
    for (const auto [key, count] : {std::pair{Qt::Key_Minus, 16}, std::pair{Qt::Key_Plus, 32}}) {
        for (int index = 0; index < count; ++index) {
            QKeyEvent zoom(QEvent::KeyPress, key, Qt::NoModifier);
            QApplication::sendEvent(&canvas, &zoom);
        }
        canvas.selectId({});
        const auto point = canvas.camera().worldToScreen({0.0, 0.0});
        QMouseEvent click(QEvent::MouseButtonPress, QPointF(point.x + 9.0, point.y),
            Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(&canvas, &click);
        EXPECT_EQ(canvas.selection().primaryId(), "zoom-point");
    }
}

// Verifies the shared canvas interaction state classifies hover, selection, manipulation, drafting, and measuring.
TEST_F(CanvasWidgetTest, InteractionStateTracksCanvasGrammar) {
    auto project = atlas::domain::Project::empty("project", "root-map");
    project = project.withRootMap(project.rootMap().withObject(atlas::domain::MapObject::create(
        "state-point", "core.Point", {{"x", 0.0}, {"y", 0.0}})));
    atlas::domain::RoadSpline road;
    road.id = "state-road";
    road.mapId = "root-map";
    road.primitives = nlohmann::json::array({nlohmann::json{
        {"id", "line"}, {"kind", "line"}, {"startControlPointId", "a"}, {"endControlPointId", "b"},
        {"start", {{"x", 0.0}, {"y", 0.0}}}, {"end", {{"x", 100.0}, {"y", 0.0}}}}});
    project = project.withRoadSpline(std::move(road));
    atlas::ui::CanvasWidget canvas;
    canvas.resize(640, 480);
    canvas.setProject(std::move(project));
    canvas.show();
    QApplication::processEvents();
    EXPECT_EQ(canvas.interactionState(), atlas::ui::CanvasWidget::InteractionState::idle);

    const auto point = canvas.camera().worldToScreen({0.0, 0.0});
    QMouseEvent hover(QEvent::MouseMove, QPointF(point.x + 5.0, point.y),
        Qt::NoButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(&canvas, &hover);
    EXPECT_EQ(canvas.interactionState(), atlas::ui::CanvasWidget::InteractionState::hovering);
    QMouseEvent press(QEvent::MouseButtonPress, QPointF(point.x, point.y),
        Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&canvas, &press);
    EXPECT_EQ(canvas.interactionState(), atlas::ui::CanvasWidget::InteractionState::manipulating);
    QMouseEvent release(QEvent::MouseButtonRelease, QPointF(point.x, point.y),
        Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(&canvas, &release);
    EXPECT_EQ(canvas.interactionState(), atlas::ui::CanvasWidget::InteractionState::selected);

    canvas.selectId({});
    canvas.setTool(atlas::ui::CanvasWidget::Tool::road);
    QMouseEvent draftPress(QEvent::MouseButtonPress, QPointF(point.x, point.y),
        Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&canvas, &draftPress);
    EXPECT_EQ(canvas.interactionState(), atlas::ui::CanvasWidget::InteractionState::drafting);
    QKeyEvent cancelDraft(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
    QApplication::sendEvent(&canvas, &cancelDraft);
    EXPECT_EQ(canvas.interactionState(), atlas::ui::CanvasWidget::InteractionState::idle);

    canvas.setTool(atlas::ui::CanvasWidget::Tool::measure);
    const auto measurePoint = canvas.camera().worldToScreen({50.0, 0.0});
    QMouseEvent measurePress(QEvent::MouseButtonPress, QPointF(measurePoint.x, measurePoint.y),
        Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&canvas, &measurePress);
    EXPECT_EQ(canvas.interactionState(), atlas::ui::CanvasWidget::InteractionState::measuring);
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

// Verifies a double-click Finish gesture emits the same single RoadSpline source record as keyboard completion.
TEST_F(CanvasWidgetTest, RoadDoubleClickFinishesDraft) {
    atlas::ui::CanvasWidget canvas;
    canvas.setTool(atlas::ui::CanvasWidget::Tool::road);
    std::optional<atlas::domain::RoadSpline> created;
    canvas.setRoadCreatedHandler([&](atlas::domain::RoadSpline road) { created = std::move(road); });
    for (const auto key : {Qt::Key_Space, Qt::Key_Right, Qt::Key_Right, Qt::Key_Right, Qt::Key_Space}) {
        QKeyEvent event(QEvent::KeyPress, key, Qt::NoModifier);
        QApplication::sendEvent(&canvas, &event);
    }
    QMouseEvent finish(QEvent::MouseButtonDblClick, QPointF(0.5, 0.5),
        Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&canvas, &finish);

    ASSERT_TRUE(created.has_value());
    ASSERT_EQ(created->primitives.size(), 1U);
    EXPECT_EQ(created->primitives.front()["kind"], "line");
    EXPECT_DOUBLE_EQ(created->primitives.front()["end"]["x"].get<double>(), 3.0);
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

// Verifies road-draft undo/redo restores the last provisional point without changing the authoritative command flow.
TEST_F(CanvasWidgetTest, RoadDraftUndoRedoRestoresMostRecentPoint) {
    atlas::ui::CanvasWidget canvas;
    canvas.setTool(atlas::ui::CanvasWidget::Tool::road);
    std::optional<atlas::domain::RoadSpline> created;
    canvas.setRoadCreatedHandler([&](atlas::domain::RoadSpline road) { created = std::move(road); });

    QKeyEvent addStart(QEvent::KeyPress, Qt::Key_Space, Qt::NoModifier);
    QApplication::sendEvent(&canvas, &addStart);
    QKeyEvent move(QEvent::KeyPress, Qt::Key_Right, Qt::NoModifier);
    QApplication::sendEvent(&canvas, &move);
    QKeyEvent addEnd(QEvent::KeyPress, Qt::Key_Space, Qt::NoModifier);
    QApplication::sendEvent(&canvas, &addEnd);
    QKeyEvent undo(QEvent::KeyPress, Qt::Key_Z, Qt::ControlModifier);
    QApplication::sendEvent(&canvas, &undo);
    QKeyEvent redo(QEvent::KeyPress, Qt::Key_Y, Qt::ControlModifier);
    QApplication::sendEvent(&canvas, &redo);
    QKeyEvent finish(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
    QApplication::sendEvent(&canvas, &finish);

    ASSERT_TRUE(created.has_value());
    EXPECT_EQ(created->primitives.size(), 1U);
    EXPECT_EQ(created->primitives.front()["kind"], "line");
    EXPECT_DOUBLE_EQ(created->primitives.front()["start"]["x"].get<double>(), 0.0);
    EXPECT_DOUBLE_EQ(created->primitives.front()["end"]["x"].get<double>(), 1.0);
}

// Verifies pointer drag-to-place emits provisional road points and finalizes only after an explicit finish command.
TEST_F(CanvasWidgetTest, RoadDraftPointerDragCreatesSnappedEndpoints) {
    atlas::ui::CanvasWidget canvas;
    canvas.resize(640, 480);
    canvas.show();
    QApplication::processEvents();
    canvas.setTool(atlas::ui::CanvasWidget::Tool::road);
    std::optional<atlas::domain::RoadSpline> created;
    QString status;
    canvas.setRoadCreatedHandler([&](atlas::domain::RoadSpline road) { created = std::move(road); });
    canvas.setStatusHandler([&](QString message) { status = std::move(message); });
    const auto sendDrag = [&](double startX, double endX) {
        const auto start = canvas.camera().worldToScreen({startX, 0.0});
        const auto end = canvas.camera().worldToScreen({endX, 0.0});
        QMouseEvent press(QEvent::MouseButtonPress, QPointF(start.x, start.y),
            Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(&canvas, &press);
        QMouseEvent move(QEvent::MouseMove, QPointF(end.x, end.y),
            Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(&canvas, &move);
        QMouseEvent release(QEvent::MouseButtonRelease, QPointF(end.x, end.y),
            Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(&canvas, &release);
    };
    const auto cancelledStart = canvas.camera().worldToScreen({0.0, 0.0});
    const auto cancelledEnd = canvas.camera().worldToScreen({17.2, 0.0});
    QMouseEvent heldPress(QEvent::MouseButtonPress, QPointF(cancelledStart.x, cancelledStart.y),
        Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&canvas, &heldPress);
    QMouseEvent heldMove(QEvent::MouseMove, QPointF(cancelledEnd.x, cancelledEnd.y),
        Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&canvas, &heldMove);
    QKeyEvent cancel(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
    QApplication::sendEvent(&canvas, &cancel);
    QMouseEvent cancelledRelease(QEvent::MouseButtonRelease, QPointF(cancelledEnd.x, cancelledEnd.y),
        Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(&canvas, &cancelledRelease);

    sendDrag(20.2, 30.1);
    EXPECT_NE(status.indexOf(QStringLiteral("1 point(s)")), -1);
    sendDrag(40.0, 50.1);
    EXPECT_NE(status.indexOf(QStringLiteral("2 point(s)")), -1);
    EXPECT_FALSE(created.has_value());
    canvas.finishRoadDrawing();
    ASSERT_TRUE(created.has_value());
    EXPECT_DOUBLE_EQ(created->primitives.front()["start"]["x"].get<double>(), 30.0);
    EXPECT_DOUBLE_EQ(created->primitives.front()["end"]["x"].get<double>(), 50.0);
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

// Verifies hovering a RoadSpline exposes its stable identity before selection.
TEST_F(CanvasWidgetTest, HoverAnnouncesRoadSplineTarget) {
    auto project = atlas::domain::Project::empty("project", "root-map");
    atlas::domain::RoadSpline road;
    road.id = "road-hover";
    road.mapId = "root-map";
    road.primitives = nlohmann::json::array({nlohmann::json{
        {"id", "line"}, {"kind", "line"}, {"startControlPointId", "a"}, {"endControlPointId", "b"},
        {"start", {{"x", 0.0}, {"y", 0.0}}}, {"end", {{"x", 100.0}, {"y", 0.0}}}}});
    project = project.withRoadSpline(std::move(road));
    atlas::ui::CanvasWidget canvas;
    canvas.resize(640, 480);
    canvas.setProject(std::move(project));
    QString status;
    canvas.setStatusHandler([&](QString message) { status = std::move(message); });
    canvas.show();
    QApplication::processEvents();
    const auto screen = canvas.camera().worldToScreen({50.0, 0.0});
    QMouseEvent move(QEvent::MouseMove, QPointF(screen.x, screen.y),
        Qt::NoButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(&canvas, &move);

    EXPECT_NE(status.indexOf(QStringLiteral("Coordinates:")), -1);
    EXPECT_NE(canvas.accessibleDescription().indexOf(QStringLiteral("RoadSpline target road-hover")), -1);
}

// Verifies hovering a generic point exposes its stable object ID before selection.
TEST_F(CanvasWidgetTest, HoverAnnouncesGenericObjectTarget) {
    auto project = atlas::domain::Project::empty("project", "root-map");
    project = project.withRootMap(project.rootMap().withObject(atlas::domain::MapObject::create(
        "point-hover", "core.Point", {{"x", 0.0}, {"y", 0.0}})));
    atlas::ui::CanvasWidget canvas;
    canvas.resize(640, 480);
    canvas.setProject(std::move(project));
    canvas.show();
    QApplication::processEvents();
    const auto screen = canvas.camera().worldToScreen({0.0, 0.0});
    QMouseEvent move(QEvent::MouseMove, QPointF(screen.x, screen.y),
        Qt::NoButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(&canvas, &move);

    EXPECT_NE(canvas.accessibleDescription().indexOf(QStringLiteral("Map object target point-hover")), -1);
}

// Verifies invalid derived envelopes use a visible non-color cue and do not mutate road source data.
TEST_F(CanvasWidgetTest, InvalidEnvelopeUsesPatternAndPreservesSource) {
    auto project = atlas::domain::Project::empty("project", "root-map");
    atlas::domain::RoadSpline road;
    road.id = "road-invalid-envelope";
    road.mapId = "root-map";
    road.primitives = nlohmann::json::array({nlohmann::json{
        {"id", "arc"}, {"kind", "circular-arc"},
        {"startControlPointId", "start"}, {"endControlPointId", "end"},
        {"start", {{"x", 0.0}, {"y", 0.0}}}, {"end", {{"x", 2.0}, {"y", 0.0}}},
        {"radius", 3.0}, {"side", "left"}}});
    project = project.withRoadSpline(std::move(road));
    const auto source = project.normalizedJson();
    atlas::ui::CanvasWidget canvas;
    canvas.resize(640, 480);
    canvas.setProject(project);
    canvas.show();
    QApplication::processEvents();
    const auto frame = canvas.grabFramebuffer();
    std::size_t diagnosticPixels = 0;
    for (int y = 0; y < frame.height(); ++y) {
        for (int x = 0; x < frame.width(); ++x) {
            const auto color = frame.pixelColor(x, y);
            if (color.red() > 180 && color.green() < 160 && color.blue() < 150) ++diagnosticPixels;
        }
    }
    EXPECT_GT(diagnosticPixels, 0U);
    EXPECT_EQ(project.normalizedJson(), source);
}

// Verifies malformed authoritative road source is labeled separately from an invalid derived envelope.
TEST_F(CanvasWidgetTest, InvalidSourceRoadReportsDistinctDiagnostic) {
    auto project = atlas::domain::Project::empty("project", "root-map");
    atlas::domain::RoadSpline road;
    road.id = "invalid-source";
    road.mapId = "root-map";
    road.primitives = nlohmann::json::array({nlohmann::json{{"id", "unsupported"}, {"kind", "unsupported"}}});
    project = project.withRoadSpline(std::move(road));
    atlas::ui::CanvasWidget canvas;
    canvas.resize(640, 480);
    canvas.setProject(std::move(project));
    QString status;
    canvas.setStatusHandler([&](QString message) { status = std::move(message); });
    canvas.show();
    QApplication::processEvents();
    canvas.grabFramebuffer();

    EXPECT_NE(status.indexOf(QStringLiteral("Source RoadSpline invalid-source invalid")), -1);
    EXPECT_NE(canvas.accessibleDescription().indexOf(QStringLiteral("Source road invalid")), -1);
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
