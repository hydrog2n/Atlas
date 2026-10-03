#include "atlas/persistence/package.hpp"
#include "atlas/ui/main_window.hpp"

#include <QAction>
#include <QAccessible>
#include <QApplication>
#include <QComboBox>
#include <QDockWidget>
#include <QFont>
#include <QDoubleSpinBox>
#include <QDir>
#include <QFileInfo>
#include <QFile>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QInputDialog>
#include <QLabel>
#include <QMessageBox>
#include <QPixmap>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QStatusBar>
#include <QTemporaryDir>
#include <QTimer>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <memory>

namespace {

class RoadAuthoringUiTest : public ::testing::Test {
protected:
    void SetUp() override {
        if (QApplication::instance() == nullptr) {
            static int argumentCount = 1;
            static char applicationName[] = "atlas_road_ui_tests";
            static char* arguments[] = {applicationName, nullptr};
            application_ = std::make_unique<QApplication>(argumentCount, arguments);
        }
    }

    static void key(atlas::ui::CanvasWidget& canvas, int keyCode) {
        QKeyEvent event(QEvent::KeyPress, keyCode, Qt::NoModifier);
        QApplication::sendEvent(&canvas, &event);
    }

    static void drawStraightRoad(atlas::ui::MainWindow& window, int lengthMeters = 4) {
        auto* canvas = window.canvas();
        canvas->setTool(atlas::ui::CanvasWidget::Tool::road);
        key(*canvas, Qt::Key_Space);
        for (int step = 0; step < lengthMeters; ++step) key(*canvas, Qt::Key_Right);
        key(*canvas, Qt::Key_Space);
        key(*canvas, Qt::Key_Return);
    }

    static atlas::domain::Project projectWithInteriorAnchor() {
        auto project = atlas::domain::Project::empty("project", "root-map");
        atlas::domain::RoadSpline road{
            "road-with-anchor", "root-map",
            nlohmann::json::array({nlohmann::json{
                {"id", "line"}, {"kind", "line"}, {"startControlPointId", "start"},
                {"endControlPointId", "end"}, {"start", {{"x", 0.0}, {"y", 0.0}}},
                {"end", {{"x", 8.0}, {"y", 0.0}}}}}),
            "start-to-end", {}, nullptr, nlohmann::json::object(), nlohmann::json::array(), {}};
        atlas::application::CommandProcessor processor({0, std::move(project), {}});
        processor.commit(atlas::application::CreateRoadSplineCommand(std::move(road), 0));
        auto source = processor.current().project;
        auto anchoredRoad = source.roadSplines().front();
        anchoredRoad.stationAnchors.push_back({"anchor-interior", 7.0,
            atlas::domain::AnchorAffinity::geometryLocked,
            {0.875, "line", 0.875, {7.0, 0.0}}});
        return source.withReplacedRoadSpline(std::move(anchoredRoad));
    }

    QPushButton* button(atlas::ui::MainWindow& window, const char* objectName) {
        return window.findChild<QPushButton*>(QString::fromLatin1(objectName));
    }

