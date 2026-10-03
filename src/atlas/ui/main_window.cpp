#include "atlas/ui/main_window.hpp"
#include "atlas/persistence/package.hpp"

#include <QAction>
#include <QActionGroup>
#include <QDialog>
#include <QDialogButtonBox>
#include <QCheckBox>
#include <QComboBox>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QInputDialog>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QStatusBar>
#include <QTreeWidgetItemIterator>
#include <QToolBar>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QUuid>

#include <algorithm>
#include <exception>
#include <limits>
#include <utility>

namespace atlas::ui {

namespace {

const domain::RoadSpline* findRoad(const domain::Project& project, const std::string& roadId) {
    const auto found = std::find_if(project.roadSplines().begin(), project.roadSplines().end(),
        [&](const auto& road) { return road.id == roadId; });
    return found == project.roadSplines().end() ? nullptr : &*found;
}

double stationForAnchor(const domain::RoadSpline& road, const std::string& anchorId) {
    const auto found = std::find_if(road.stationAnchors.begin(), road.stationAnchors.end(),
        [&](const auto& anchor) { return anchor.id == anchorId; });
    return found == road.stationAnchors.end() ? std::numeric_limits<double>::quiet_NaN() : found->resolvedStation;
}

std::pair<double, double> roadEnd(const domain::RoadSpline& road) {
    if (road.primitives.empty()) throw std::invalid_argument("RoadSpline has no primitives.");
    const auto& last = road.primitives.back();
    const auto kind = last.value("kind", "");
    if (kind == "cubic-bezier") {
        const auto& points = last.contains("controlPoints") ? last.at("controlPoints") : last.at("points");
        return {points.at(3).value("x", 0.0), points.at(3).value("y", 0.0)};
    }
    const auto& point = last.at("end");
    return {point.value("x", 0.0), point.value("y", 0.0)};
}

} // namespace

MainWindow::MainWindow(domain::Project project, QWidget* parent)
    : QMainWindow(parent), commandProcessor_({0, std::move(project), {}}) {
    setWindowTitle(QStringLiteral("Atlas"));
    resize(1280, 800);

    canvas_ = new CanvasWidget(this);
    canvas_->setProject(commandProcessor_.current().project);
    canvas_->setAccessibleName(QStringLiteral("Atlas map canvas"));
    setCentralWidget(canvas_);

    auto* hierarchyDock = new QDockWidget(QStringLiteral("Hierarchy"), this);
    hierarchyDock->setObjectName(QStringLiteral("hierarchyDock"));
    hierarchyDock->setAccessibleName(QStringLiteral("Road and map hierarchy"));
    hierarchy_ = new QTreeWidget(hierarchyDock);
    hierarchy_->setHeaderLabel(QStringLiteral("Map objects"));
    hierarchy_->setObjectName(QStringLiteral("mapHierarchyWidget"));
    hierarchy_->setAccessibleName(QStringLiteral("Map hierarchy"));
    hierarchy_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    hierarchyDock->setWidget(hierarchy_);
    addDockWidget(Qt::LeftDockWidgetArea, hierarchyDock);

    auto* inspectorDock = new QDockWidget(QStringLiteral("Inspector"), this);
    inspectorDock->setObjectName(QStringLiteral("inspectorDock"));
    inspectorDock->setAccessibleName(QStringLiteral("Selected road inspector"));
    inspectorPanel_ = new QWidget;
    inspectorPanel_->setMinimumWidth(0);
    inspectorPanel_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    auto* inspectorLayout = new QVBoxLayout(inspectorPanel_);
    inspector_ = new QLabel(QStringLiteral("No object selected"), inspectorPanel_);
    inspector_->setObjectName(QStringLiteral("objectInspectorSummary"));
    inspector_->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    inspector_->setWordWrap(true);
    inspector_->setAccessibleName(QStringLiteral("Object inspector"));
    inspectorLayout->addWidget(inspector_);
    controlPointSelector_ = new QComboBox(inspectorPanel_);
    controlPointSelector_->setObjectName(QStringLiteral("controlPointSelector"));
    controlPointSelector_->setAccessibleName(QStringLiteral("Road control point"));
    inspectorLayout->addWidget(controlPointSelector_);
    controlPointX_ = new QDoubleSpinBox(inspectorPanel_);
    controlPointX_->setObjectName(QStringLiteral("controlPointX"));
    controlPointX_->setRange(-1.0e9, 1.0e9);
    controlPointX_->setDecimals(3);
    controlPointX_->setSuffix(QStringLiteral(" m X"));
    controlPointX_->setAccessibleName(QStringLiteral("Control point X coordinate in meters"));
    inspectorLayout->addWidget(controlPointX_);
    controlPointY_ = new QDoubleSpinBox(inspectorPanel_);
    controlPointY_->setObjectName(QStringLiteral("controlPointY"));
    controlPointY_->setRange(-1.0e9, 1.0e9);
    controlPointY_->setDecimals(3);
    controlPointY_->setSuffix(QStringLiteral(" m Y"));
    controlPointY_->setAccessibleName(QStringLiteral("Control point Y coordinate in meters"));
    inspectorLayout->addWidget(controlPointY_);
    endpointX_ = new QDoubleSpinBox(inspectorPanel_);
    endpointX_->setObjectName(QStringLiteral("roadEndpointX"));
    endpointX_->setRange(-1.0e9, 1.0e9);
    endpointX_->setDecimals(3);
    endpointX_->setSuffix(QStringLiteral(" m end X"));
    endpointX_->setAccessibleName(QStringLiteral("Road endpoint X coordinate in meters"));
    inspectorLayout->addWidget(endpointX_);
    endpointY_ = new QDoubleSpinBox(inspectorPanel_);
    endpointY_->setObjectName(QStringLiteral("roadEndpointY"));
    endpointY_->setRange(-1.0e9, 1.0e9);
    endpointY_->setDecimals(3);
    endpointY_->setSuffix(QStringLiteral(" m end Y"));
    endpointY_->setAccessibleName(QStringLiteral("Road endpoint Y coordinate in meters"));
    inspectorLayout->addWidget(endpointY_);
    auto* editPointButton = new QPushButton(QStringLiteral("Preview point edit"), inspectorPanel_);
    editPointButton->setObjectName(QStringLiteral("previewControlPointEditButton"));
    editPointButton->setAccessibleName(QStringLiteral("Preview control point coordinate edit"));
    inspectorLayout->addWidget(editPointButton);
    auto* reverseButton = new QPushButton(QStringLiteral("Reverse"), inspectorPanel_);
    reverseButton->setAccessibleName(QStringLiteral("Reverse selected road"));
    auto* extendButton = new QPushButton(QStringLiteral("Extend end"), inspectorPanel_);
    extendButton->setObjectName(QStringLiteral("extendRoadButton"));
    extendButton->setAccessibleName(QStringLiteral("Extend selected road end"));
    auto* shortenButton = new QPushButton(QStringLiteral("Shorten end"), inspectorPanel_);
    shortenButton->setObjectName(QStringLiteral("shortenRoadButton"));
    shortenButton->setAccessibleName(QStringLiteral("Shorten selected road end"));
    auto* deleteButton = new QPushButton(QStringLiteral("Delete road"), inspectorPanel_);
    deleteButton->setAccessibleName(QStringLiteral("Delete selected road"));
    auto* splitButton = new QPushButton(QStringLiteral("Split segment"), inspectorPanel_);
    splitButton->setAccessibleName(QStringLiteral("Split selected road segment"));
    boundaryStation_ = new QDoubleSpinBox(inspectorPanel_);
    boundaryStation_->setObjectName(QStringLiteral("boundaryStation"));
    boundaryStation_->setRange(0.0, 1000000000.0);
    boundaryStation_->setDecimals(3);
    boundaryStation_->setSuffix(QStringLiteral(" m boundary station"));
    boundaryStation_->setAccessibleName(QStringLiteral("New RoadSegment boundary station in meters"));
    auto* boundaryButton = new QPushButton(QStringLiteral("Move boundary"), inspectorPanel_);
    boundaryButton->setObjectName(QStringLiteral("moveBoundaryButton"));
    boundaryButton->setAccessibleName(QStringLiteral("Move selected segment boundary"));
    auto* mergeButton = new QPushButton(QStringLiteral("Merge segments"), inspectorPanel_);
    mergeButton->setObjectName(QStringLiteral("mergeSegmentsButton"));
    mergeButton->setAccessibleName(QStringLiteral("Merge selected adjacent segments"));
    inspectorLayout->addWidget(reverseButton);
    inspectorLayout->addWidget(extendButton);
    inspectorLayout->addWidget(shortenButton);
    inspectorLayout->addWidget(deleteButton);
    inspectorLayout->addWidget(splitButton);
    inspectorLayout->addWidget(boundaryStation_);
    inspectorLayout->addWidget(boundaryButton);
    inspectorLayout->addWidget(mergeButton);
    inspectorLayout->addStretch(1);
    auto* inspectorScroll = new QScrollArea(inspectorDock);
    inspectorScroll->setObjectName(QStringLiteral("inspectorScrollArea"));
    inspectorScroll->setWidgetResizable(true);
    inspectorScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    inspectorScroll->setWidget(inspectorPanel_);
    inspectorDock->setWidget(inspectorScroll);
    addDockWidget(Qt::RightDockWidgetArea, inspectorDock);
    resizeDocks({hierarchyDock, inspectorDock}, {240, 300}, Qt::Horizontal);
    for (auto* child : inspectorPanel_->findChildren<QWidget*>(QString(), Qt::FindDirectChildrenOnly)) {
        auto policy = child->sizePolicy();
        policy.setHorizontalPolicy(QSizePolicy::Ignored);
        child->setSizePolicy(policy);
        child->setMinimumWidth(0);
    }

    auto* fileMenu = menuBar()->addMenu(QStringLiteral("File"));
    fileMenu->setAccessibleName(QStringLiteral("File menu"));
    auto* newAction = fileMenu->addAction(QStringLiteral("New project"));
    newAction->setObjectName(QStringLiteral("newProjectAction"));
    newAction->setShortcut(QKeySequence::New);
    auto* openAction = fileMenu->addAction(QStringLiteral("Open package..."));
    openAction->setShortcut(QKeySequence::Open);
    auto* saveAction = fileMenu->addAction(QStringLiteral("Save"));
    saveAction->setShortcut(QKeySequence::Save);
    auto* saveAsAction = fileMenu->addAction(QStringLiteral("Save As..."));
    saveAsAction->setShortcut(QKeySequence::SaveAs);
    auto* editMenu = menuBar()->addMenu(QStringLiteral("Edit"));
    editMenu->setAccessibleName(QStringLiteral("Edit menu"));

    auto* toolbar = addToolBar(QStringLiteral("Primary tools"));
    toolbar->setObjectName(QStringLiteral("primaryToolbar"));
    toolbar->setMovable(false);
    toolbar->setAccessibleName(QStringLiteral("Primary tools"));

    auto* selectAction = toolbar->addAction(QStringLiteral("Select"));
    selectAction->setObjectName(QStringLiteral("selectToolAction"));
    selectAction->setShortcut(QKeySequence(QStringLiteral("V")));
    selectAction->setShortcutContext(Qt::WidgetShortcut);
    auto* roadAction = toolbar->addAction(QStringLiteral("Road"));
    roadAction->setObjectName(QStringLiteral("roadToolAction"));
    roadAction->setShortcut(QKeySequence(QStringLiteral("R")));
    roadAction->setShortcutContext(Qt::WidgetShortcut);
    auto* editAction = toolbar->addAction(QStringLiteral("Edit"));
    editAction->setObjectName(QStringLiteral("editRoadToolAction"));
    editAction->setShortcut(QKeySequence(QStringLiteral("E")));
    editAction->setShortcutContext(Qt::WidgetShortcut);
    auto* splitAction = toolbar->addAction(QStringLiteral("Split"));
    splitAction->setObjectName(QStringLiteral("splitRoadToolAction"));
    splitAction->setShortcut(QKeySequence(QStringLiteral("S")));
    splitAction->setShortcutContext(Qt::WidgetShortcut);
    auto* measureAction = toolbar->addAction(QStringLiteral("Measure"));
    measureAction->setObjectName(QStringLiteral("measureToolAction"));
    measureAction->setShortcut(QKeySequence(QStringLiteral("M")));
    measureAction->setShortcutContext(Qt::WidgetShortcut);
    auto* toolGroup = new QActionGroup(toolbar);
    toolGroup->setExclusive(true);
    for (auto* action : {selectAction, roadAction, editAction, splitAction, measureAction}) {
        action->setCheckable(true);
        toolGroup->addAction(action);
    }
    selectAction->setChecked(true);
    zoomReadout_ = new QLabel(QStringLiteral("Zoom: 100%"), toolbar);
    zoomReadout_->setObjectName(QStringLiteral("zoomReadout"));
    zoomReadout_->setAccessibleName(QStringLiteral("Canvas zoom level"));
    toolbar->addWidget(zoomReadout_);

    transformToolbar_ = addToolBar(QStringLiteral("Transform space"));
    transformToolbar_->setObjectName(QStringLiteral("transformSpaceToolbar"));
    transformToolbar_->setMovable(false);
    auto* transformSpaceGroup = new QActionGroup(transformToolbar_);
    transformSpaceGroup->setExclusive(true);
    worldTransformAction_ = transformToolbar_->addAction(QStringLiteral("World"));
    worldTransformAction_->setObjectName(QStringLiteral("worldTransformSpaceAction"));
    worldTransformAction_->setCheckable(true);
    localTransformAction_ = transformToolbar_->addAction(QStringLiteral("Local"));
    localTransformAction_->setObjectName(QStringLiteral("localTransformSpaceAction"));
    localTransformAction_->setCheckable(true);
    localTransformAction_->setEnabled(false);
    transformSpaceGroup->addAction(worldTransformAction_);
    transformSpaceGroup->addAction(localTransformAction_);
    worldTransformAction_->setChecked(true);
    transformToolbar_->hide();

    roadContextToolbar_ = addToolBar(QStringLiteral("Road parameters"));
    roadContextToolbar_->setObjectName(QStringLiteral("roadContextToolbar"));
    roadContextToolbar_->setMovable(false);
    primitiveMode_ = new QComboBox(roadContextToolbar_);
    primitiveMode_->setObjectName(QStringLiteral("roadPrimitiveMode"));
    primitiveMode_->addItems({QStringLiteral("Polyline"), QStringLiteral("Bezier"),
        QStringLiteral("Fixed-radius arc")});
    primitiveMode_->setAccessibleName(QStringLiteral("Road primitive mode"));
    roadContextToolbar_->addWidget(primitiveMode_);
    gridSnapToggle_ = new QCheckBox(QStringLiteral("Snap to 1 m grid"), roadContextToolbar_);
    gridSnapToggle_->setObjectName(QStringLiteral("roadGridSnapToggle"));
    gridSnapToggle_->setChecked(true);
    gridSnapToggle_->setAccessibleName(QStringLiteral("Snap road draft points to one meter grid"));
    roadContextToolbar_->addWidget(gridSnapToggle_);
    radiusEditor_ = new QDoubleSpinBox(roadContextToolbar_);
    radiusEditor_->setObjectName(QStringLiteral("fixedRadiusEditor"));
    radiusEditor_->setRange(0.001, 1000000000.0);
    radiusEditor_->setDecimals(3);
    radiusEditor_->setValue(20.0);
    radiusEditor_->setSuffix(QStringLiteral(" m radius"));
    radiusEditor_->setAccessibleName(QStringLiteral("Fixed-radius arc radius in meters"));
    radiusEditor_->setVisible(false);
    roadContextToolbar_->addWidget(radiusEditor_);
    widthEditor_ = new QDoubleSpinBox(roadContextToolbar_);
    widthEditor_->setRange(0.1, 1000000.0);
    widthEditor_->setDecimals(3);
    widthEditor_->setValue(8.0);
    widthEditor_->setSuffix(QStringLiteral(" m total width"));
    widthEditor_->setAccessibleName(QStringLiteral("New road total width in meters"));
    roadContextToolbar_->addWidget(widthEditor_);
    measureStart_ = new QDoubleSpinBox(roadContextToolbar_);
    measureStart_->setObjectName(QStringLiteral("measureStartStation"));
    measureStart_->setRange(0.0, 1000000000.0);
    measureStart_->setDecimals(3);
    measureStart_->setSuffix(QStringLiteral(" m start"));
    measureStart_->setAccessibleName(QStringLiteral("Measure start station in meters"));
    roadContextToolbar_->addWidget(measureStart_);
    measureEnd_ = new QDoubleSpinBox(roadContextToolbar_);
    measureEnd_->setObjectName(QStringLiteral("measureEndStation"));
    measureEnd_->setRange(0.0, 1000000000.0);
    measureEnd_->setDecimals(3);
    measureEnd_->setSuffix(QStringLiteral(" m end"));
    measureEnd_->setAccessibleName(QStringLiteral("Measure end station in meters"));
    roadContextToolbar_->addWidget(measureEnd_);
    measureButton_ = new QPushButton(QStringLiteral("Measure stations"), roadContextToolbar_);
    measureButton_->setObjectName(QStringLiteral("measureStationsButton"));
    measureButton_->setAccessibleName(QStringLiteral("Measure distance between road stations"));
    roadContextToolbar_->addWidget(measureButton_);
    finishRoadButton_ = new QPushButton(QStringLiteral("Finish road"), roadContextToolbar_);
    finishRoadButton_->setObjectName(QStringLiteral("finishRoadDraftButton"));
    finishRoadButton_->setAccessibleName(QStringLiteral("Finish current road draft"));
    roadContextToolbar_->addWidget(finishRoadButton_);
    measureStart_->hide();
    measureEnd_->hide();
    measureButton_->hide();
    finishRoadButton_->hide();
    roadContextToolbar_->hide();

    changesDock_ = new QDockWidget(QStringLiteral("Changes & Diagnostics"), this);
    changesDock_->setObjectName(QStringLiteral("changesDiagnosticsDock"));
    changesDock_->setAccessibleName(QStringLiteral("Changes and diagnostics"));
    auto* changesPanel = new QWidget(changesDock_);
    auto* changesLayout = new QVBoxLayout(changesPanel);
    previewSummary_ = new QLabel(changesPanel);
    previewSummary_->setObjectName(QStringLiteral("roadPreviewSummary"));
    previewSummary_->setWordWrap(true);
    previewSummary_->setAccessibleName(QStringLiteral("Command preview and diagnostics"));
    changesLayout->addWidget(previewSummary_);
    auto* previewButtons = new QWidget(changesPanel);
    auto* previewButtonLayout = new QHBoxLayout(previewButtons);
    applyButton_ = new QPushButton(QStringLiteral("Apply"), previewButtons);
    applyButton_->setObjectName(QStringLiteral("applyPreviewButton"));
    applyButton_->setAccessibleName(QStringLiteral("Apply previewed road change"));
    auto* cancelButton = new QPushButton(QStringLiteral("Cancel"), previewButtons);
    cancelButton->setObjectName(QStringLiteral("cancelPreviewButton"));
    cancelButton->setAccessibleName(QStringLiteral("Cancel previewed road change"));
    previewButtonLayout->addWidget(applyButton_);
    previewButtonLayout->addWidget(cancelButton);
    changesLayout->addWidget(previewButtons);
    changesDock_->setWidget(changesPanel);
    addDockWidget(Qt::BottomDockWidgetArea, changesDock_);
    resizeDocks({changesDock_}, {180}, Qt::Vertical);
    changesDock_->hide();

    undoAction_ = editMenu->addAction(QStringLiteral("Undo"));
    undoAction_->setObjectName(QStringLiteral("undoAction"));
    undoAction_->setShortcut(QKeySequence::Undo);
    redoAction_ = editMenu->addAction(QStringLiteral("Redo"));
    redoAction_->setObjectName(QStringLiteral("redoAction"));
    redoAction_->setShortcut(QKeySequence::Redo);
    toolbar->addAction(undoAction_);
    toolbar->addAction(redoAction_);

    connect(undoAction_, &QAction::triggered, this, [this]() {
        if (commandProcessor_.undo()) {
            if (!undoSelection_.empty()) {
                redoSelection_.push_back(selectedRoadId_);
                selectedRoadId_ = undoSelection_.back();
                undoSelection_.pop_back();
            }
            refreshProjectViews();
        }
        updateHistoryActions();
    });
    connect(redoAction_, &QAction::triggered, this, [this]() {
        if (commandProcessor_.redo()) {
            if (!redoSelection_.empty()) {
                undoSelection_.push_back(selectedRoadId_);
                selectedRoadId_ = redoSelection_.back();
                redoSelection_.pop_back();
            }
            refreshProjectViews();
        }
        updateHistoryActions();
    });
    connect(newAction, &QAction::triggered, this, [this]() { newProject(); });
    connect(openAction, &QAction::triggered, this, [this]() { openFromDialog(); });
    connect(saveAction, &QAction::triggered, this, [this]() { saveFromDialog(); });
    connect(saveAsAction, &QAction::triggered, this, [this]() { saveAsFromDialog(); });
    connect(selectAction, &QAction::triggered, this, [this]() {
        canvas_->setTool(CanvasWidget::Tool::select);
        setFocus(Qt::ShortcutFocusReason);
    });
    connect(roadAction, &QAction::triggered, this, [this]() {
        canvas_->setTool(CanvasWidget::Tool::road);
        canvas_->setFocus(Qt::ShortcutFocusReason);
    });
    connect(editAction, &QAction::triggered, this, [this]() {
        canvas_->setTool(CanvasWidget::Tool::edit);
        canvas_->setFocus(Qt::ShortcutFocusReason);
    });
    connect(splitAction, &QAction::triggered, this, [this]() {
        canvas_->setTool(CanvasWidget::Tool::split);
        canvas_->setFocus(Qt::ShortcutFocusReason);
    });
    connect(measureAction, &QAction::triggered, this, [this]() {
        canvas_->setTool(CanvasWidget::Tool::measure);
        canvas_->setFocus(Qt::ShortcutFocusReason);
    });
    connect(primitiveMode_, &QComboBox::currentIndexChanged, this, [this](int index) {
        canvas_->setRoadDrawingMode(index == 1 ? CanvasWidget::RoadDrawingMode::bezier
            : index == 2 ? CanvasWidget::RoadDrawingMode::fixedRadiusArc
                         : CanvasWidget::RoadDrawingMode::polyline);
        radiusEditor_->setVisible(index == 2);
    });
    connect(gridSnapToggle_, &QCheckBox::toggled, canvas_, [this](bool enabled) {
        canvas_->setRoadSnapToGrid(enabled);
    });
    connect(radiusEditor_, &QDoubleSpinBox::valueChanged, this,
        [this](double value) { canvas_->setArcRadiusMeters(value); });
    connect(widthEditor_, &QDoubleSpinBox::valueChanged, this,
        [this](double value) { canvas_->setRoadWidthMeters(value); });
    connect(applyButton_, &QPushButton::clicked, this, [this]() { applyPreview(); });
    connect(cancelButton, &QPushButton::clicked, this, [this]() { cancelPreview(); });
    canvas_->setRoadCreatedHandler([this](domain::RoadSpline road) {
        pendingSelectionId_ = road.id;
        previewCommand(std::make_unique<application::CreateRoadSplineCommand>(
            std::move(road), commandProcessor_.current().number, widthEditor_->value()));
    });
    canvas_->setRoadEditedHandler([this](domain::RoadSpline road) {
        editRoadWithAnchorResolution(std::move(road), true, false);
    });
    canvas_->setMapObjectEditedHandler([this](domain::MapObject object) {
        previewCommand(std::make_unique<application::EditMapObjectCommand>(
            std::move(object), commandProcessor_.current().number, false));
        applyPreview();
    });
    canvas_->setRoadSelectedHandler([this](std::string roadId) {
        selectedSegmentId_.clear();
        canvas_->setSelectedSegment({});
        selectRoad(roadId);
    });
    canvas_->setSegmentSelectedHandler([this](std::string roadId, std::string segmentId) {
        selectedSegmentId_ = std::move(segmentId);
        selectRoad(roadId);
        canvas_->setSelectedSegment(selectedSegmentId_);
        updateInspector();
    });
    canvas_->setRoadStationHandler([this](std::string roadId, double station) {
        splitAtStation(roadId, station);
    });
    canvas_->setStatusHandler([this](QString message) { statusBar()->showMessage(std::move(message)); });
    canvas_->setSelectionChangedHandler([this](bool roadSelected) {
        localTransformAction_->setEnabled(roadSelected);
        if (!roadSelected) canvas_->setTransformSpace(CanvasWidget::TransformSpace::world);
        updateTransformSpaceControls();
    });
    canvas_->setToolChangedHandler([this, selectAction, roadAction, editAction, splitAction, measureAction](
        CanvasWidget::Tool tool) {
        selectAction->setChecked(tool == CanvasWidget::Tool::select);
        roadAction->setChecked(tool == CanvasWidget::Tool::road);
        editAction->setChecked(tool == CanvasWidget::Tool::edit);
        splitAction->setChecked(tool == CanvasWidget::Tool::split);
        measureAction->setChecked(tool == CanvasWidget::Tool::measure);
        transformToolbar_->setVisible(tool == CanvasWidget::Tool::select || tool == CanvasWidget::Tool::edit);
        const auto creating = tool == CanvasWidget::Tool::road;
        const auto measuring = tool == CanvasWidget::Tool::measure;
        roadContextToolbar_->setVisible(creating || measuring);
        primitiveMode_->setVisible(creating);
        gridSnapToggle_->setVisible(creating);
        widthEditor_->setVisible(creating);
        radiusEditor_->setVisible(creating && primitiveMode_->currentIndex() == 2);
        measureStart_->setVisible(measuring);
        measureEnd_->setVisible(measuring);
        measureButton_->setVisible(measuring);
        finishRoadButton_->setVisible(creating);
    });
    canvas_->setViewChangedHandler([this](double zoom) {
        zoomReadout_->setText(QStringLiteral("Zoom: %1%").arg(zoom * 100.0, 0, 'f', 0));
    });

    auto* roadMenu = menuBar()->addMenu(QStringLiteral("Road"));
    roadMenu->setAccessibleName(QStringLiteral("Road commands"));
    auto* reverseAction = roadMenu->addAction(QStringLiteral("Reverse selected road"));
    reverseAction->setObjectName(QStringLiteral("reverseRoadAction"));
    auto* extendAction = roadMenu->addAction(QStringLiteral("Extend selected road"));
    auto* shortenAction = roadMenu->addAction(QStringLiteral("Shorten selected road"));
    auto* deleteAction = roadMenu->addAction(QStringLiteral("Delete selected road"));
    deleteAction->setObjectName(QStringLiteral("deleteRoadAction"));
    auto* boundaryAction = roadMenu->addAction(QStringLiteral("Move selected segment boundary"));
    auto* mergeAction = roadMenu->addAction(QStringLiteral("Merge selected adjacent segments"));
    mergeAction->setObjectName(QStringLiteral("mergeSegmentsAction"));
    connect(reverseAction, &QAction::triggered, this, [this]() { reverseSelectedRoad(); });
    connect(extendAction, &QAction::triggered, this, [this]() { extendSelectedRoad(); });
    connect(shortenAction, &QAction::triggered, this, [this]() { shortenSelectedRoad(); });
    connect(deleteAction, &QAction::triggered, this, [this]() { deleteSelectedRoad(); });
    connect(boundaryAction, &QAction::triggered, this, [this]() { moveSelectedBoundary(); });
    connect(mergeAction, &QAction::triggered, this, [this]() { mergeSelectedSegments(); });
    connect(reverseButton, &QPushButton::clicked, reverseAction, &QAction::trigger);
    connect(extendButton, &QPushButton::clicked, extendAction, &QAction::trigger);
    connect(shortenButton, &QPushButton::clicked, shortenAction, &QAction::trigger);
    connect(deleteButton, &QPushButton::clicked, deleteAction, &QAction::trigger);
    connect(boundaryButton, &QPushButton::clicked, boundaryAction, &QAction::trigger);
    connect(mergeButton, &QPushButton::clicked, mergeAction, &QAction::trigger);
    auto* viewMenu = menuBar()->addMenu(QStringLiteral("View"));
    auto* fitSelectionAction = viewMenu->addAction(QStringLiteral("Fit selection"));
    fitSelectionAction->setObjectName(QStringLiteral("fitSelectionAction"));
    fitSelectionAction->setShortcut(QKeySequence(QStringLiteral("Shift+F")));
    connect(fitSelectionAction, &QAction::triggered, canvas_, [this]() { canvas_->fitSelection(); });
    connect(worldTransformAction_, &QAction::triggered, this, [this]() {
        canvas_->setTransformSpace(CanvasWidget::TransformSpace::world);
        updateTransformSpaceControls();
    });
    connect(localTransformAction_, &QAction::triggered, this, [this]() {
        canvas_->setTransformSpace(CanvasWidget::TransformSpace::local);
        updateTransformSpaceControls();
    });
    auto* fitRoadAction = viewMenu->addAction(QStringLiteral("Fit selected road"));
    fitRoadAction->setObjectName(QStringLiteral("fitSelectedRoadAction"));
    fitRoadAction->setShortcut(QKeySequence(QStringLiteral("F")));
    connect(fitRoadAction, &QAction::triggered, this, [this]() {
        if (!selectedRoadId_.empty()) canvas_->fitRoad(selectedRoadId_);
    });
    viewMenu->addAction(hierarchyDock->toggleViewAction());
    viewMenu->addAction(inspectorDock->toggleViewAction());
    viewMenu->addAction(changesDock_->toggleViewAction());
    viewMenu->addAction(roadContextToolbar_->toggleViewAction());
    viewMenu->addAction(transformToolbar_->toggleViewAction());
    auto* toolsMenu = menuBar()->addMenu(QStringLiteral("Tools"));
    toolsMenu->addAction(selectAction);
    toolsMenu->addAction(roadAction);
    toolsMenu->addAction(editAction);
    toolsMenu->addAction(splitAction);
    toolsMenu->addAction(measureAction);
    auto* windowMenu = menuBar()->addMenu(QStringLiteral("Window"));
    windowMenu->addAction(hierarchyDock->toggleViewAction());
    windowMenu->addAction(inspectorDock->toggleViewAction());
    windowMenu->addAction(changesDock_->toggleViewAction());
    connect(controlPointSelector_, &QComboBox::currentIndexChanged, this, [this](int index) {
        if (index < 0) return;
        controlPointX_->setValue(controlPointSelector_->itemData(index, Qt::UserRole + 1).toDouble());
        controlPointY_->setValue(controlPointSelector_->itemData(index, Qt::UserRole + 2).toDouble());
    });
    connect(editPointButton, &QPushButton::clicked, this, [this]() { previewControlPointEdit(); });
    connect(measureButton_, &QPushButton::clicked, this, [this]() {
        if (selectedRoadId_.empty()) {
            statusBar()->showMessage(QStringLiteral("Select a RoadSpline to measure stations."));
            return;
        }
        statusBar()->showMessage(QStringLiteral("Road distance: %1 m")
            .arg(std::abs(measureEnd_->value() - measureStart_->value()), 0, 'f', 3));
    });
    connect(finishRoadButton_, &QPushButton::clicked, canvas_, [this]() {
        canvas_->finishRoadDrawing();
    });
    connect(splitButton, &QPushButton::clicked, this, [this]() {
        if (selectedRoadId_.empty()) return;
        bool accepted = false;
        const auto station = QInputDialog::getDouble(this, QStringLiteral("Split RoadSegment"),
            QStringLiteral("Station in meters"), 0.0, 0.0, 1000000000.0, 3, &accepted);
        if (accepted) splitAtStation(selectedRoadId_, station);
    });
    connect(hierarchy_, &QTreeWidget::currentItemChanged, this,
        [this](QTreeWidgetItem* current, QTreeWidgetItem*) {
            if (current == nullptr) return;
            const auto id = current->data(0, Qt::UserRole).toString().toStdString();
            const auto kind = current->data(0, Qt::UserRole + 1).toString();
            if (kind == QStringLiteral("road")) {
                selectedSegmentId_.clear();
                canvas_->setSelectedSegment({});
                selectRoad(id);
            } else if (kind == QStringLiteral("segment")) {
                selectedSegmentId_ = id;
                canvas_->setSelectedSegment(id);
                const auto& project = commandProcessor_.current().project;
                const auto segment = std::find_if(project.roadSegments().begin(), project.roadSegments().end(),
                    [&](const auto& item) { return item.id == id; });
                if (segment != project.roadSegments().end()) selectRoad(segment->roadSplineId);
            }
        });

    auto* commandPalette = menuBar()->addMenu(QStringLiteral("Command Palette"));
    commandPalette->setAccessibleName(QStringLiteral("Command palette"));
    auto* focusCanvas = commandPalette->addAction(QStringLiteral("Focus canvas"));
    focusCanvas->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+P")));
    connect(focusCanvas, &QAction::triggered, canvas_, [this]() {
        canvas_->setFocus(Qt::ShortcutFocusReason);
    });
    auto* activateRoad = commandPalette->addAction(QStringLiteral("Draw road"));
    connect(activateRoad, &QAction::triggered, roadAction, &QAction::trigger);
    auto* activateEdit = commandPalette->addAction(QStringLiteral("Edit selected road"));
    connect(activateEdit, &QAction::triggered, editAction, &QAction::trigger);
    auto* activateSplit = commandPalette->addAction(QStringLiteral("Split RoadSegment"));
    connect(activateSplit, &QAction::triggered, splitAction, &QAction::trigger);
    auto* activateMeasure = commandPalette->addAction(QStringLiteral("Measure road stations"));
    connect(activateMeasure, &QAction::triggered, measureAction, &QAction::trigger);
    auto* activateReverse = commandPalette->addAction(QStringLiteral("Reverse selected road"));
    connect(activateReverse, &QAction::triggered, reverseAction, &QAction::trigger);
    auto* activateExtend = commandPalette->addAction(QStringLiteral("Extend selected road"));
    connect(activateExtend, &QAction::triggered, extendAction, &QAction::trigger);
    auto* activateShorten = commandPalette->addAction(QStringLiteral("Shorten selected road"));
    connect(activateShorten, &QAction::triggered, shortenAction, &QAction::trigger);
    auto* activateDelete = commandPalette->addAction(QStringLiteral("Delete selected road"));
    connect(activateDelete, &QAction::triggered, deleteAction, &QAction::trigger);
    auto* activateMerge = commandPalette->addAction(QStringLiteral("Merge adjacent segments"));
    connect(activateMerge, &QAction::triggered, mergeAction, &QAction::trigger);
    auto* activateBoundaryMove = commandPalette->addAction(QStringLiteral("Move segment boundary"));
    connect(activateBoundaryMove, &QAction::triggered, boundaryAction, &QAction::trigger);
    auto* cancelAction = new QAction(QStringLiteral("Cancel current draft or preview"), this);
    cancelAction->setShortcut(QKeySequence(Qt::Key_Escape));
    cancelAction->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    addAction(cancelAction);
    connect(cancelAction, &QAction::triggered, this, [this]() {
        if (pendingPreview_) cancelPreview();
        else if (canvas_->tool() != CanvasWidget::Tool::select) {
            canvas_->setTool(CanvasWidget::Tool::select);
        }
    });

    statusBar()->showMessage(QStringLiteral("Coordinates: 0.000 m | Level: Ground | Layer: Default"));
    savedSource_ = commandProcessor_.current().project.normalizedJson(schemaGeneration_);
    refreshProjectViews();
    updateHistoryActions();
}

const application::CommandProcessor& MainWindow::commandProcessor() const noexcept {
    return commandProcessor_;
}

CanvasWidget* MainWindow::canvas() const noexcept {
    return canvas_;
}

void MainWindow::refreshProjectViews() {
    const auto& project = commandProcessor_.current().project;
    const auto previousCanvasSelection = canvas_->selection().primaryId();
    QSignalBlocker blocker(hierarchy_);
    canvas_->setProject(project);
    hierarchy_->clear();
    auto* projectItem = new QTreeWidgetItem(hierarchy_, {QStringLiteral("Project")});
    projectItem->setData(0, Qt::UserRole, QString::fromStdString(project.id()));
    auto* mapItem = new QTreeWidgetItem(projectItem, {QStringLiteral("Map: %1")
        .arg(QString::fromStdString(project.rootMap().id))});
    mapItem->setData(0, Qt::UserRole, QString::fromStdString(project.rootMap().id));
    for (const auto& road : project.roadSplines()) {
        auto* roadItem = new QTreeWidgetItem(mapItem, {QStringLiteral("RoadSpline: %1")
            .arg(QString::fromStdString(road.id))});
        roadItem->setData(0, Qt::UserRole, QString::fromStdString(road.id));
        roadItem->setData(0, Qt::UserRole + 1, QStringLiteral("road"));
        for (const auto& segmentId : road.segmentIds) {
            const auto segment = std::find_if(project.roadSegments().begin(), project.roadSegments().end(),
                [&](const auto& candidate) { return candidate.id == segmentId; });
            auto label = QStringLiteral("RoadSegment: %1").arg(QString::fromStdString(segmentId));
            if (segment != project.roadSegments().end()) {
                label += QStringLiteral(" [%1-%2 m]")
                    .arg(stationForAnchor(road, segment->startAnchorId), 0, 'f', 3)
                    .arg(stationForAnchor(road, segment->endAnchorId), 0, 'f', 3);
            }
            auto* segmentItem = new QTreeWidgetItem(roadItem, {label});
            segmentItem->setData(0, Qt::UserRole, QString::fromStdString(segmentId));
            segmentItem->setData(0, Qt::UserRole + 1, QStringLiteral("segment"));
        }
    }
    hierarchy_->expandAll();
    if (findRoad(project, selectedRoadId_) == nullptr) {
        selectedRoadId_.clear();
        selectedSegmentId_.clear();
    }
    canvas_->selectId(selectedRoadId_.empty() ? previousCanvasSelection : selectedRoadId_);
    canvas_->setSelectedSegment(selectedSegmentId_);
    updateInspector();
    const auto title = packagePath_.empty() ? QStringLiteral("Atlas")
        : QStringLiteral("Atlas — %1").arg(QString::fromStdString(packagePath_.string()));
    setWindowTitle(title + (isDirty() ? QStringLiteral(" *") : QString()));
}

void MainWindow::updateHistoryActions() {
    undoAction_->setEnabled(commandProcessor_.canUndo());
    redoAction_->setEnabled(commandProcessor_.canRedo());
}

void MainWindow::updateTransformSpaceControls() {
    const auto* selectedRoad = findRoad(
        commandProcessor_.current().project, canvas_->selection().primaryId());
    localTransformAction_->setEnabled(selectedRoad != nullptr);
    worldTransformAction_->setChecked(canvas_->transformSpace() == CanvasWidget::TransformSpace::world);
    localTransformAction_->setChecked(canvas_->transformSpace() == CanvasWidget::TransformSpace::local);
}

void MainWindow::selectRoad(const std::string& roadId) {
    selectedRoadId_ = roadId;
    canvas_->selectId(roadId);
    updateInspector();
    QSignalBlocker blocker(hierarchy_);
    for (QTreeWidgetItemIterator item(hierarchy_); *item != nullptr; ++item) {
        if ((*item)->data(0, Qt::UserRole + 1).toString() == QStringLiteral("road") &&
            (*item)->data(0, Qt::UserRole).toString().toStdString() == roadId) {
            hierarchy_->setCurrentItem(*item);
            break;
        }
    }
}

void MainWindow::updateInspector() {
    const auto* road = findRoad(commandProcessor_.current().project, selectedRoadId_);
    QSignalBlocker controlPointBlocker(controlPointSelector_);
    controlPointSelector_->clear();
    if (road == nullptr) {
        inspector_->setText(QStringLiteral("No object selected"));
        for (auto* button : inspectorPanel_->findChildren<QPushButton*>()) button->setEnabled(false);
        controlPointSelector_->setEnabled(false);
        controlPointX_->setEnabled(false);
        controlPointY_->setEnabled(false);
        endpointX_->setEnabled(false);
        endpointY_->setEnabled(false);
        return;
    }
    const auto [endX, endY] = roadEnd(*road);
    endpointX_->setValue(endX);
    endpointY_->setValue(endY);
    endpointX_->setEnabled(true);
    endpointY_->setEnabled(true);
    double length = 0.0;
    for (const auto& anchor : road->stationAnchors) length = std::max(length, anchor.resolvedStation);
    measureStart_->setMaximum(length);
    measureEnd_->setMaximum(length);
    if (length > 0.0 && measureEnd_->value() == 0.0) measureEnd_->setValue(length);
    const auto& project = commandProcessor_.current().project;
    const auto segmentId = selectedSegmentId_.empty() && !road->segmentIds.empty()
        ? road->segmentIds.front() : selectedSegmentId_;
    const auto segment = std::find_if(project.roadSegments().begin(), project.roadSegments().end(),
        [&](const auto& candidate) { return candidate.id == segmentId; });
    const auto width = segment == project.roadSegments().end()
        ? 8.0 : segment->crossSectionState.value("totalWidthMeters", 8.0);
    const auto& map = project.rootMap();
    inspector_->setText(QStringLiteral("RoadSpline\nID: %1\nOwner Map: %2\nMap: %3\nSpatialLevel: %4\nDisplayLayer: %5\nDirection: %6\nPlan length: %7 m\nSegments: %8\nPlaceholder width: %9 m%10")
        .arg(QString::fromStdString(road->id))
        .arg(QString::fromStdString(road->mapId))
        .arg(QString::fromStdString(map.id))
        .arg(QString::fromStdString(map.defaultSpatialLevelId))
        .arg(QString::fromStdString(map.defaultDisplayLayerId))
        .arg(QString::fromStdString(road->direction))
        .arg(length, 0, 'f', 3)
        .arg(road->segmentIds.size())
        .arg(width, 0, 'f', 3)
        .arg(selectedSegmentId_.empty() ? QString() : QStringLiteral("\nSelected segment: %1")
            .arg(QString::fromStdString(selectedSegmentId_))));
    for (auto* button : inspectorPanel_->findChildren<QPushButton*>()) button->setEnabled(true);
    auto* mergeButton = inspectorPanel_->findChild<QPushButton*>(QStringLiteral("mergeSegmentsButton"));
    if (mergeButton) mergeButton->setEnabled(hierarchy_->selectedItems().size() >= 2);
    boundaryStation_->setMaximum(length);
    bool canMoveBoundary = false;
    if (!selectedSegmentId_.empty() && road->segmentIds.size() > 1) {
        const auto segment = std::find_if(project.roadSegments().begin(), project.roadSegments().end(),
            [&](const auto& candidate) { return candidate.id == selectedSegmentId_; });
        if (segment != project.roadSegments().end()) {
            const auto position = std::find(road->segmentIds.begin(), road->segmentIds.end(), segment->id);
            const auto anchorId = position == road->segmentIds.begin()
                ? segment->endAnchorId : segment->startAnchorId;
            const auto station = stationForAnchor(*road, anchorId);
            if (std::isfinite(station)) boundaryStation_->setValue(station);
            canMoveBoundary = position != road->segmentIds.end() &&
                (position != road->segmentIds.begin() || std::next(position) != road->segmentIds.end());
        }
    }
    boundaryStation_->setEnabled(canMoveBoundary);
    auto* boundaryButton = inspectorPanel_->findChild<QPushButton*>(QStringLiteral("moveBoundaryButton"));
    if (boundaryButton) boundaryButton->setEnabled(canMoveBoundary);
    for (const auto& primitive : road->primitives) {
        const auto kind = primitive.value("kind", "");
        if (kind == "cubic-bezier") {
            const auto& ids = primitive.at("controlPointIds");
            const auto& points = primitive.contains("controlPoints") ? primitive.at("controlPoints") : primitive.at("points");
            for (std::size_t index = 0; index < ids.size(); ++index) {
                const auto pointId = ids[index].get<std::string>();
                if (controlPointSelector_->findData(QString::fromStdString(pointId), Qt::UserRole) >= 0) continue;
                const auto itemIndex = controlPointSelector_->count();
                controlPointSelector_->addItem(QStringLiteral("Control point %1").arg(index + 1),
                    QString::fromStdString(pointId));
                controlPointSelector_->setItemData(itemIndex, points[index].value("x", 0.0), Qt::UserRole + 1);
                controlPointSelector_->setItemData(itemIndex, points[index].value("y", 0.0), Qt::UserRole + 2);
            }
        } else {
            for (const auto& endpoint : {std::string("start"), std::string("end")}) {
                const auto pointId = primitive.value(endpoint + "ControlPointId", "");
                if (pointId.empty() || controlPointSelector_->findData(QString::fromStdString(pointId), Qt::UserRole) >= 0)
                    continue;
                const auto point = primitive.at(endpoint);
                const auto itemIndex = controlPointSelector_->count();
                controlPointSelector_->addItem(endpoint == "start" ? QStringLiteral("Start control point")
                                                                     : QStringLiteral("End control point"),
                    QString::fromStdString(pointId));
                controlPointSelector_->setItemData(itemIndex, point.value("x", 0.0), Qt::UserRole + 1);
                controlPointSelector_->setItemData(itemIndex, point.value("y", 0.0), Qt::UserRole + 2);
            }
        }
    }
    const auto hasControlPoints = controlPointSelector_->count() > 0;
    controlPointSelector_->setEnabled(hasControlPoints);
    controlPointX_->setEnabled(hasControlPoints);
    controlPointY_->setEnabled(hasControlPoints);
    auto* editPointButton = inspectorPanel_->findChild<QPushButton*>(QStringLiteral("previewControlPointEditButton"));
    if (editPointButton) editPointButton->setEnabled(hasControlPoints);
    if (hasControlPoints) {
        controlPointX_->setValue(controlPointSelector_->itemData(0, Qt::UserRole + 1).toDouble());
        controlPointY_->setValue(controlPointSelector_->itemData(0, Qt::UserRole + 2).toDouble());
    }
    if (length > 0.0 && measureEnd_->value() == 0.0) measureEnd_->setValue(length);
}

void MainWindow::previewControlPointEdit() {
    const auto* road = findRoad(commandProcessor_.current().project, selectedRoadId_);
    if (road == nullptr || controlPointSelector_->currentIndex() < 0) return;
    const auto pointId = controlPointSelector_->currentData(Qt::UserRole).toString().toStdString();
    auto editedRoad = *road;
    for (auto& primitive : editedRoad.primitives) {
        if (primitive.value("kind", "") == "cubic-bezier") {
            auto& ids = primitive["controlPointIds"];
            auto& points = primitive.contains("controlPoints") ? primitive["controlPoints"] : primitive["points"];
            for (std::size_t index = 0; index < ids.size(); ++index) {
                if (ids[index].get<std::string>() == pointId) {
                    points[index] = {{"x", controlPointX_->value()}, {"y", controlPointY_->value()}};
                }
            }
        } else {
            for (const auto& endpoint : {std::string("start"), std::string("end")}) {
                if (primitive.value(endpoint + "ControlPointId", "") == pointId) {
                    primitive[endpoint] = {{"x", controlPointX_->value()}, {"y", controlPointY_->value()}};
                }
            }
        }
    }
    editRoadWithAnchorResolution(std::move(editedRoad), false, true);
}

void MainWindow::editRoadWithAnchorResolution(
    domain::RoadSpline road,
    bool commitOnRelease,
    bool coalesceWithPriorEdit) {
    const auto revision = commandProcessor_.current().number;
    auto command = std::make_unique<application::EditRoadSplineCommand>(
        road, revision, coalesceWithPriorEdit);
    application::Preview probe;
    try {
        probe = commandProcessor_.preview(*command);
    } catch (const std::exception&) {
        previewCommand(std::move(command));
        if (commitOnRelease) applyPreview();
        return;
    }
    const auto remapDiagnostic = std::find_if(probe.diagnostics.begin(), probe.diagnostics.end(),
        [](const auto& diagnostic) {
            return diagnostic.ruleId == "STAT-CORE-003" && diagnostic.blocksCommit;
        });
    if (remapDiagnostic != probe.diagnostics.end()) {
        const auto* original = findRoad(commandProcessor_.current().project, road.id);
        if (original == nullptr) return;
        std::map<std::string, double> stationResolutions;
        for (const auto& anchorId : remapDiagnostic->affectedIds) {
            const auto anchor = std::find_if(original->stationAnchors.begin(), original->stationAnchors.end(),
                [&](const auto& candidate) { return candidate.id == anchorId; });
            if (anchor == original->stationAnchors.end()) continue;
            bool accepted = false;
            const auto station = QInputDialog::getDouble(this, QStringLiteral("Resolve RoadSpline anchor"),
                QStringLiteral("Choose a target station in meters for anchor %1 on the edited curve.")
                    .arg(QString::fromStdString(anchorId)),
                anchor->resolvedStation, 0.0, 1000000000.0, 3, &accepted);
            if (!accepted) {
                statusBar()->showMessage(QStringLiteral("Road edit canceled; anchor resolution is required."));
                return;
            }
            stationResolutions.emplace(anchorId, station);
        }
        command = std::make_unique<application::EditRoadSplineCommand>(
            std::move(road), revision, coalesceWithPriorEdit, std::move(stationResolutions));
    }
    previewCommand(std::move(command));
    if (commitOnRelease) applyPreview();
}

void MainWindow::splitAtStation(const std::string& roadId, double station) {
    const auto& project = commandProcessor_.current().project;
    const auto* road = findRoad(project, roadId);
    if (road == nullptr) return;
    const auto segment = std::find_if(project.roadSegments().begin(), project.roadSegments().end(),
        [&](const auto& candidate) {
            if (candidate.roadSplineId != roadId) return false;
            const auto start = stationForAnchor(*road, candidate.startAnchorId);
            const auto end = stationForAnchor(*road, candidate.endAnchorId);
            return station > start && station < end;
        });
    if (segment == project.roadSegments().end()) {
        statusBar()->showMessage(QStringLiteral("Split station must lie inside a RoadSegment."));
        return;
    }
    previewCommand(std::make_unique<application::SplitRoadSegmentCommand>(
        roadId, segment->id, station, commandProcessor_.current().number));
}

void MainWindow::reverseSelectedRoad() {
    if (selectedRoadId_.empty()) return;
    previewCommand(std::make_unique<application::ReverseRoadSplineCommand>(
        selectedRoadId_, commandProcessor_.current().number));
}

void MainWindow::deleteSelectedRoad() {
    const auto* road = findRoad(commandProcessor_.current().project, selectedRoadId_);
    if (road == nullptr) return;
    auto resolution = application::RoadDeleteResolution::cancel;
    if (!road->segmentIds.empty()) {
        const auto choice = QMessageBox::question(this, QStringLiteral("Delete RoadSpline"),
            QStringLiteral("This RoadSpline owns %1 RoadSegment(s). Delete the road and all owned segments as one undoable change?")
                .arg(road->segmentIds.size()),
            QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel);
        if (choice != QMessageBox::Yes) return;
        resolution = application::RoadDeleteResolution::deleteOwnedSegments;
    }
    previewCommand(std::make_unique<application::DeleteRoadSplineCommand>(
        selectedRoadId_, commandProcessor_.current().number, resolution));
}

void MainWindow::extendSelectedRoad() {
    const auto* road = findRoad(commandProcessor_.current().project, selectedRoadId_);
    if (road == nullptr) return;
    previewCommand(std::make_unique<application::ExtendRoadSplineCommand>(
        selectedRoadId_, geometry::Point2D{endpointX_->value(), endpointY_->value()}, commandProcessor_.current().number));
}

void MainWindow::shortenSelectedRoad() {
    const auto* road = findRoad(commandProcessor_.current().project, selectedRoadId_);
    if (road == nullptr) return;
    bool accepted = false;
    const QStringList choices{QStringLiteral("Move dependents"), QStringLiteral("Delete dependents"),
        QStringLiteral("Cancel")};
    const auto choice = QInputDialog::getItem(this, QStringLiteral("Resolve shortened anchors"),
        QStringLiteral("Dependent anchor resolution"), choices, 2, false, &accepted);
    if (!accepted || choice == choices[2]) return;
    const auto resolution = choice == choices[0] ? application::ShortenResolution::moveDependents
                                                 : application::ShortenResolution::deleteDependents;
    previewCommand(std::make_unique<application::ShortenRoadSplineCommand>(
        selectedRoadId_, geometry::Point2D{endpointX_->value(), endpointY_->value()},
        commandProcessor_.current().number, resolution));
}

void MainWindow::mergeSelectedSegments() {
    const auto selectedItems = hierarchy_->selectedItems();
    std::vector<std::string> segmentIds;
    for (const auto* item : selectedItems) {
        if (item->data(0, Qt::UserRole + 1).toString() == QStringLiteral("segment")) {
            segmentIds.push_back(item->data(0, Qt::UserRole).toString().toStdString());
        }
    }
    if (segmentIds.size() != 2) {
        statusBar()->showMessage(QStringLiteral("Select two adjacent RoadSegments to merge."));
        return;
    }
    const auto& project = commandProcessor_.current().project;
    const auto first = std::find_if(project.roadSegments().begin(), project.roadSegments().end(),
        [&](const auto& segment) { return segment.id == segmentIds[0]; });
    const auto second = std::find_if(project.roadSegments().begin(), project.roadSegments().end(),
        [&](const auto& segment) { return segment.id == segmentIds[1]; });
    if (first == project.roadSegments().end() || second == project.roadSegments().end() ||
        first->roadSplineId != second->roadSplineId) return;
    const auto* road = findRoad(project, first->roadSplineId);
    if (road == nullptr) return;
    const auto firstPosition = std::find(road->segmentIds.begin(), road->segmentIds.end(), first->id);
    const auto secondPosition = std::find(road->segmentIds.begin(), road->segmentIds.end(), second->id);
    if (firstPosition == road->segmentIds.end() || secondPosition == road->segmentIds.end() ||
        std::abs(std::distance(firstPosition, secondPosition)) != 1) {
        statusBar()->showMessage(QStringLiteral("Only adjacent RoadSegments can be merged."));
        return;
    }
    const auto upstreamId = firstPosition < secondPosition ? first->id : second->id;
    const auto downstreamId = firstPosition < secondPosition ? second->id : first->id;
    application::MergeRoadSegmentsCommand probe(
        road->id, upstreamId, downstreamId, commandProcessor_.current().number);
    application::SegmentMergeResolutions resolutions;
    for (const auto& field : probe.conflictingFields(commandProcessor_.current())) {
        bool accepted = false;
        const auto source = QInputDialog::getItem(this, QStringLiteral("Resolve merge field"),
            QString::fromStdString(field), {QStringLiteral("Keep upstream"), QStringLiteral("Keep downstream")},
            0, false, &accepted);
        if (!accepted) return;
        resolutions[field] = source == QStringLiteral("Keep upstream")
            ? application::SegmentMergeSource::upstream : application::SegmentMergeSource::downstream;
    }
    previewCommand(std::make_unique<application::MergeRoadSegmentsCommand>(
        road->id, upstreamId, downstreamId, commandProcessor_.current().number, std::move(resolutions)));
}

void MainWindow::moveSelectedBoundary() {
    if (selectedSegmentId_.empty()) {
        statusBar()->showMessage(QStringLiteral("Select a RoadSegment boundary to move."));
        return;
    }
    const auto& project = commandProcessor_.current().project;
    const auto segment = std::find_if(project.roadSegments().begin(), project.roadSegments().end(),
        [&](const auto& candidate) { return candidate.id == selectedSegmentId_; });
    if (segment == project.roadSegments().end()) return;
    const auto* road = findRoad(project, segment->roadSplineId);
    if (road == nullptr || road->segmentIds.size() < 2) {
        statusBar()->showMessage(QStringLiteral("A boundary requires at least two RoadSegments."));
        return;
    }
    const auto position = std::find(road->segmentIds.begin(), road->segmentIds.end(), segment->id);
    if (position == road->segmentIds.end()) return;
    const auto boundaryAnchorId = position == road->segmentIds.begin()
        ? segment->endAnchorId : segment->startAnchorId;
    previewCommand(std::make_unique<application::MoveRoadSegmentBoundaryCommand>(
        road->id, boundaryAnchorId, boundaryStation_->value(), commandProcessor_.current().number));
}

void MainWindow::previewCommand(std::unique_ptr<application::Command> command) {
    pendingCommand_ = std::move(command);
    try {
        pendingPreview_ = commandProcessor_.preview(*pendingCommand_);
        canvas_->setPreviewProject(pendingPreview_->candidate.project);
        QString summary = QString::fromLatin1(pendingCommand_->name());
        if (!pendingPreview_->impactIds.empty()) {
            summary += QStringLiteral("\nAffected IDs: ");
            for (std::size_t index = 0; index < pendingPreview_->impactIds.size(); ++index) {
                if (index != 0) summary += QStringLiteral(", ");
                summary += QString::fromStdString(pendingPreview_->impactIds[index]);
            }
        }
        for (const auto& diagnostic : pendingPreview_->diagnostics) {
            const auto severity = diagnostic.severity == application::Severity::error ? "Error"
                : diagnostic.severity == application::Severity::warning ? "Warning" : "Info";
            summary += QStringLiteral("\n%1: %2").arg(QString::fromLatin1(severity),
                QString::fromStdString(diagnostic.message));
        }
        for (const auto& envelope : pendingPreview_->roadEnvelopes) {
            summary += QStringLiteral("\nEnvelope %1: %2")
                .arg(QString::fromStdString(envelope.roadSplineId),
                    envelope.result.valid ? QStringLiteral("valid") : QStringLiteral("invalid preview"));
            for (const auto& issue : envelope.result.issues) {
                summary += QStringLiteral("\n%1").arg(QString::fromStdString(issue.message));
            }
        }
        if (pendingPreview_->diagnostics.empty()) summary += QStringLiteral("\nReady to apply.");
        previewSummary_->setText(summary);
        const auto blocked = std::any_of(pendingPreview_->diagnostics.begin(), pendingPreview_->diagnostics.end(),
            [](const auto& diagnostic) {
                return diagnostic.severity == application::Severity::error && diagnostic.blocksCommit;
            });
        applyButton_->setEnabled(!blocked);
        changesDock_->show();
        applyButton_->setFocus(Qt::OtherFocusReason);
    } catch (const std::exception& error) {
        previewSummary_->setText(QString::fromUtf8(error.what()));
        applyButton_->setEnabled(false);
        changesDock_->show();
        pendingPreview_.reset();
        canvas_->setPreviewProject(std::nullopt);
    }
}

void MainWindow::applyPreview() {
    if (!pendingCommand_ || !pendingPreview_ || !applyButton_->isEnabled()) return;
    try {
        const bool createdRoad = std::string(pendingCommand_->name()) == "create-road-spline";
        const auto previousSelection = selectedRoadId_;
        commandProcessor_.commit(*pendingCommand_);
        undoSelection_.push_back(previousSelection);
        redoSelection_.clear();
        if (!pendingSelectionId_.empty()) selectedRoadId_ = pendingSelectionId_;
        cancelPreview();
        refreshProjectViews();
        if (createdRoad && !selectedRoadId_.empty()) {
            canvas_->fitRoad(selectedRoadId_);
            statusBar()->showMessage(QStringLiteral("Road created and selected"));
        }
        updateHistoryActions();
    } catch (const std::exception& error) {
        previewSummary_->setText(QString::fromUtf8(error.what()));
        applyButton_->setEnabled(false);
    }
}

void MainWindow::cancelPreview() {
    if (pendingPreview_) commandProcessor_.cancel(*pendingPreview_);
    pendingCommand_.reset();
    pendingPreview_.reset();
    canvas_->setPreviewProject(std::nullopt);
    changesDock_->hide();
    pendingSelectionId_.clear();
}

bool MainWindow::isDirty() const {
    try {
        return commandProcessor_.current().project.normalizedJson(schemaGeneration_) != savedSource_;
    } catch (const std::exception&) {
        return true;
    }
}

bool MainWindow::openPackage(const std::filesystem::path& path, std::string* error) {
    try {
        auto package = persistence::Package::load(path);
        const auto manifest = nlohmann::json::parse(package.manifestJson());
        const auto schema = manifest.value("schemaVersion", 0);
        if (schema < 1 || schema > 2) throw std::invalid_argument("Unsupported package schema for editing.");
        cancelPreview();
        commandProcessor_ = application::CommandProcessor({0, package.project(), {}});
        packagePath_ = path;
        schemaGeneration_ = schema;
        savedSource_ = package.project().normalizedJson(schemaGeneration_);
        selectedRoadId_.clear();
        selectedSegmentId_.clear();
        undoSelection_.clear();
        redoSelection_.clear();
        refreshProjectViews();
        updateHistoryActions();
        return true;
    } catch (const std::exception& exception) {
        if (error) *error = exception.what();
        return false;
    }
}

bool MainWindow::savePackage(
    const std::filesystem::path& path,
    bool allowSchemaMigration,
    std::string* error) {
    try {
        const bool samePackage = !packagePath_.empty() && path.lexically_normal() == packagePath_.lexically_normal();
        auto targetSchema = samePackage ? schemaGeneration_ : 2;
        const auto& project = commandProcessor_.current().project;
        if (samePackage && targetSchema == 1 && !project.roadSplines().empty()) {
            if (!allowSchemaMigration) {
                throw std::invalid_argument("Road records require explicit migration to package schema 2.");
            }
            const auto migration = persistence::Package::migrate(path, 2);
            if (!migration.succeeded) {
                throw std::invalid_argument("Package migration failed: " + migration.error);
            }
            targetSchema = 2;
            schemaGeneration_ = 2;
        }
        persistence::SaveOptions options;
        options.schemaGeneration = targetSchema;
        persistence::Package::fromProject(project).save(path, options);
        packagePath_ = path;
        schemaGeneration_ = targetSchema;
        savedSource_ = project.normalizedJson(schemaGeneration_);
        refreshProjectViews();
        return true;
    } catch (const std::exception& exception) {
        if (error) *error = exception.what();
        return false;
    }
}

bool MainWindow::confirmDiscardChanges() {
    if (!isDirty()) return true;
    const auto choice = QMessageBox::warning(this, QStringLiteral("Unsaved project changes"),
        QStringLiteral("Save changes before continuing?"),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Cancel);
    if (choice == QMessageBox::Cancel) return false;
    if (choice == QMessageBox::Save) {
        saveFromDialog();
        return !isDirty();
    }
    return true;
}

void MainWindow::newProject() {
    if (!confirmDiscardChanges()) return;
    cancelPreview();
    const auto projectId = QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
    const auto mapId = QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
    commandProcessor_ = application::CommandProcessor({0, domain::Project::empty(projectId, mapId), {}});
    packagePath_.clear();
    schemaGeneration_ = 2;
    savedSource_ = commandProcessor_.current().project.normalizedJson(schemaGeneration_);
    selectedRoadId_.clear();
    selectedSegmentId_.clear();
    undoSelection_.clear();
    redoSelection_.clear();
    refreshProjectViews();
    updateHistoryActions();
}

void MainWindow::openFromDialog() {
    const auto path = QFileDialog::getExistingDirectory(this, QStringLiteral("Open Atlas package"));
    if (path.isEmpty() || !confirmDiscardChanges()) return;
    std::string error;
    if (!openPackage(path.toStdString(), &error)) {
        QMessageBox::critical(this, QStringLiteral("Unable to open package"), QString::fromStdString(error));
    }
}

void MainWindow::saveFromDialog() {
    if (packagePath_.empty()) {
        saveAsFromDialog();
        return;
    }
    std::string error;
    if (savePackage(packagePath_, false, &error)) return;
    const bool migrationNeeded = schemaGeneration_ == 1 &&
        !commandProcessor_.current().project.roadSplines().empty();
    if (migrationNeeded && QMessageBox::question(this, QStringLiteral("Upgrade package schema"),
        QStringLiteral("Road records require schema 2. Migrate this package transactionally?"),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No) == QMessageBox::Yes &&
        savePackage(packagePath_, true, &error)) return;
    QMessageBox::critical(this, QStringLiteral("Unable to save package"), QString::fromStdString(error));
}

void MainWindow::saveAsFromDialog() {
    auto path = QFileDialog::getSaveFileName(this, QStringLiteral("Save Atlas package"), {},
        QStringLiteral("Atlas packages (*.atlas)"));
    if (path.isEmpty()) return;
    if (!path.endsWith(QStringLiteral(".atlas"), Qt::CaseInsensitive)) path += QStringLiteral(".atlas");
    std::string error;
    if (!savePackage(path.toStdString(), false, &error)) {
        QMessageBox::critical(this, QStringLiteral("Unable to save package"), QString::fromStdString(error));
    }
}

} // namespace atlas::ui