    static std::unique_ptr<QApplication> application_;
};

std::unique_ptr<QApplication> RoadAuthoringUiTest::application_;

double relativeLuminance(const QColor& color) {
    const auto linear = [](double channel) {
        return channel <= 0.04045 ? channel / 12.92 : std::pow((channel + 0.055) / 1.055, 2.4);
    };
    return 0.2126 * linear(color.redF()) + 0.7152 * linear(color.greenF()) + 0.0722 * linear(color.blueF());
}

double contrastRatio(QColor foreground, QColor background) {
    if (foreground.alphaF() < 1.0) {
        const auto alpha = foreground.alphaF();
        foreground.setRedF(foreground.redF() * alpha + background.redF() * (1.0 - alpha));
        foreground.setGreenF(foreground.greenF() * alpha + background.greenF() * (1.0 - alpha));
        foreground.setBlueF(foreground.blueF() * alpha + background.blueF() * (1.0 - alpha));
    }
    const auto foregroundLuminance = relativeLuminance(foreground);
    const auto backgroundLuminance = relativeLuminance(background);
    return (std::max(foregroundLuminance, backgroundLuminance) + 0.05) /
        (std::min(foregroundLuminance, backgroundLuminance) + 0.05);
}

// Verifies road creation remains a preview until Apply, and cancel/undo/redo preserve source history.
TEST_F(RoadAuthoringUiTest, CreatePreviewCancelApplyUndoRedo) {
    auto project = atlas::domain::Project::empty("project", "root-map");
    atlas::ui::MainWindow window(project);
    drawStraightRoad(window);
    ASSERT_TRUE(button(window, "applyPreviewButton"));
    EXPECT_TRUE(button(window, "cancelPreviewButton")->isEnabled());
    EXPECT_TRUE(window.commandProcessor().current().project.roadSplines().empty());
    auto* previewSummary = window.findChild<QLabel*>(QStringLiteral("roadPreviewSummary"));
    ASSERT_NE(previewSummary, nullptr);
    EXPECT_NE(previewSummary->text().indexOf(QStringLiteral("Envelope")), -1);
    EXPECT_NE(previewSummary->text().indexOf(QStringLiteral("valid")), -1);

    button(window, "cancelPreviewButton")->click();
    EXPECT_TRUE(window.commandProcessor().current().project.roadSplines().empty());

    drawStraightRoad(window);
    button(window, "applyPreviewButton")->click();
    ASSERT_EQ(window.commandProcessor().current().project.roadSplines().size(), 1U);
    ASSERT_EQ(window.commandProcessor().current().project.roadSegments().size(), 1U);
    EXPECT_EQ(window.commandProcessor().current().project.roadSplines().front().stationAnchors.size(), 2U);
    EXPECT_GT(window.commandProcessor().current().project.roadSplines().front().stationAnchors.back().resolvedStation, 0.0);
    EXPECT_EQ(window.canvas()->selection().primaryId(),
        window.commandProcessor().current().project.roadSplines().front().id);

    auto* undo = window.findChild<QAction*>(QStringLiteral("undoAction"));
    auto* redo = window.findChild<QAction*>(QStringLiteral("redoAction"));
    ASSERT_NE(undo, nullptr);
    ASSERT_NE(redo, nullptr);
    undo->trigger();
    EXPECT_TRUE(window.commandProcessor().current().project.roadSplines().empty());
    redo->trigger();
    EXPECT_EQ(window.commandProcessor().current().project.roadSplines().size(), 1U);
}

// Verifies invalid derived geometry is explicitly labeled in the preview and does not rely on color alone.
TEST_F(RoadAuthoringUiTest, InvalidEnvelopePreviewHasTextDiagnostic) {
    atlas::ui::MainWindow window(atlas::domain::Project::empty("project", "root-map"));
    auto* mode = window.findChild<QComboBox*>(QStringLiteral("roadPrimitiveMode"));
    auto* radius = window.findChild<QDoubleSpinBox*>(QStringLiteral("fixedRadiusEditor"));
    ASSERT_NE(mode, nullptr);
    ASSERT_NE(radius, nullptr);
    mode->setCurrentIndex(2);
    radius->setValue(2.1);
    window.canvas()->setTool(atlas::ui::CanvasWidget::Tool::road);
    QKeyEvent addStart(QEvent::KeyPress, Qt::Key_Space, Qt::NoModifier);
    QApplication::sendEvent(window.canvas(), &addStart);
    for (int step = 0; step < 4; ++step) {
        QKeyEvent move(QEvent::KeyPress, Qt::Key_Right, Qt::NoModifier);
        QApplication::sendEvent(window.canvas(), &move);
    }
    QKeyEvent addEnd(QEvent::KeyPress, Qt::Key_Space, Qt::NoModifier);
    QApplication::sendEvent(window.canvas(), &addEnd);
    QKeyEvent finish(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
    QApplication::sendEvent(window.canvas(), &finish);

    auto* summary = window.findChild<QLabel*>(QStringLiteral("roadPreviewSummary"));
    ASSERT_NE(summary, nullptr);
    EXPECT_NE(summary->text().indexOf(QStringLiteral("invalid preview")), -1);
    EXPECT_NE(summary->text().indexOf(QStringLiteral("Error:")), -1);
    EXPECT_TRUE(window.commandProcessor().current().project.roadSplines().empty());
}

// Verifies replacing a document cancels its staged road command so stale Apply cannot mutate the new project.
TEST_F(RoadAuthoringUiTest, NewProjectDiscardsPendingRoadPreview) {
    atlas::ui::MainWindow window(atlas::domain::Project::empty("project", "root-map"));
    drawStraightRoad(window);
    ASSERT_TRUE(button(window, "applyPreviewButton")->isEnabled());
    EXPECT_TRUE(window.commandProcessor().current().project.roadSplines().empty());

    window.findChild<QAction*>(QStringLiteral("newProjectAction"))->trigger();
    button(window, "applyPreviewButton")->click();

    EXPECT_TRUE(window.commandProcessor().current().project.roadSplines().empty());
    EXPECT_FALSE(window.findChild<QDockWidget*>(QStringLiteral("changesDiagnosticsDock"))->isVisible());
}

// Verifies road tools and authoring controls expose accessible names and keyboard shortcuts.
TEST_F(RoadAuthoringUiTest, RoadAuthoringControlsExposeAccessibleNames) {
    atlas::ui::MainWindow window(atlas::domain::Project::empty("project", "root-map"));
    EXPECT_EQ(window.canvas()->accessibleName(), QStringLiteral("Atlas map canvas"));
    auto* hierarchy = window.findChild<QTreeWidget*>(QStringLiteral("mapHierarchyWidget"));
    ASSERT_NE(hierarchy, nullptr);
    EXPECT_EQ(hierarchy->accessibleName(), QStringLiteral("Map hierarchy"));
    auto* primitiveMode = window.findChild<QComboBox*>(QStringLiteral("roadPrimitiveMode"));
    ASSERT_NE(primitiveMode, nullptr);
    EXPECT_EQ(primitiveMode->accessibleName(), QStringLiteral("Road primitive mode"));
    auto* roadAction = window.findChild<QAction*>(QStringLiteral("roadToolAction"));
    ASSERT_NE(roadAction, nullptr);
    EXPECT_EQ(roadAction->shortcut().toString(), QStringLiteral("R"));
    EXPECT_NE(window.findChild<QAction*>(QStringLiteral("undoAction")), nullptr);
    EXPECT_NE(window.findChild<QAction*>(QStringLiteral("redoAction")), nullptr);
}

// Verifies names are published through Qt's platform accessibility interface, not only widget properties.
TEST_F(RoadAuthoringUiTest, PrimaryControlsExposePlatformAccessibilityNames) {
    atlas::ui::MainWindow window(atlas::domain::Project::empty("project", "root-map"));
    auto* hierarchy = window.findChild<QTreeWidget*>(QStringLiteral("mapHierarchyWidget"));
    auto* primitiveMode = window.findChild<QComboBox*>(QStringLiteral("roadPrimitiveMode"));
    auto* apply = button(window, "applyPreviewButton");
    ASSERT_NE(hierarchy, nullptr);
    ASSERT_NE(primitiveMode, nullptr);
    ASSERT_NE(apply, nullptr);

    const auto canvasAccessible = QAccessible::queryAccessibleInterface(window.canvas());
    const auto hierarchyAccessible = QAccessible::queryAccessibleInterface(hierarchy);
    const auto modeAccessible = QAccessible::queryAccessibleInterface(primitiveMode);
    const auto applyAccessible = QAccessible::queryAccessibleInterface(apply);
    ASSERT_NE(canvasAccessible, nullptr);
    ASSERT_NE(hierarchyAccessible, nullptr);
    ASSERT_NE(modeAccessible, nullptr);
    ASSERT_NE(applyAccessible, nullptr);
    EXPECT_EQ(canvasAccessible->text(QAccessible::Name), QStringLiteral("Atlas map canvas"));
    EXPECT_EQ(hierarchyAccessible->text(QAccessible::Name), QStringLiteral("Map hierarchy"));
    EXPECT_EQ(modeAccessible->text(QAccessible::Name), QStringLiteral("Road primitive mode"));
    EXPECT_EQ(applyAccessible->text(QAccessible::Name), QStringLiteral("Apply previewed road change"));
}

// Verifies the selected-road summary exposes type, owner, Map, SpatialLevel, and DisplayLayer for UX-CORE-001.
TEST_F(RoadAuthoringUiTest, SelectedRoadSummaryExposesOwnershipContext) {
    atlas::ui::MainWindow window(atlas::domain::Project::empty("project", "root-map"));
    drawStraightRoad(window);
    button(window, "applyPreviewButton")->click();
    const auto summary = window.findChild<QLabel*>(QStringLiteral("objectInspectorSummary"));
    ASSERT_NE(summary, nullptr);
    EXPECT_NE(summary->text().indexOf(QStringLiteral("RoadSpline")), -1);
    EXPECT_NE(summary->text().indexOf(QStringLiteral("Owner Map: root-map")), -1);
    EXPECT_NE(summary->text().indexOf(QStringLiteral("Map: root-map")), -1);
    EXPECT_NE(summary->text().indexOf(QStringLiteral("SpatialLevel: default-level")), -1);
    EXPECT_NE(summary->text().indexOf(QStringLiteral("DisplayLayer: default-layer")), -1);
}

// Verifies text and essential road-state overlays meet WCAG-style contrast thresholds in the desktop palette.
TEST_F(RoadAuthoringUiTest, TextAndRoadOverlaysMeetContrastThresholds) {
    atlas::ui::MainWindow window(atlas::domain::Project::empty("project", "root-map"));
    const auto palette = window.palette();
    EXPECT_GE(contrastRatio(palette.color(QPalette::WindowText), palette.color(QPalette::Window)), 4.5);
    const QColor canvasBackground(20, 23, 28);
    EXPECT_GE(contrastRatio(QColor(180, 190, 205), canvasBackground), 3.0);
    EXPECT_GE(contrastRatio(QColor(255, 220, 100), canvasBackground), 3.0);
    EXPECT_GE(contrastRatio(QColor(190, 220, 240), canvasBackground), 3.0);
    EXPECT_GE(contrastRatio(QColor(64, 140, 195, 210), canvasBackground), 3.0);
    EXPECT_GE(contrastRatio(QColor(220, 70, 60), canvasBackground), 3.0);
}

// Verifies Inspector and transform-space controls remain reachable at baseline and doubled UI text scale.
TEST_F(RoadAuthoringUiTest, InspectorLayoutRemainsReachableAtDoubleTextScale) {
    atlas::ui::MainWindow window(atlas::domain::Project::empty("project", "root-map"));
    window.resize(1280, 800);
    window.show();
    QApplication::processEvents();
    drawStraightRoad(window, 8);
    button(window, "applyPreviewButton")->click();
    window.canvas()->setTool(atlas::ui::CanvasWidget::Tool::select);
    window.canvas()->setFocus();
    QApplication::processEvents();
    ASSERT_TRUE(window.canvas()->hasFocus());
    auto* inspector = window.findChild<QScrollArea*>(QStringLiteral("inspectorScrollArea"));
    ASSERT_NE(inspector, nullptr);
    auto baseFont = window.font();
    const auto basePointSize = baseFont.pointSizeF() > 0.0 ? baseFont.pointSizeF() : 9.0;
    for (const auto scale : {1.0, 2.0}) {
        auto scaledFont = baseFont;
        scaledFont.setPointSizeF(basePointSize * scale);
        window.setFont(scaledFont);
        QApplication::processEvents();
        EXPECT_GT(inspector->viewport()->width(), 0);
        EXPECT_GT(inspector->viewport()->height(), 0);
        ASSERT_NE(inspector->widget(), nullptr);
        EXPECT_LE(inspector->widget()->width(), inspector->viewport()->width());
        EXPECT_EQ(inspector->horizontalScrollBar()->maximum(), 0);
        EXPECT_GE(inspector->verticalScrollBar()->maximum(), 0);
        if (const auto* screenshotDirectory = std::getenv("ATLAS_UI_SCREENSHOT_DIR")) {
            const auto scaleLabel = scale == 1.0 ? QStringLiteral("100") : QStringLiteral("200");
            const auto screenshotPath = QString::fromLocal8Bit(screenshotDirectory) +
                QStringLiteral("/atlas-j-workspace-%1.png").arg(scaleLabel);
            EXPECT_TRUE(window.grab().save(screenshotPath)) << screenshotPath.toStdString();
            const auto canvasPath = QString::fromLocal8Bit(screenshotDirectory) +
                QStringLiteral("/atlas-j-canvas-%1.png").arg(scaleLabel);
            EXPECT_TRUE(window.canvas()->grabFramebuffer().save(canvasPath)) << canvasPath.toStdString();
        }
    }
}

// Verifies tool shortcuts work from the canvas without hijacking letters typed into Inspector fields.
TEST_F(RoadAuthoringUiTest, ToolShortcutsAreCanvasScoped) {
    atlas::ui::MainWindow window(atlas::domain::Project::empty("project", "root-map"));
    QKeyEvent activateRoad(QEvent::KeyPress, Qt::Key_R, Qt::NoModifier);
    QApplication::sendEvent(window.canvas(), &activateRoad);
    EXPECT_EQ(window.canvas()->tool(), atlas::ui::CanvasWidget::Tool::road);
    auto* endpointX = window.findChild<QDoubleSpinBox*>(QStringLiteral("roadEndpointX"));
    ASSERT_NE(endpointX, nullptr);
    endpointX->setFocus();
    QKeyEvent typeM(QEvent::KeyPress, Qt::Key_M, Qt::NoModifier, QStringLiteral("m"));
    QApplication::sendEvent(endpointX, &typeM);
    EXPECT_EQ(window.canvas()->tool(), atlas::ui::CanvasWidget::Tool::road);
    QKeyEvent activateMeasure(QEvent::KeyPress, Qt::Key_M, Qt::NoModifier);
    QApplication::sendEvent(window.canvas(), &activateMeasure);
    EXPECT_EQ(window.canvas()->tool(), atlas::ui::CanvasWidget::Tool::measure);
}

// Verifies Select-mode handle drags cancel with Escape or commit on release, while Inspector edits remain previewed.
TEST_F(RoadAuthoringUiTest, ControlPointDragCommitsAndInspectorEditPreviews) {
    atlas::ui::MainWindow window(atlas::domain::Project::empty("project", "root-map"));
    window.show();
    QApplication::processEvents();
    drawStraightRoad(window);
    button(window, "applyPreviewButton")->click();
    const auto before = window.commandProcessor().current().normalizedSource();
    auto* canvas = window.canvas();
    canvas->setTool(atlas::ui::CanvasWidget::Tool::select);

    const auto startScreen = canvas->camera().worldToScreen({0.0, 0.0});
    QMouseEvent press(QEvent::MouseButtonPress, QPointF(startScreen.x, startScreen.y),
        Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(canvas, &press);
    const auto movedScreen = canvas->camera().worldToScreen({0.0, 1.0});
    QMouseEvent move(QEvent::MouseMove, QPointF(movedScreen.x, movedScreen.y),
        Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(canvas, &move);
    QKeyEvent cancelDrag(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
    QApplication::sendEvent(canvas, &cancelDrag);
    EXPECT_EQ(window.commandProcessor().current().normalizedSource(), before);

    QMouseEvent secondPress(QEvent::MouseButtonPress, QPointF(startScreen.x, startScreen.y),
        Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(canvas, &secondPress);
    QMouseEvent secondMove(QEvent::MouseMove, QPointF(movedScreen.x, movedScreen.y),
        Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(canvas, &secondMove);
    QMouseEvent release(QEvent::MouseButtonRelease, QPointF(movedScreen.x, movedScreen.y),
        Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(canvas, &release);

    EXPECT_NE(window.commandProcessor().current().normalizedSource(), before);
    window.findChild<QAction*>(QStringLiteral("undoAction"))->trigger();
    EXPECT_EQ(window.commandProcessor().current().normalizedSource(), before);

    auto* pointSelector = window.findChild<QComboBox*>(QStringLiteral("controlPointSelector"));
    auto* pointY = window.findChild<QDoubleSpinBox*>(QStringLiteral("controlPointY"));
    ASSERT_NE(pointSelector, nullptr);
    ASSERT_NE(pointY, nullptr);
    for (int index = 0; index < pointSelector->count(); ++index) {
        if (pointSelector->itemText(index).endsWith(QStringLiteral("/control/1"))) {
            pointSelector->setCurrentIndex(index);
            break;
        }
    }
    pointY->setValue(2.5);
    button(window, "previewControlPointEditButton")->click();
    EXPECT_EQ(window.commandProcessor().current().normalizedSource(), before);
    button(window, "applyPreviewButton")->click();
    EXPECT_NE(window.commandProcessor().current().normalizedSource(), before);
    EXPECT_GT(window.commandProcessor().current().project.roadSplines().front().stationAnchors.back().resolvedStation, 4.0);
    window.findChild<QAction*>(QStringLiteral("undoAction"))->trigger();
    EXPECT_EQ(window.commandProcessor().current().normalizedSource(), before);
}

// Verifies stable-ID control points can receive keyboard focus and move through the undoable edit command path.
TEST_F(RoadAuthoringUiTest, ControlPointKeyboardFocusAndNudgeAreUndoable) {
    atlas::ui::MainWindow window(atlas::domain::Project::empty("project", "root-map"));
    window.show();
    QApplication::processEvents();
    drawStraightRoad(window, 8);
    button(window, "applyPreviewButton")->click();
    auto* canvas = window.canvas();
    canvas->setTool(atlas::ui::CanvasWidget::Tool::select);
    const auto controlPoint = canvas->camera().worldToScreen({8.0, 0.0});
    QMouseEvent hover(QEvent::MouseMove, QPointF(controlPoint.x + 9.0, controlPoint.y),
        Qt::NoButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(canvas, &hover);
    QKeyEvent focus(QEvent::KeyPress, Qt::Key_Tab, Qt::NoModifier);
    QApplication::sendEvent(canvas, &focus);
    EXPECT_NE(canvas->accessibleDescription().indexOf(QStringLiteral("Focused road control point")), -1);
    EXPECT_NE(canvas->accessibleDescription().indexOf(QStringLiteral("/control/1")), -1);
    const auto before = window.commandProcessor().current().normalizedSource();
    QKeyEvent nudge(QEvent::KeyPress, Qt::Key_Left, Qt::NoModifier);
    QApplication::sendEvent(canvas, &nudge);
    ASSERT_NE(window.commandProcessor().current().normalizedSource(), before)
        << window.findChild<QLabel*>(QStringLiteral("roadPreviewSummary"))->text().toStdString();
    window.findChild<QAction*>(QStringLiteral("undoAction"))->trigger();
    EXPECT_EQ(window.commandProcessor().current().normalizedSource(), before);
}

// Verifies point-like generic objects drag through EditMapObjectCommand, cancel without mutation, and undo on release.
TEST_F(RoadAuthoringUiTest, GenericPointDragCommitsCancelsAndUndoes) {
    auto project = atlas::domain::Project::empty("project", "root-map");
    auto map = project.rootMap().withObject(atlas::domain::MapObject::create(
        "point-1", "core.Point", {{"x", 0.0}, {"y", 0.0}}));
    project = project.withRootMap(std::move(map));
    atlas::ui::MainWindow window(std::move(project));
    window.show();
    QApplication::processEvents();
    auto* canvas = window.canvas();
    canvas->setTool(atlas::ui::CanvasWidget::Tool::select);
    const auto before = window.commandProcessor().current().normalizedSource();
    const auto start = canvas->camera().worldToScreen({0.0, 0.0});
    const auto finish = canvas->camera().worldToScreen({2.0, 3.0});

    QMouseEvent press(QEvent::MouseButtonPress, QPointF(start.x, start.y),
        Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(canvas, &press);
    QMouseEvent move(QEvent::MouseMove, QPointF(finish.x, finish.y),
        Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(canvas, &move);
    QKeyEvent cancel(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
    QApplication::sendEvent(canvas, &cancel);
    EXPECT_EQ(window.commandProcessor().current().normalizedSource(), before);

    QApplication::sendEvent(canvas, &press);
    QApplication::sendEvent(canvas, &move);
    QMouseEvent release(QEvent::MouseButtonRelease, QPointF(finish.x, finish.y),
        Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(canvas, &release);
    const auto* moved = window.commandProcessor().current().project.rootMap().findObject("point-1");
    ASSERT_NE(moved, nullptr);
    EXPECT_DOUBLE_EQ(moved->geometry().value("x", 0.0), 2.0);
    EXPECT_DOUBLE_EQ(moved->geometry().value("y", 0.0), 3.0);
    window.findChild<QAction*>(QStringLiteral("undoAction"))->trigger();
    EXPECT_EQ(window.commandProcessor().current().normalizedSource(), before);
}

// Verifies tool actions expose the active mode and the road context toolbar provides explicit draft completion.
TEST_F(RoadAuthoringUiTest, ToolStateZoomAndRoadFinishAreDiscoverable) {
    atlas::ui::MainWindow window(atlas::domain::Project::empty("project", "root-map"));
    window.show();
    QApplication::processEvents();
    auto* roadAction = window.findChild<QAction*>(QStringLiteral("roadToolAction"));
    auto* selectAction = window.findChild<QAction*>(QStringLiteral("selectToolAction"));
    auto* zoom = window.findChild<QLabel*>(QStringLiteral("zoomReadout"));
    auto* finish = window.findChild<QPushButton*>(QStringLiteral("finishRoadDraftButton"));
    auto* fit = window.findChild<QAction*>(QStringLiteral("fitSelectedRoadAction"));
    ASSERT_NE(roadAction, nullptr);
    ASSERT_NE(selectAction, nullptr);
    ASSERT_NE(zoom, nullptr);
    ASSERT_NE(finish, nullptr);
    ASSERT_NE(fit, nullptr);
    ASSERT_NE(window.findChild<QAction*>(QStringLiteral("fitSelectionAction")), nullptr);
    roadAction->trigger();
    EXPECT_TRUE(roadAction->isChecked());
    EXPECT_FALSE(selectAction->isChecked());
    EXPECT_TRUE(finish->isVisible());
    const auto initialZoom = zoom->text();
    QKeyEvent zoomIn(QEvent::KeyPress, Qt::Key_Plus, Qt::NoModifier);
    QApplication::sendEvent(window.canvas(), &zoomIn);
    EXPECT_NE(zoom->text(), initialZoom);
    for (const auto key : {Qt::Key_Space, Qt::Key_Right, Qt::Key_Space}) {
        QKeyEvent event(QEvent::KeyPress, key, Qt::NoModifier);
        QApplication::sendEvent(window.canvas(), &event);
    }
    finish->click();
    EXPECT_TRUE(window.findChild<QPushButton*>(QStringLiteral("applyPreviewButton"))->isEnabled());
}

// Verifies Fit Selection centers the viewport on a selected generic point and updates the zoom readout.
TEST_F(RoadAuthoringUiTest, FitSelectionCentersPointObject) {
    auto project = atlas::domain::Project::empty("project", "root-map");
    project = project.withRootMap(project.rootMap().withObject(atlas::domain::MapObject::create(
        "point-fit", "core.Point", {{"x", 24.0}, {"y", -11.0}})));
    atlas::ui::MainWindow window(std::move(project));
    window.show();
    QApplication::processEvents();
    window.canvas()->selectId("point-fit");
    auto* fit = window.findChild<QAction*>(QStringLiteral("fitSelectionAction"));
    ASSERT_NE(fit, nullptr);
    auto* localTransform = window.findChild<QAction*>(QStringLiteral("localTransformSpaceAction"));
    ASSERT_NE(localTransform, nullptr);
    EXPECT_FALSE(localTransform->isEnabled());
    fit->trigger();
    EXPECT_DOUBLE_EQ(window.canvas()->camera().center().x, 24.0);
    EXPECT_DOUBLE_EQ(window.canvas()->camera().center().y, -11.0);
    EXPECT_NE(window.findChild<QLabel*>(QStringLiteral("zoomReadout"))->text().indexOf(QStringLiteral("Zoom:")), -1);
}

// Verifies world-axis translation and rotation gizmo drags commit as source-coordinate edits and undo cleanly.
TEST_F(RoadAuthoringUiTest, RoadTransformGizmoTranslatesRotatesAndUndoes) {
    atlas::ui::MainWindow window(atlas::domain::Project::empty("project", "root-map"));
    window.show();
    QApplication::processEvents();
    drawStraightRoad(window, 8);
    button(window, "applyPreviewButton")->click();
    auto* canvas = window.canvas();
    canvas->setTool(atlas::ui::CanvasWidget::Tool::select);
    const auto before = window.commandProcessor().current().normalizedSource();
    const auto zoom = canvas->camera().zoom();
    const auto center = canvas->camera().worldToScreen({4.0, 0.0});
    const QPointF xHandle(center.x + 36.0, center.y);
    QMouseEvent press(QEvent::MouseButtonPress, xHandle, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(canvas, &press);
    QMouseEvent move(QEvent::MouseMove, xHandle + QPointF(30.0, 0.0),
        Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(canvas, &move);
    QMouseEvent release(QEvent::MouseButtonRelease, xHandle + QPointF(30.0, 0.0),
        Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(canvas, &release);
    const auto translated = window.commandProcessor().current().normalizedSource();
    EXPECT_NE(translated, before);
    EXPECT_NEAR(window.commandProcessor().current().project.roadSplines().front()
        .primitives.front()["start"]["x"].get<double>(), 30.0 / zoom, 1.0e-6);

    const auto rotationCenter = canvas->camera().worldToScreen({4.0 + 30.0 / zoom, 0.0});
    const QPointF rotationHandle(rotationCenter.x + 38.0, rotationCenter.y - 38.0);
    QMouseEvent rotationPress(QEvent::MouseButtonPress, rotationHandle,
        Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(canvas, &rotationPress);
    EXPECT_NE(canvas->accessibleDescription().indexOf(QStringLiteral("World rotation handle")), -1);
    const QPointF rotatedPosition(rotationCenter.x, rotationCenter.y - 54.0);
    QMouseEvent rotationMove(QEvent::MouseMove, rotatedPosition,
        Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(canvas, &rotationMove);
    QMouseEvent rotationRelease(QEvent::MouseButtonRelease, rotatedPosition,
        Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(canvas, &rotationRelease);
    EXPECT_NE(window.commandProcessor().current().normalizedSource(), translated);
    window.findChild<QAction*>(QStringLiteral("undoAction"))->trigger();
    EXPECT_EQ(window.commandProcessor().current().normalizedSource(), translated);
    window.findChild<QAction*>(QStringLiteral("undoAction"))->trigger();
    EXPECT_EQ(window.commandProcessor().current().normalizedSource(), before);
}

// Verifies Local mode follows a RoadSpline tangent frame and commits translation through the normal edit command.
TEST_F(RoadAuthoringUiTest, RoadTransformGizmoUsesLocalTangentAxes) {
    auto project = atlas::domain::Project::empty("project", "root-map");
    atlas::domain::RoadSpline road;
    road.id = "vertical-road";
    road.mapId = "root-map";
    road.primitives = nlohmann::json::array({nlohmann::json{
        {"id", "line"}, {"kind", "line"}, {"startControlPointId", "start"},
        {"endControlPointId", "end"}, {"start", {{"x", 0.0}, {"y", 0.0}}},
        {"end", {{"x", 0.0}, {"y", 8.0}}}}});
    atlas::application::CommandProcessor processor({0, std::move(project), {}});
    processor.commit(atlas::application::CreateRoadSplineCommand(std::move(road), 0));
    atlas::ui::MainWindow window(processor.current().project);
    window.show();
    QApplication::processEvents();
    auto* canvas = window.canvas();
    canvas->setTool(atlas::ui::CanvasWidget::Tool::select);
    canvas->selectId("vertical-road");
    auto* localAction = window.findChild<QAction*>(QStringLiteral("localTransformSpaceAction"));
    ASSERT_NE(localAction, nullptr);
    ASSERT_TRUE(localAction->isEnabled());
    localAction->trigger();
    EXPECT_EQ(canvas->transformSpace(), atlas::ui::CanvasWidget::TransformSpace::local);
    const auto before = window.commandProcessor().current().normalizedSource();
    const auto zoom = canvas->camera().zoom();
    const auto center = canvas->camera().worldToScreen({0.0, 4.0});
    const QPointF handle(center.x, center.y - 30.0);
    QMouseEvent press(QEvent::MouseButtonPress, handle, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(canvas, &press);
    QMouseEvent move(QEvent::MouseMove, handle + QPointF(0.0, -30.0),
        Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(canvas, &move);
    QMouseEvent release(QEvent::MouseButtonRelease, handle + QPointF(0.0, -30.0),
        Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(canvas, &release);
    EXPECT_NE(window.commandProcessor().current().normalizedSource(), before);
    EXPECT_NEAR(window.commandProcessor().current().project.roadSplines().front()
        .primitives.front()["start"]["y"].get<double>(), 30.0 / zoom, 1.0e-6);
    window.findChild<QAction*>(QStringLiteral("undoAction"))->trigger();
    EXPECT_EQ(window.commandProcessor().current().normalizedSource(), before);
}

// Verifies point-object world-axis gizmo translation commits on release and Escape cancels without source mutation.
TEST_F(RoadAuthoringUiTest, PointTransformGizmoTranslatesAndCancels) {
    auto project = atlas::domain::Project::empty("project", "root-map");
    project = project.withRootMap(project.rootMap().withObject(atlas::domain::MapObject::create(
        "point-transform", "core.Point", {{"x", 2.0}, {"y", 3.0}})));
    atlas::ui::MainWindow window(std::move(project));
    window.show();
    QApplication::processEvents();
    auto* canvas = window.canvas();
    canvas->setTool(atlas::ui::CanvasWidget::Tool::select);
    canvas->selectId("point-transform");
    const auto before = window.commandProcessor().current().normalizedSource();
    const auto zoom = canvas->camera().zoom();
    const auto center = canvas->camera().worldToScreen({2.0, 3.0});
    const QPointF handle(center.x + 30.0, center.y);
    QMouseEvent press(QEvent::MouseButtonPress, handle, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(canvas, &press);
    QMouseEvent move(QEvent::MouseMove, handle + QPointF(20.0, 0.0),
        Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(canvas, &move);
    QMouseEvent release(QEvent::MouseButtonRelease, handle + QPointF(20.0, 0.0),
        Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(canvas, &release);
    const auto* moved = window.commandProcessor().current().project.rootMap().findObject("point-transform");
    ASSERT_NE(moved, nullptr);
    EXPECT_NEAR(moved->geometry().value("x", 0.0), 2.0 + 20.0 / zoom, 1.0e-6);
    EXPECT_DOUBLE_EQ(moved->geometry().value("y", 0.0), 3.0);
    window.findChild<QAction*>(QStringLiteral("undoAction"))->trigger();
    EXPECT_EQ(window.commandProcessor().current().normalizedSource(), before);

    QMouseEvent cancelPress(QEvent::MouseButtonPress, handle, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(canvas, &cancelPress);
    QApplication::sendEvent(canvas, &move);
    QKeyEvent cancel(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
    QApplication::sendEvent(canvas, &cancel);
    EXPECT_EQ(window.commandProcessor().current().normalizedSource(), before);
}

// Verifies unresolved handle-edit anchors require an explicit station choice and cancel without mutation.
TEST_F(RoadAuthoringUiTest, HandleEditPromptsForAnchorStationResolution) {
    auto project = projectWithInteriorAnchor();
    auto road = project.roadSplines().front();
    road.primitives[0]["id"] = "replacement-line";
    project = project.withReplacedRoadSpline(std::move(road));
    atlas::ui::MainWindow window(std::move(project));
    window.show();
    QApplication::processEvents();
    auto* canvas = window.canvas();
    canvas->setTool(atlas::ui::CanvasWidget::Tool::select);
    canvas->selectId("road-with-anchor");
    const auto before = window.commandProcessor().current().normalizedSource();
    const auto dragStart = canvas->camera().worldToScreen({0.0, 0.0});
    const auto dragEnd = canvas->camera().worldToScreen({0.0, 1.0});
    const auto dragHandle = [&]() {
        QMouseEvent press(QEvent::MouseButtonPress, QPointF(dragStart.x, dragStart.y),
            Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(canvas, &press);
        QMouseEvent move(QEvent::MouseMove, QPointF(dragEnd.x, dragEnd.y),
            Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(canvas, &move);
        QMouseEvent release(QEvent::MouseButtonRelease, QPointF(dragEnd.x, dragEnd.y),
            Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(canvas, &release);
    };
    const auto answerStationDialog = [](double station, bool accept) {
        QTimer::singleShot(0, [station, accept]() {
            auto* dialog = qobject_cast<QInputDialog*>(QApplication::activeModalWidget());
            if (dialog == nullptr) return;
            dialog->setDoubleValue(station);
            if (accept) dialog->accept();
            else dialog->reject();
        });
    };

    answerStationDialog(5.0, false);
    dragHandle();
    EXPECT_EQ(window.commandProcessor().current().normalizedSource(), before);

    answerStationDialog(5.0, true);
    dragHandle();
    EXPECT_NE(window.commandProcessor().current().normalizedSource(), before);
    const auto& anchors = window.commandProcessor().current().project.roadSplines().front().stationAnchors;
    const auto anchor = std::find_if(anchors.begin(), anchors.end(), [](const auto& candidate) {
        return candidate.id == "anchor-interior";
    });
    ASSERT_NE(anchor, anchors.end());
    EXPECT_DOUBLE_EQ(anchor->resolvedStation, 5.0);
    EXPECT_EQ(anchor->remapSignature.primitiveId, "replacement-line");
}

// Verifies moving an internal RoadSegment boundary commits through the station preview.
TEST_F(RoadAuthoringUiTest, BoundaryStationPreviewCommits) {
    atlas::ui::MainWindow window(atlas::domain::Project::empty("project", "root-map"));
    drawStraightRoad(window, 8);
    button(window, "applyPreviewButton")->click();
    const auto beforeSplit = window.commandProcessor().current().normalizedSource();
    auto* canvas = window.canvas();
    canvas->setTool(atlas::ui::CanvasWidget::Tool::split);
    const auto screen = canvas->camera().worldToScreen({4.0, 0.0});
    QMouseEvent splitClick(QEvent::MouseButtonPress, QPointF(screen.x, screen.y),
        Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(canvas, &splitClick);
    button(window, "applyPreviewButton")->click();
    ASSERT_EQ(window.commandProcessor().current().project.roadSegments().size(), 2U);
    const auto afterSplit = window.commandProcessor().current().normalizedSource();
    window.findChild<QAction*>(QStringLiteral("undoAction"))->trigger();
    EXPECT_EQ(window.commandProcessor().current().normalizedSource(), beforeSplit);
    window.findChild<QAction*>(QStringLiteral("redoAction"))->trigger();
    EXPECT_EQ(window.commandProcessor().current().normalizedSource(), afterSplit);

    auto* hierarchy = window.findChild<QTreeWidget*>(QStringLiteral("mapHierarchyWidget"));
    ASSERT_NE(hierarchy, nullptr);
    for (QTreeWidgetItemIterator item(hierarchy); *item != nullptr; ++item) {
        if ((*item)->data(0, Qt::UserRole + 1).toString() == QStringLiteral("segment")) {
            hierarchy->setCurrentItem(*item);
            break;
        }
    }
    window.findChild<QDoubleSpinBox*>(QStringLiteral("boundaryStation"))->setValue(3.5);
    button(window, "moveBoundaryButton")->click();
    ASSERT_TRUE(button(window, "applyPreviewButton"));
    button(window, "applyPreviewButton")->click();
    EXPECT_EQ(window.commandProcessor().current().project.roadSegments().size(), 2U);
}

// Verifies split and merge are previewed commands and reverse remains undoable.
TEST_F(RoadAuthoringUiTest, SplitMergeAndReverseUseCommandHistory) {
    atlas::ui::MainWindow window(atlas::domain::Project::empty("project", "root-map"));
    drawStraightRoad(window, 8);
    button(window, "applyPreviewButton")->click();
    auto* canvas = window.canvas();
    canvas->setTool(atlas::ui::CanvasWidget::Tool::split);
    const auto screen = canvas->camera().worldToScreen({4.0, 0.0});
    QMouseEvent splitClick(QEvent::MouseButtonPress, QPointF(screen.x, screen.y),
        Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(canvas, &splitClick);
    button(window, "applyPreviewButton")->click();
    ASSERT_EQ(window.commandProcessor().current().project.roadSegments().size(), 2U);

    auto* hierarchy = window.findChild<QTreeWidget*>(QStringLiteral("mapHierarchyWidget"));
    ASSERT_NE(hierarchy, nullptr);
    hierarchy->clearSelection();
    std::size_t selectedSegments = 0;
    for (QTreeWidgetItemIterator item(hierarchy); *item != nullptr; ++item) {
        if ((*item)->data(0, Qt::UserRole + 1).toString() == QStringLiteral("segment")) {
            (*item)->setSelected(true);
            ++selectedSegments;
        }
    }
    ASSERT_EQ(selectedSegments, 2U);
    auto* merge = window.findChild<QAction*>(QStringLiteral("mergeSegmentsAction"));
    ASSERT_NE(merge, nullptr);
    merge->trigger();
    button(window, "applyPreviewButton")->click();
    ASSERT_EQ(window.commandProcessor().current().project.roadSegments().size(), 1U);

    auto* reverse = window.findChild<QAction*>(QStringLiteral("reverseRoadAction"));
    ASSERT_NE(reverse, nullptr);
    reverse->trigger();
    button(window, "applyPreviewButton")->click();
    EXPECT_EQ(window.commandProcessor().current().project.roadSplines().front().direction, "end-to-start");
    window.findChild<QAction*>(QStringLiteral("undoAction"))->trigger();
    EXPECT_EQ(window.commandProcessor().current().project.roadSplines().front().direction, "start-to-end");
}

// Verifies numeric station measurement is keyboard-accessible and never changes authoritative source.
TEST_F(RoadAuthoringUiTest, MeasureStationsUsesExactValuesWithoutMutation) {
    atlas::ui::MainWindow window(atlas::domain::Project::empty("project", "root-map"));
    drawStraightRoad(window, 8);
    button(window, "applyPreviewButton")->click();
    const auto before = window.commandProcessor().current().normalizedSource();
    window.findChild<QAction*>(QStringLiteral("measureToolAction"))->trigger();
    auto* start = window.findChild<QDoubleSpinBox*>(QStringLiteral("measureStartStation"));
    auto* end = window.findChild<QDoubleSpinBox*>(QStringLiteral("measureEndStation"));
    ASSERT_NE(start, nullptr);
    ASSERT_NE(end, nullptr);
    start->setValue(1.25);
    end->setValue(6.75);
    button(window, "measureStationsButton")->click();

    EXPECT_NE(window.statusBar()->currentMessage().toStdString().find("5.500 m"), std::string::npos);
    EXPECT_EQ(window.commandProcessor().current().normalizedSource(), before);
}

// Verifies canvas measurement renders first, live, and completed feedback and Escape removes it without source edits.
TEST_F(RoadAuthoringUiTest, CanvasMeasurementOverlayIsLiveAndTransient) {
    atlas::ui::MainWindow window(atlas::domain::Project::empty("project", "root-map"));
    window.show();
    QApplication::processEvents();
    drawStraightRoad(window, 8);
    button(window, "applyPreviewButton")->click();
    const auto before = window.commandProcessor().current().normalizedSource();
    auto* canvas = window.canvas();
    canvas->setTool(atlas::ui::CanvasWidget::Tool::measure);
    const auto baseline = canvas->grabFramebuffer();
    const auto countMeasurePixels = [](const QImage& image) {
        std::size_t count = 0;
        for (int y = 0; y < image.height(); ++y) {
            for (int x = 0; x < image.width(); ++x) {
                const auto color = image.pixelColor(x, y);
                if (color.red() < 180 && color.green() > 180 && color.blue() > 150) ++count;
            }
        }
        return count;
    };
    const auto baselineMeasurePixels = countMeasurePixels(baseline);
    const auto start = canvas->camera().worldToScreen({1.0, 0.0});
    const auto end = canvas->camera().worldToScreen({6.0, 0.0});
    QMouseEvent first(QEvent::MouseButtonPress, QPointF(start.x, start.y),
        Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(canvas, &first);
    QMouseEvent move(QEvent::MouseMove, QPointF(end.x, end.y),
        Qt::NoButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(canvas, &move);
    QApplication::processEvents();
    const auto liveOverlay = canvas->grabFramebuffer();
    EXPECT_GT(countMeasurePixels(liveOverlay), baselineMeasurePixels);

    QMouseEvent second(QEvent::MouseButtonPress, QPointF(end.x, end.y),
        Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(canvas, &second);
    QApplication::processEvents();
    EXPECT_NE(window.statusBar()->currentMessage().toStdString().find("5.000 m"), std::string::npos);
    QKeyEvent cancel(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
    QApplication::sendEvent(canvas, &cancel);
    QApplication::processEvents();
    EXPECT_LT(countMeasurePixels(canvas->grabFramebuffer()), countMeasurePixels(liveOverlay));
    EXPECT_EQ(window.commandProcessor().current().normalizedSource(), before);
}

// Verifies split RoadSegments render and select as distinct station intervals with stable segment IDs.
TEST_F(RoadAuthoringUiTest, SplitSegmentsHaveDistinctCanvasSelection) {
    atlas::ui::MainWindow window(atlas::domain::Project::empty("project", "root-map"));
    window.show();
    QApplication::processEvents();
    drawStraightRoad(window, 8);
    button(window, "applyPreviewButton")->click();
    const auto singleSegmentFrame = window.canvas()->grabFramebuffer();
    window.canvas()->setTool(atlas::ui::CanvasWidget::Tool::split);
    const auto splitPoint = window.canvas()->camera().worldToScreen({4.0, 0.0});
    QMouseEvent split(QEvent::MouseButtonPress, QPointF(splitPoint.x, splitPoint.y),
        Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(window.canvas(), &split);
    button(window, "applyPreviewButton")->click();
    ASSERT_EQ(window.commandProcessor().current().project.roadSegments().size(), 2U);
    EXPECT_NE(window.canvas()->grabFramebuffer(), singleSegmentFrame);
    window.canvas()->setTool(atlas::ui::CanvasWidget::Tool::select);
    auto* summary = window.findChild<QLabel*>(QStringLiteral("objectInspectorSummary"));
    ASSERT_NE(summary, nullptr);
    QString firstSegment;
    for (const auto station : {2.0, 6.0}) {
        const auto point = window.canvas()->camera().worldToScreen({station, 0.0});
        QMouseEvent click(QEvent::MouseButtonPress, QPointF(point.x, point.y),
            Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(window.canvas(), &click);
        if (station == 2.0) {
            firstSegment = summary->text().section(QStringLiteral("Selected segment: "), 1);
            EXPECT_FALSE(firstSegment.isEmpty());
        } else {
            EXPECT_NE(summary->text().section(QStringLiteral("Selected segment: "), 1), firstSegment);
        }
    }
}

// Verifies Extend uses keyboard-editable endpoint values and remains previewable and undoable.
TEST_F(RoadAuthoringUiTest, ExtendEndpointPreviewCommitsAndUndoes) {
    atlas::ui::MainWindow window(atlas::domain::Project::empty("project", "root-map"));
    drawStraightRoad(window, 4);
    button(window, "applyPreviewButton")->click();
    const auto before = window.commandProcessor().current().normalizedSource();
    auto* endpointX = window.findChild<QDoubleSpinBox*>(QStringLiteral("roadEndpointX"));
    auto* endpointY = window.findChild<QDoubleSpinBox*>(QStringLiteral("roadEndpointY"));
    ASSERT_NE(endpointX, nullptr);
    ASSERT_NE(endpointY, nullptr);
    endpointX->setValue(6.0);
    endpointY->setValue(2.0);
    button(window, "extendRoadButton")->click();

    EXPECT_EQ(window.commandProcessor().current().normalizedSource(), before);
    button(window, "applyPreviewButton")->click();
    const auto& road = window.commandProcessor().current().project.roadSplines().front();
    EXPECT_DOUBLE_EQ(road.primitives.back()["end"]["x"], 6.0);
    EXPECT_DOUBLE_EQ(road.primitives.back()["end"]["y"], 2.0);
    window.findChild<QAction*>(QStringLiteral("undoAction"))->trigger();
    EXPECT_EQ(window.commandProcessor().current().normalizedSource(), before);
}

// Verifies shortening presents anchor-resolution choices and keeps source unchanged until Apply.
TEST_F(RoadAuthoringUiTest, ShortenEndpointRequiresResolutionAndPreview) {
    atlas::ui::MainWindow window(projectWithInteriorAnchor());
    auto* hierarchy = window.findChild<QTreeWidget*>(QStringLiteral("mapHierarchyWidget"));
    ASSERT_NE(hierarchy, nullptr);
    for (QTreeWidgetItemIterator item(hierarchy); *item != nullptr; ++item) {
        if ((*item)->data(0, Qt::UserRole + 1).toString() == QStringLiteral("road")) {
            hierarchy->setCurrentItem(*item);
            break;
        }
    }
    const auto before = window.commandProcessor().current().normalizedSource();
    window.findChild<QDoubleSpinBox*>(QStringLiteral("roadEndpointX"))->setValue(6.0);
    window.findChild<QDoubleSpinBox*>(QStringLiteral("roadEndpointY"))->setValue(0.0);
    QTimer::singleShot(0, [] {
        auto* dialog = qobject_cast<QInputDialog*>(QApplication::activeModalWidget());
        if (dialog != nullptr) {
            dialog->setTextValue(QStringLiteral("Move dependents"));
            dialog->accept();
        }
    });
    button(window, "shortenRoadButton")->click();

    EXPECT_EQ(window.commandProcessor().current().normalizedSource(), before);
    ASSERT_TRUE(button(window, "applyPreviewButton"));
    button(window, "applyPreviewButton")->click();
    EXPECT_NE(window.commandProcessor().current().normalizedSource(), before);
    const auto& anchors = window.commandProcessor().current().project.roadSplines().front().stationAnchors;
    const auto interior = std::find_if(anchors.begin(), anchors.end(),
        [](const auto& anchor) { return anchor.id == "anchor-interior"; });
    ASSERT_NE(interior, anchors.end());
    EXPECT_DOUBLE_EQ(interior->resolvedStation, 6.0);
}

// Verifies canceling shortening preserves an out-of-domain anchor and Delete removes it only after preview commit.
TEST_F(RoadAuthoringUiTest, ShortenCanCancelOrDeleteOutOfDomainAnchor) {
    atlas::ui::MainWindow window(projectWithInteriorAnchor());
    auto* hierarchy = window.findChild<QTreeWidget*>(QStringLiteral("mapHierarchyWidget"));
    ASSERT_NE(hierarchy, nullptr);
    for (QTreeWidgetItemIterator item(hierarchy); *item != nullptr; ++item) {
        if ((*item)->data(0, Qt::UserRole + 1).toString() == QStringLiteral("road")) {
            hierarchy->setCurrentItem(*item);
            break;
        }
    }
    window.findChild<QDoubleSpinBox*>(QStringLiteral("roadEndpointX"))->setValue(6.0);
    const auto before = window.commandProcessor().current().normalizedSource();
    const auto answerResolution = [](const QString& resolution) {
        QTimer::singleShot(0, [resolution]() {
            auto* dialog = qobject_cast<QInputDialog*>(QApplication::activeModalWidget());
            if (dialog != nullptr) {
                dialog->setTextValue(resolution);
                dialog->accept();
            }
        });
    };

    answerResolution(QStringLiteral("Cancel"));
    button(window, "shortenRoadButton")->click();
    EXPECT_EQ(window.commandProcessor().current().normalizedSource(), before);
    EXPECT_FALSE(window.findChild<QDockWidget*>(QStringLiteral("changesDiagnosticsDock"))->isVisible());

    answerResolution(QStringLiteral("Delete dependents"));
    button(window, "shortenRoadButton")->click();
    EXPECT_EQ(window.commandProcessor().current().normalizedSource(), before);
    button(window, "applyPreviewButton")->click();
    const auto& anchors = window.commandProcessor().current().project.roadSplines().front().stationAnchors;
    EXPECT_EQ(std::find_if(anchors.begin(), anchors.end(),
        [](const auto& anchor) { return anchor.id == "anchor-interior"; }), anchors.end());
    window.findChild<QAction*>(QStringLiteral("undoAction"))->trigger();
    EXPECT_EQ(window.commandProcessor().current().normalizedSource(), before);
}

// Verifies deleting a selected RoadSpline is previewed, cancellable, and undoable.
TEST_F(RoadAuthoringUiTest, DeleteRoadPreviewCanCancelAndUndo) {
    atlas::ui::MainWindow window(atlas::domain::Project::empty("project", "root-map"));
    drawStraightRoad(window);
    button(window, "applyPreviewButton")->click();
    ASSERT_EQ(window.commandProcessor().current().project.roadSplines().size(), 1U);
    auto* deleteAction = window.findChild<QAction*>(QStringLiteral("deleteRoadAction"));
    ASSERT_NE(deleteAction, nullptr);
    const auto answerConfirmation = [](QMessageBox::StandardButton answer) {
        QTimer::singleShot(0, [answer]() {
            auto* dialog = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
            if (dialog != nullptr && dialog->button(answer) != nullptr) dialog->button(answer)->click();
        });
    };
    answerConfirmation(QMessageBox::Cancel);
    deleteAction->trigger();
    EXPECT_EQ(window.commandProcessor().current().project.roadSplines().size(), 1U);
    answerConfirmation(QMessageBox::Yes);
    deleteAction->trigger();
    EXPECT_EQ(window.commandProcessor().current().project.roadSplines().size(), 1U);
    EXPECT_EQ(window.commandProcessor().current().project.roadSegments().size(), 1U);
    button(window, "cancelPreviewButton")->click();
    EXPECT_EQ(window.commandProcessor().current().project.roadSplines().size(), 1U);
    answerConfirmation(QMessageBox::Yes);
    deleteAction->trigger();
    ASSERT_TRUE(button(window, "applyPreviewButton")->isEnabled())
        << window.findChild<QLabel*>(QStringLiteral("roadPreviewSummary"))->text().toStdString();
    button(window, "applyPreviewButton")->click();
    EXPECT_TRUE(window.commandProcessor().current().project.roadSplines().empty());
    EXPECT_TRUE(window.commandProcessor().current().project.roadSegments().empty());
    window.findChild<QAction*>(QStringLiteral("undoAction"))->trigger();
    EXPECT_EQ(window.commandProcessor().current().project.roadSplines().size(), 1U);
}

// Verifies schema-2 road packages round-trip through the desktop entry points and schema-1 upgrades are explicit.
TEST_F(RoadAuthoringUiTest, SaveOpenRoundTripAndSchemaOneUpgradeRequireConsent) {
    QTemporaryDir temporary;
    ASSERT_TRUE(temporary.isValid());
    const auto schemaTwoPath = std::filesystem::path(temporary.path().toStdString()) / "roads.atlas";
    atlas::ui::MainWindow source(atlas::domain::Project::empty("project", "root-map"));
    drawStraightRoad(source);
    button(source, "applyPreviewButton")->click();
    ASSERT_TRUE(source.savePackage(schemaTwoPath));

    atlas::ui::MainWindow reopened(atlas::domain::Project::empty("other", "other-map"));
    ASSERT_TRUE(reopened.openPackage(schemaTwoPath));
    EXPECT_EQ(reopened.commandProcessor().current().project.normalizedJson(),
        source.commandProcessor().current().project.normalizedJson());

    const auto schemaOnePath = std::filesystem::path(temporary.path().toStdString()) / "legacy.atlas";
    atlas::persistence::SaveOptions schemaOneOptions;
    schemaOneOptions.schemaGeneration = 1;
    atlas::persistence::Package::fromProject(atlas::domain::Project::empty("legacy", "legacy-map"))
        .save(schemaOnePath, schemaOneOptions);
    atlas::ui::MainWindow legacy(atlas::domain::Project::empty("unused", "unused-map"));
    ASSERT_TRUE(legacy.openPackage(schemaOnePath));
    drawStraightRoad(legacy);
    button(legacy, "applyPreviewButton")->click();
    std::string error;
    EXPECT_FALSE(legacy.savePackage(schemaOnePath, false, &error));
    EXPECT_NE(error.find("explicit migration"), std::string::npos);
    EXPECT_EQ(nlohmann::json::parse(atlas::persistence::Package::load(schemaOnePath).manifestJson())["schemaVersion"], 1);
    ASSERT_TRUE(legacy.savePackage(schemaOnePath, true, &error)) << error;
    const auto migrated = atlas::persistence::Package::load(schemaOnePath);
    EXPECT_EQ(nlohmann::json::parse(migrated.manifestJson())["schemaVersion"], 2);
    EXPECT_EQ(migrated.project().roadSplines().size(), 1U);
}

// Verifies retained empty, minimal-road, and split-road fixtures round-trip through desktop open/save/reopen.
TEST_F(RoadAuthoringUiTest, RetainedCompatibilityFixturesRoundTripThroughDesktop) {
    QTemporaryDir temporary;
    ASSERT_TRUE(temporary.isValid());
    const std::filesystem::path sourceRoot(ATLAS_SOURCE_DIR);
    const auto emptyPackagePath = sourceRoot / "fixtures" / "compatibility" / "empty-project.atlas";
    atlas::ui::MainWindow emptyWindow(atlas::domain::Project::empty("unused", "unused-map"));
    ASSERT_TRUE(emptyWindow.openPackage(emptyPackagePath)) << emptyPackagePath.string();
    const auto emptySource = emptyWindow.commandProcessor().current().project.normalizedJson();
    const auto emptyResavePath = std::filesystem::path(temporary.path().toStdString()) / "empty-roundtrip.atlas";
    ASSERT_TRUE(emptyWindow.savePackage(emptyResavePath));
    atlas::ui::MainWindow emptyReopened(atlas::domain::Project::empty("other", "other-map"));
    ASSERT_TRUE(emptyReopened.openPackage(emptyResavePath));
    EXPECT_EQ(emptyReopened.commandProcessor().current().project.normalizedJson(), emptySource);

    for (const auto* fixtureName : {"minimal-road.json", "split-road.json"}) {
        const auto fixturePath = sourceRoot / "tests" / "fixtures" / "compatibility" / fixtureName;
        std::ifstream fixture(fixturePath, std::ios::binary);
        ASSERT_TRUE(fixture.good()) << fixturePath.string();
        const std::string fixtureText(std::istreambuf_iterator<char>(fixture), {});
        const auto fixtureProject = atlas::domain::Project::fromJson(fixtureText);
        const auto packagePath = std::filesystem::path(temporary.path().toStdString()) /
            (std::string(fixtureName) + ".atlas");
        atlas::persistence::Package::fromProject(fixtureProject).save(packagePath);
        atlas::ui::MainWindow opened(atlas::domain::Project::empty("unused", "unused-map"));
        ASSERT_TRUE(opened.openPackage(packagePath));
        ASSERT_TRUE(opened.savePackage(packagePath));
        atlas::ui::MainWindow reopened(atlas::domain::Project::empty("other", "other-map"));
        ASSERT_TRUE(reopened.openPackage(packagePath));
        EXPECT_EQ(reopened.commandProcessor().current().project.normalizedJson(), fixtureProject.normalizedJson())
            << fixtureName;
    }
}

} // namespace