#include "atlas/ui/canvas_widget.hpp"

#include "atlas/geometry/envelope.hpp"

#include <QMouseEvent>
#include <QKeyEvent>
#include <QOpenGLFunctions>
#include <QPainter>
#include <QWheelEvent>
#include <QUuid>
#include <QPainterPath>
#include <QPolygonF>

#include <algorithm>
#include <cmath>
#include <limits>
#include <QColor>
#include <utility>

namespace atlas::ui {

namespace {

// World-space tolerances are divided by zoom to keep pointer targets a stable screen size.
constexpr double controlPointHitRadiusPixels = 10.0;
constexpr double canvasTargetHitRadiusPixels = 10.0;
constexpr double roadHitRadiusPixels = 12.0;

atlas::geometry::Point2D readPoint(const nlohmann::json& value) {
    return {value.value("x", 0.0), value.value("y", 0.0)};
}

atlas::geometry::CurveKernel curveForRoad(const atlas::domain::RoadSpline& road) {
    std::vector<atlas::geometry::CurvePrimitive> primitives;
    if (!road.primitives.is_array() || road.primitives.empty()) {
        throw std::invalid_argument("RoadSpline requires at least one primitive.");
    }
    for (const auto& item : road.primitives) {
        const auto kind = item.value("kind", "");
        if (kind == "line" || kind == "polyline") {
            primitives.push_back(atlas::geometry::PolylinePrimitive{
                item.value("id", ""), item.value("startControlPointId", ""),
                item.value("endControlPointId", ""), readPoint(item.at("start")), readPoint(item.at("end"))});
        } else if (kind == "cubic-bezier") {
            const auto& ids = item.at("controlPointIds");
            const auto& points = item.contains("controlPoints") ? item.at("controlPoints") : item.at("points");
            if (!ids.is_array() || ids.size() != 4 || !points.is_array() || points.size() != 4) {
                throw std::invalid_argument("Cubic Bezier source requires four control points.");
            }
            std::array<std::string, 4> pointIds;
            std::array<atlas::geometry::Point2D, 4> controlPoints;
            for (std::size_t index = 0; index < 4; ++index) {
                pointIds[index] = ids[index].get<std::string>();
                controlPoints[index] = readPoint(points[index]);
            }
            primitives.push_back(atlas::geometry::CubicBezierPrimitive{
                item.value("id", ""), std::move(pointIds), std::move(controlPoints)});
        } else if (kind == "circular-arc") {
            primitives.push_back(atlas::geometry::CircularArcPrimitive{
                item.value("id", ""), item.value("startControlPointId", ""),
                item.value("endControlPointId", ""), readPoint(item.at("start")), readPoint(item.at("end")),
                item.value("radius", 0.0), item.value("side", "left") == "left"
                    ? atlas::geometry::ArcSide::left : atlas::geometry::ArcSide::right});
        } else {
            throw std::invalid_argument("RoadSpline primitive kind is unsupported.");
        }
    }
    return atlas::geometry::CurveKernel(std::move(primitives));
}

QPointF toScreen(const atlas::render::Camera2D& camera, atlas::geometry::Point2D point) {
    const auto screen = camera.worldToScreen({point.x, point.y});
    return {screen.x, screen.y};
}

double stationForAnchor(const atlas::domain::RoadSpline& road, const std::string& anchorId) {
    const auto anchor = std::find_if(road.stationAnchors.begin(), road.stationAnchors.end(),
        [&](const auto& candidate) { return candidate.id == anchorId; });
    return anchor == road.stationAnchors.end() ? std::numeric_limits<double>::quiet_NaN()
                                                : anchor->resolvedStation;
}

} // namespace

CanvasWidget::CanvasWidget(QWidget* parent)
    : QOpenGLWidget(parent), camera_({1.0, 1.0}) {
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
}

const atlas::render::Camera2D& CanvasWidget::camera() const noexcept {
    return camera_;
}

void CanvasWidget::setProject(atlas::domain::Project project) {
    project_ = std::move(project);
    selection_.clear();
    transformSpace_ = TransformSpace::world;
    if (selectionChangedHandler_) selectionChangedHandler_(false);
    update();
}

const atlas::application::SelectionState& CanvasWidget::selection() const noexcept {
    return selection_;
}

void CanvasWidget::setTool(Tool tool) {
    if (tool_ == Tool::road && tool != Tool::road) cancelRoadDraft();
    hoveredControlPoint_.reset();
    hoveredRoadId_.clear();
    hoveredSegmentId_.clear();
    hoveredObjectId_.clear();
    if (tool_ == Tool::measure && tool != Tool::measure) {
        firstMeasurePoint_.reset();
        hoveredMeasurementPoint_.reset();
        secondMeasurePoint_.reset();
    }
    tool_ = tool;
    setAccessibleDescription(tool_ == Tool::road
        ? QStringLiteral("Road tool. Use arrows to position the cursor, Space to add a point, Enter to preview, Escape to cancel.")
        : tool_ == Tool::edit ? QStringLiteral("Edit tool. Select a road control point and drag or move it.")
        : tool_ == Tool::split ? QStringLiteral("Split tool. Select a road station to preview a segment split.")
        : tool_ == Tool::measure ? QStringLiteral("Measure tool. Select two points on a road to measure station distance.")
                                 : QStringLiteral("Selection tool."));
    if (toolChangedHandler_) toolChangedHandler_(tool_);
    update();
}

CanvasWidget::Tool CanvasWidget::tool() const noexcept { return tool_; }

CanvasWidget::InteractionState CanvasWidget::interactionState() const noexcept {
    if (draggingControlPoint_ || draggingTransformGizmo_ || draggingMapObject_) {
        return InteractionState::manipulating;
    }
    if (tool_ == Tool::road && (provisionalDraftPoint_ || !draftPoints_.empty())) {
        return InteractionState::drafting;
    }
    if (tool_ == Tool::measure && firstMeasurePoint_) return InteractionState::measuring;
    if (!selection_.primaryId().empty()) return InteractionState::selected;
    if (hoveredControlPoint_ || !hoveredRoadId_.empty() || !hoveredSegmentId_.empty() ||
        !hoveredObjectId_.empty()) return InteractionState::hovering;
    return InteractionState::idle;
}

void CanvasWidget::setRoadDrawingMode(RoadDrawingMode mode) {
    drawingMode_ = mode;
    cancelRoadDraft();
}

void CanvasWidget::setRoadSnapToGrid(bool enabled) { snapRoadDraftToGrid_ = enabled; }

void CanvasWidget::setArcRadiusMeters(double radius) { arcRadiusMeters_ = radius; }
void CanvasWidget::setRoadWidthMeters(double width) { roadWidthMeters_ = width; update(); }

void CanvasWidget::finishRoadDrawing() {
    finishRoadDraft();
}

void CanvasWidget::setRoadCreatedHandler(std::function<void(atlas::domain::RoadSpline)> handler) {
    roadCreatedHandler_ = std::move(handler);
}

void CanvasWidget::setRoadEditedHandler(std::function<void(atlas::domain::RoadSpline)> handler) {
    roadEditedHandler_ = std::move(handler);
}

void CanvasWidget::setMapObjectEditedHandler(std::function<void(atlas::domain::MapObject)> handler) {
    mapObjectEditedHandler_ = std::move(handler);
}

void CanvasWidget::setRoadSelectedHandler(std::function<void(std::string)> handler) {
    roadSelectedHandler_ = std::move(handler);
}

void CanvasWidget::setSelectionChangedHandler(std::function<void(bool)> handler) {
    selectionChangedHandler_ = std::move(handler);
    if (selectionChangedHandler_) {
        selectionChangedHandler_(std::any_of(project_.roadSplines().begin(), project_.roadSplines().end(),
            [&](const auto& road) { return road.id == selection_.primaryId(); }));
    }
}

void CanvasWidget::setSegmentSelectedHandler(std::function<void(std::string, std::string)> handler) {
    segmentSelectedHandler_ = std::move(handler);
}

void CanvasWidget::setRoadStationHandler(std::function<void(std::string, double)> handler) {
    roadStationHandler_ = std::move(handler);
}

void CanvasWidget::setStatusHandler(std::function<void(QString)> handler) {
    statusHandler_ = std::move(handler);
}

void CanvasWidget::setToolChangedHandler(std::function<void(Tool)> handler) {
    toolChangedHandler_ = std::move(handler);
}

void CanvasWidget::setViewChangedHandler(std::function<void(double)> handler) {
    viewChangedHandler_ = std::move(handler);
    if (viewChangedHandler_) viewChangedHandler_(camera_.zoom());
}

void CanvasWidget::setPreviewProject(std::optional<atlas::domain::Project> project) {
    previewProject_ = std::move(project);
    update();
}

void CanvasWidget::selectId(const std::string& id) {
    if (id.empty()) selection_.clear();
    else selection_.select(id);
    const auto selectedRoad = std::any_of(project_.roadSplines().begin(), project_.roadSplines().end(),
        [&](const auto& road) { return road.id == selection_.primaryId(); });
    if (!selectedRoad) transformSpace_ = TransformSpace::world;
    if (selectionChangedHandler_) selectionChangedHandler_(selectedRoad);
    update();
}

void CanvasWidget::setSelectedSegment(const std::string& segmentId) {
    selectedSegmentId_ = segmentId;
    update();
}

void CanvasWidget::setTransformSpace(TransformSpace transformSpace) {
    const auto selectedRoad = std::any_of(project_.roadSplines().begin(), project_.roadSplines().end(),
        [&](const auto& road) { return road.id == selection_.primaryId(); });
    transformSpace_ = transformSpace == TransformSpace::local && selectedRoad
        ? TransformSpace::local : TransformSpace::world;
    update();
}

CanvasWidget::TransformSpace CanvasWidget::transformSpace() const noexcept {
    return transformSpace_;
}

void CanvasWidget::fitSelection() {
    const auto selectedId = selection_.primaryId();
    if (std::any_of(project_.roadSplines().begin(), project_.roadSplines().end(),
        [&](const auto& road) { return road.id == selectedId; })) {
        fitRoad(selectedId);
        return;
    }
    const auto* object = project_.rootMap().findObject(selectedId);
    if (object == nullptr || !object->geometry().is_object() ||
        !object->geometry().contains("x") || !object->geometry().contains("y")) return;
    const auto x = object->geometry().value("x", 0.0);
    const auto y = object->geometry().value("y", 0.0);
    camera_.fitToBounds({x - 5.0, y - 5.0}, {x + 5.0, y + 5.0}, 32.0);
    if (viewChangedHandler_) viewChangedHandler_(camera_.zoom());
    update();
}

void CanvasWidget::fitRoad(const std::string& roadSplineId) {
    const auto road = std::find_if(project_.roadSplines().begin(), project_.roadSplines().end(),
        [&](const auto& candidate) { return candidate.id == roadSplineId; });
    if (road == project_.roadSplines().end()) return;
    try {
        const auto curve = curveForRoad(*road);
        const auto samples = curve.sampleByStationInterval(0.5, 100000);
        if (samples.empty()) return;
        auto minimumX = samples.front().position.x;
        auto maximumX = minimumX;
        auto minimumY = samples.front().position.y;
        auto maximumY = minimumY;
        for (const auto& sample : samples) {
            minimumX = std::min(minimumX, sample.position.x);
            maximumX = std::max(maximumX, sample.position.x);
            minimumY = std::min(minimumY, sample.position.y);
            maximumY = std::max(maximumY, sample.position.y);
        }
        double halfWidth = 4.0;
        const auto segment = std::find_if(project_.roadSegments().begin(), project_.roadSegments().end(),
            [&](const auto& candidate) { return candidate.roadSplineId == roadSplineId; });
        if (segment != project_.roadSegments().end()) {
            halfWidth = segment->crossSectionState.value("totalWidthMeters", 8.0) * 0.5;
        }
        minimumX -= halfWidth;
        maximumX += halfWidth;
        minimumY -= halfWidth;
        maximumY += halfWidth;
        camera_.fitToBounds({minimumX, minimumY}, {maximumX, maximumY}, 32.0);
        if (viewChangedHandler_) viewChangedHandler_(camera_.zoom());
        update();
    } catch (const std::exception&) {
        return;
    }
}

bool CanvasWidget::event(QEvent* event) {
    if (event->type() == QEvent::KeyPress) {
        auto* key = static_cast<QKeyEvent*>(event);
        if (key->key() == Qt::Key_Tab && !key->modifiers().testFlag(Qt::ControlModifier)) {
            cycleCanvasSelection(key->modifiers().testFlag(Qt::ShiftModifier));
            key->accept();
            return true;
        }
    }
    return QOpenGLWidget::event(event);
}

void CanvasWidget::initializeGL() {
    context()->functions()->glClearColor(0.08f, 0.09f, 0.11f, 1.0f);
}

void CanvasWidget::resizeGL(int width, int height) {
    camera_.setViewport({static_cast<double>(width), static_cast<double>(height)});
}

void CanvasWidget::paintGL() {
    context()->functions()->glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, false);
    drawGrid(painter);
    drawRoads(painter);
    drawObjects(painter);
    drawRoadDraft(painter);
    drawMeasurement(painter);
    drawTransformGizmo(painter);
    if (hasFocus()) {
        painter.setBrush(Qt::NoBrush);
        painter.setPen(QPen(QColor(255, 220, 100), 2.0, Qt::DashLine));
        painter.drawRect(rect().adjusted(2, 2, -3, -3));
    }
    painter.end();
}

void CanvasWidget::drawRoads(QPainter& painter) {
    const auto& project = previewProject_ ? *previewProject_ : project_;
    for (const auto& road : project.roadSplines()) {
        try {
            const auto& renderRoad = draggedRoad_ && draggedRoad_->id == road.id ? *draggedRoad_ : road;
            const auto curve = curveForRoad(renderRoad);
            const auto samples = curve.sampleByStationInterval(0.5, 100000);
            QPainterPath centerline;
            for (std::size_t index = 0; index < samples.size(); ++index) {
                const auto position = toScreen(camera_, samples[index].position);
                if (index == 0) centerline.moveTo(position);
                else centerline.lineTo(position);
            }

            double widthMeters = 8.0;
            auto geometryPolicy = project.geometryPolicy();
            auto crossSectionState = nlohmann::json::object();
            if (!road.segmentIds.empty()) {
                const auto segment = std::find_if(project.roadSegments().begin(), project.roadSegments().end(),
                    [&](const auto& candidate) { return candidate.id == road.segmentIds.front(); });
                if (segment != project.roadSegments().end()) {
                    crossSectionState = segment->crossSectionState;
                    widthMeters = segment->crossSectionState.value("totalWidthMeters", 8.0);
                }
            }
            atlas::geometry::RoadEnvelopeOptions options;
            options.totalWidthMeters = widthMeters;
            options.miterLimit = geometryPolicy.value("miterLimitRatio", 4.0);
            const auto joinStyle = crossSectionState.value("joinStyle", "round");
            options.joinStyle = joinStyle == "miter" ? atlas::geometry::JoinStyle::miter
                : joinStyle == "bevel" ? atlas::geometry::JoinStyle::bevel : atlas::geometry::JoinStyle::round;
            const auto cacheKey = renderRoad.normalizedJson().dump() + geometryPolicy.dump() + crossSectionState.dump();
            const auto envelopeLookup = envelopeCache_.getOrGenerate(cacheKey, curve, options);
            const auto& envelope = *envelopeLookup.result;
            const auto& polygon = envelope.valid ? envelope.polygon : envelope.boundedPreview;
            if (polygon.size() >= 3) {
                QPolygonF screenPolygon;
                for (const auto point : polygon) screenPolygon << toScreen(camera_, point);
                painter.setPen(QPen(envelope.valid ? QColor(64, 140, 195, 210) : QColor(245, 105, 85),
                    1.0, envelope.valid ? Qt::SolidLine : Qt::DashLine));
                painter.setBrush(envelope.valid ? QBrush(QColor(55, 135, 190, 42))
                    : QBrush(QColor(245, 105, 85, 180), Qt::DiagCrossPattern));
                painter.drawPolygon(screenPolygon);
                if (!envelope.valid) {
                    const auto issue = envelope.issues.empty() ? QStringLiteral("No diagnostic available")
                        : QString::fromStdString(envelope.issues.front().message).left(72);
                    painter.setPen(QColor(255, 255, 255));
                    painter.drawText(screenPolygon.boundingRect().topLeft() + QPointF(4.0, 15.0),
                        QStringLiteral("Derived envelope invalid: %1").arg(issue));
                }
            }

            const bool selected = selection_.primaryId() == road.id;
            const bool hovered = hoveredRoadId_ == road.id;
            painter.setBrush(Qt::NoBrush);
            if (selected || hovered) {
                painter.setPen(QPen(selected ? QColor(255, 220, 100, 100) : QColor(125, 210, 255, 100),
                    selected ? 8.0 : 6.0));
                painter.drawPath(centerline);
            }
            painter.setPen(QPen(selected ? QColor(255, 220, 100) : hovered ? QColor(125, 210, 255)
                : QColor(190, 220, 240), selected ? 3.5 : hovered ? 3.0 : 2.0));
            painter.drawPath(centerline);
            if (road.segmentIds.size() > 1) {
                for (std::size_t segmentIndex = 0; segmentIndex < road.segmentIds.size(); ++segmentIndex) {
                    const auto segment = std::find_if(project.roadSegments().begin(), project.roadSegments().end(),
                        [&](const auto& candidate) { return candidate.id == road.segmentIds[segmentIndex]; });
                    if (segment == project.roadSegments().end()) continue;
                    const auto startStation = stationForAnchor(road, segment->startAnchorId);
                    const auto endStation = stationForAnchor(road, segment->endAnchorId);
                    if (!std::isfinite(startStation) || !std::isfinite(endStation) || endStation <= startStation) continue;
                    QPainterPath segmentPath;
                    segmentPath.moveTo(toScreen(camera_, curve.evaluateAtStation(startStation).position));
                    for (auto station = startStation + 0.5; station < endStation; station += 0.5) {
                        segmentPath.lineTo(toScreen(camera_, curve.evaluateAtStation(station).position));
                    }
                    segmentPath.lineTo(toScreen(camera_, curve.evaluateAtStation(endStation).position));
                    const auto segmentSelected = segment->id == selectedSegmentId_;
                    const auto segmentHovered = segment->id == hoveredSegmentId_;
                    const auto segmentColor = segmentSelected ? QColor(255, 220, 100)
                        : segmentHovered ? QColor(125, 210, 255)
                        : segmentIndex % 2 == 0 ? QColor(90, 205, 190) : QColor(235, 155, 95);
                    painter.setBrush(Qt::NoBrush);
                    painter.setPen(QPen(segmentColor, segmentSelected || segmentHovered ? 5.0 : 3.5,
                        segmentIndex % 2 == 0 ? Qt::SolidLine : Qt::DashLine));
                    painter.drawPath(segmentPath);
                    const auto midpoint = curve.evaluateAtStation((startStation + endStation) * 0.5).position;
                    painter.setPen(segmentColor);
                    painter.drawText(toScreen(camera_, midpoint) + QPointF(5.0, -6.0),
                        QStringLiteral("S%1").arg(segmentIndex + 1));
                    if (segmentIndex + 1 < road.segmentIds.size()) {
                        const auto boundary = toScreen(camera_, curve.evaluateAtStation(endStation).position);
                        painter.setPen(QPen(QColor(25, 30, 35), 1.5));
                        painter.setBrush(segmentSelected || segmentHovered ? segmentColor : QColor(240, 240, 240));
                        painter.drawEllipse(boundary, 5.0, 5.0);
                    }
                }
            }
            if (selected || hovered || tool_ == Tool::edit) {
                for (const auto& item : renderRoad.primitives) {
                    if (item.value("kind", "") == "cubic-bezier") {
                        const auto& ids = item.at("controlPointIds");
                        const auto& points = item.contains("controlPoints") ? item.at("controlPoints") : item.at("points");
                        for (std::size_t index = 0; index < points.size(); ++index) {
                            const auto id = ids[index].get<std::string>();
                            const auto point = toScreen(camera_, readPoint(points[index]));
                            const bool active = id == selectedControlPointId_ ||
                                (hoveredControlPoint_ && hoveredControlPoint_->controlPointId == id);
                            painter.setPen(QPen(active ? QColor(255, 255, 255) : QColor(30, 35, 42),
                                active ? 2.0 : 1.0));
                            painter.setBrush(active ? QColor(255, 220, 100) : QColor(190, 220, 240));
                            painter.drawEllipse(point, active ? 7.0 : 5.5, active ? 7.0 : 5.5);
                        }
                    } else {
                        for (const auto& endpoint : {std::string("start"), std::string("end")}) {
                            const auto id = item.value(endpoint + "ControlPointId", "");
                            const auto point = toScreen(camera_, readPoint(item.at(endpoint)));
                            const bool active = id == selectedControlPointId_ ||
                                (hoveredControlPoint_ && hoveredControlPoint_->controlPointId == id);
                            painter.setPen(QPen(active ? QColor(255, 255, 255) : QColor(30, 35, 42),
                                active ? 2.0 : 1.0));
                            painter.setBrush(active ? QColor(255, 220, 100) : QColor(190, 220, 240));
                            painter.drawEllipse(point, active ? 7.0 : 5.5, active ? 7.0 : 5.5);
                        }
                    }
                }
            }
        } catch (const std::exception& error) {
            if (statusHandler_) {
                statusHandler_(QStringLiteral("Source RoadSpline %1 invalid: %2")
                    .arg(QString::fromStdString(road.id), QString::fromUtf8(error.what()).left(96)));
            }
            setAccessibleDescription(QStringLiteral("Source road invalid: %1")
                .arg(QString::fromUtf8(error.what()).left(96)));
            painter.setPen(QColor(255, 150, 135));
            painter.drawText(QPointF(12.0, 24.0 + static_cast<double>(&road - project.roadSplines().data()) * 18.0),
                QStringLiteral("Source road invalid: %1").arg(QString::fromUtf8(error.what()).left(72)));
            continue;
        }
    }
}

void CanvasWidget::drawRoadDraft(QPainter& painter) {
    if (tool_ != Tool::road || draftPoints_.empty()) return;
    try {
        std::vector<atlas::geometry::CurvePrimitive> draftPrimitives;
        if (drawingMode_ == RoadDrawingMode::polyline && draftPoints_.size() >= 2) {
            for (std::size_t index = 1; index < draftPoints_.size(); ++index) {
                draftPrimitives.push_back(atlas::geometry::PolylinePrimitive{
                    "draft-" + std::to_string(index - 1), "draft-control-" + std::to_string(index - 1),
                    "draft-control-" + std::to_string(index), draftPoints_[index - 1], draftPoints_[index]});
            }
        } else if (drawingMode_ == RoadDrawingMode::bezier && draftPoints_.size() == 4) {
            draftPrimitives.push_back(atlas::geometry::CubicBezierPrimitive{
                "draft-bezier", {"draft-p0", "draft-p1", "draft-p2", "draft-p3"},
                {draftPoints_[0], draftPoints_[1], draftPoints_[2], draftPoints_[3]}});
        } else if (drawingMode_ == RoadDrawingMode::fixedRadiusArc && draftPoints_.size() == 2) {
            draftPrimitives.push_back(atlas::geometry::CircularArcPrimitive{
                "draft-arc", "draft-start", "draft-end", draftPoints_[0], draftPoints_[1],
                arcRadiusMeters_, atlas::geometry::ArcSide::left});
        }
        if (!draftPrimitives.empty()) {
            const atlas::geometry::CurveKernel curve(std::move(draftPrimitives));
            atlas::geometry::RoadEnvelopeOptions options;
            options.totalWidthMeters = roadWidthMeters_;
            const auto envelope = atlas::geometry::generateRoadEnvelope(curve, options);
            const auto& polygon = envelope.valid ? envelope.polygon : envelope.boundedPreview;
            if (polygon.size() >= 3) {
                QPolygonF screenPolygon;
                for (const auto point : polygon) screenPolygon << toScreen(camera_, point);
                painter.setPen(QPen(envelope.valid ? QColor(255, 195, 70, 210) : QColor(220, 70, 60),
                    1.0, envelope.valid ? Qt::DashLine : Qt::DotLine));
                painter.setBrush(envelope.valid ? QColor(255, 195, 70, 34) : QColor(220, 70, 60, 24));
                painter.drawPolygon(screenPolygon);
            }
        }
    } catch (const std::exception&) {
    }
    painter.setPen(QPen(QColor(255, 195, 70), 2.0, Qt::DashLine));
    painter.setBrush(Qt::NoBrush);
    QPainterPath path;
    path.moveTo(toScreen(camera_, draftPoints_.front()));
    for (std::size_t index = 1; index < draftPoints_.size(); ++index) {
        path.lineTo(toScreen(camera_, draftPoints_[index]));
    }
    if (draftPoints_.size() == 1 || draftPoints_.back().x != keyboardCursor_.x ||
        draftPoints_.back().y != keyboardCursor_.y) {
        path.lineTo(toScreen(camera_, keyboardCursor_));
    }
    painter.drawPath(path);
    painter.setBrush(QColor(255, 195, 70));
    for (const auto point : draftPoints_) painter.drawEllipse(toScreen(camera_, point), 4.0, 4.0);
    painter.setPen(QPen(QColor(255, 255, 255), 1.5));
    painter.drawEllipse(toScreen(camera_, keyboardCursor_), provisionalDraftPoint_ ? 6.0 : 4.0,
        provisionalDraftPoint_ ? 6.0 : 4.0);
}

void CanvasWidget::addRoadDraftPoint(atlas::geometry::Point2D point) {
    if (snapRoadDraftToGrid_) point = atlas::geometry::snapToGrid(point, 1.0, {});
    if (drawingMode_ == RoadDrawingMode::bezier && draftPoints_.size() >= 4) return;
    if (drawingMode_ == RoadDrawingMode::fixedRadiusArc && draftPoints_.size() >= 2) return;
    draftPoints_.push_back(point);
    draftRedoPoints_.clear();
    provisionalDraftPoint_.reset();
    if (statusHandler_) {
        statusHandler_(QStringLiteral("Road draft: %1 point(s) | %2 m, %3 m")
            .arg(draftPoints_.size()).arg(point.x, 0, 'f', 3).arg(point.y, 0, 'f', 3));
    }
    update();
}

void CanvasWidget::finishRoadDraft() {
    const auto requiredPoints = drawingMode_ == RoadDrawingMode::polyline ? 2U
        : drawingMode_ == RoadDrawingMode::bezier ? 4U : 2U;
    if (draftPoints_.size() < requiredPoints) return;
    auto roadId = QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
    atlas::domain::RoadSpline road;
    road.id = roadId;
    road.mapId = project_.rootMap().id;
    road.primitives = nlohmann::json::array();
    if (drawingMode_ == RoadDrawingMode::bezier) {
        nlohmann::json ids = nlohmann::json::array();
        nlohmann::json points = nlohmann::json::array();
        for (std::size_t index = 0; index < 4; ++index) {
            ids.push_back(roadId + "/control/" + std::to_string(index));
            points.push_back({{"x", draftPoints_[index].x}, {"y", draftPoints_[index].y}});
        }
        road.primitives.push_back({{"id", roadId + "/primitive/0"}, {"kind", "cubic-bezier"},
            {"controlPointIds", std::move(ids)}, {"controlPoints", std::move(points)}});
    } else if (drawingMode_ == RoadDrawingMode::fixedRadiusArc) {
        road.primitives.push_back({{"id", roadId + "/primitive/0"}, {"kind", "circular-arc"},
            {"startControlPointId", roadId + "/control/0"}, {"endControlPointId", roadId + "/control/1"},
            {"start", {{"x", draftPoints_[0].x}, {"y", draftPoints_[0].y}}},
            {"end", {{"x", draftPoints_[1].x}, {"y", draftPoints_[1].y}}},
            {"radius", arcRadiusMeters_}, {"side", "left"}});
    } else {
        for (std::size_t index = 1; index < draftPoints_.size(); ++index) {
            road.primitives.push_back({{"id", roadId + "/primitive/" + std::to_string(index - 1)},
                {"kind", "line"}, {"startControlPointId", roadId + "/control/" + std::to_string(index - 1)},
                {"endControlPointId", roadId + "/control/" + std::to_string(index)},
                {"start", {{"x", draftPoints_[index - 1].x}, {"y", draftPoints_[index - 1].y}}},
                {"end", {{"x", draftPoints_[index].x}, {"y", draftPoints_[index].y}}}});
        }
    }
    draftPoints_.clear();
    draftRedoPoints_.clear();
    provisionalDraftPoint_.reset();
    keyboardCursor_ = {};
    if (roadCreatedHandler_) roadCreatedHandler_(std::move(road));
    update();
}

void CanvasWidget::cancelRoadDraft() {
    draftPoints_.clear();
    draftRedoPoints_.clear();
    provisionalDraftPoint_.reset();
    update();
}

std::optional<std::pair<std::string, double>> CanvasWidget::roadAt(
    atlas::geometry::Point2D point, double toleranceMeters) const {
    const auto candidates = roadCandidatesAt(point, toleranceMeters);
    if (candidates.empty()) return std::nullopt;
    return candidates.front();
}

std::vector<std::pair<std::string, double>> CanvasWidget::roadCandidatesAt(
    atlas::geometry::Point2D point, double toleranceMeters) const {
    const auto& project = previewProject_ ? *previewProject_ : project_;
    struct Candidate {
        std::string roadId;
        double station;
        double distance;
    };
    std::vector<Candidate> candidates;
    for (const auto& road : project.roadSplines()) {
        try {
            const auto curve = curveForRoad(road);
            const auto step = std::max(0.1, std::min(0.5, curve.totalLength() / 1000.0));
            auto bestDistance = toleranceMeters;
            auto bestStation = 0.0;
            for (double station = 0.0; station <= curve.totalLength(); station += step) {
                const auto position = curve.evaluateAtStation(std::min(station, curve.totalLength())).position;
                const auto dx = position.x - point.x;
                const auto dy = position.y - point.y;
                const auto distance = std::hypot(dx, dy);
                if (distance <= bestDistance) {
                    bestDistance = distance;
                    bestStation = std::min(station, curve.totalLength());
                }
            }
            const auto endpoint = curve.evaluateAtStation(curve.totalLength()).position;
            const auto endpointDistance = std::hypot(endpoint.x - point.x, endpoint.y - point.y);
            if (endpointDistance <= bestDistance) {
                bestDistance = endpointDistance;
                bestStation = curve.totalLength();
            }
            if (bestDistance <= toleranceMeters) candidates.push_back({road.id, bestStation, bestDistance});
        } catch (const std::exception&) {
            continue;
        }
    }
    std::sort(candidates.begin(), candidates.end(), [](const Candidate& left, const Candidate& right) {
        if (left.distance != right.distance) return left.distance < right.distance;
        return left.roadId < right.roadId;
    });
    std::vector<std::pair<std::string, double>> result;
    result.reserve(candidates.size());
    for (auto& candidate : candidates) result.emplace_back(std::move(candidate.roadId), candidate.station);
    return result;
}

std::optional<CanvasWidget::ControlPointHit> CanvasWidget::controlPointAt(
    QPointF screenPoint, double tolerancePixels) const {
    auto hits = controlPointsAt(screenPoint, tolerancePixels);
    if (hits.empty()) return std::nullopt;
    return hits.front();
}

std::vector<CanvasWidget::ControlPointHit> CanvasWidget::controlPointsAt(
    QPointF screenPoint, double tolerancePixels) const {
    std::vector<ControlPointHit> hits;
    for (const auto& road : project_.roadSplines()) {
        for (const auto& primitive : road.primitives) {
            const auto kind = primitive.value("kind", "");
            if (kind == "cubic-bezier") {
                const auto& ids = primitive.at("controlPointIds");
                const auto& points = primitive.contains("controlPoints") ? primitive.at("controlPoints")
                                                                          : primitive.at("points");
                for (std::size_t index = 0; index < ids.size(); ++index) {
                    const auto worldPoint = readPoint(points[index]);
                    const auto distance = QLineF(toScreen(camera_, worldPoint), screenPoint).length();
                    const auto pointId = ids[index].get<std::string>();
                    if (distance <= tolerancePixels) hits.push_back({road.id, pointId, worldPoint, distance});
                }
            } else {
                for (const auto& endpoint : {std::string("start"), std::string("end")}) {
                    const auto pointId = primitive.value(endpoint + "ControlPointId", "");
                    if (pointId.empty()) continue;
                    const auto worldPoint = readPoint(primitive.at(endpoint));
                    const auto distance = QLineF(toScreen(camera_, worldPoint), screenPoint).length();
                    if (distance <= tolerancePixels) hits.push_back({road.id, pointId, worldPoint, distance});
                }
            }
        }
    }
    std::sort(hits.begin(), hits.end(), [](const ControlPointHit& left, const ControlPointHit& right) {
        if (left.screenDistance != right.screenDistance) return left.screenDistance < right.screenDistance;
        return std::tie(left.roadSplineId, left.controlPointId) <
            std::tie(right.roadSplineId, right.controlPointId);
    });
    hits.erase(std::unique(hits.begin(), hits.end(), [](const ControlPointHit& left, const ControlPointHit& right) {
        return left.roadSplineId == right.roadSplineId && left.controlPointId == right.controlPointId;
    }), hits.end());
    return hits;
}

std::optional<atlas::geometry::Point2D> CanvasWidget::controlPointPosition(
    const atlas::domain::RoadSpline& road, const std::string& pointId) const {
    for (const auto& primitive : road.primitives) {
        if (primitive.value("kind", "") == "cubic-bezier") {
            const auto& ids = primitive.at("controlPointIds");
            const auto& points = primitive.contains("controlPoints") ? primitive.at("controlPoints")
                                                                        : primitive.at("points");
            for (std::size_t index = 0; index < ids.size(); ++index) {
                if (ids[index].get<std::string>() == pointId) return readPoint(points[index]);
            }
        } else {
            for (const auto& endpoint : {std::string("start"), std::string("end")}) {
                if (primitive.value(endpoint + "ControlPointId", "") == pointId) {
                    return readPoint(primitive.at(endpoint));
                }
            }
        }
    }
    return std::nullopt;
}

std::pair<atlas::geometry::Point2D, atlas::geometry::Point2D> CanvasWidget::transformAxes(
    const std::string& targetId,
    bool roadSpline) const {
    atlas::geometry::Point2D axisX{1.0, 0.0};
    atlas::geometry::Point2D axisY{0.0, 1.0};
    if (transformSpace_ != TransformSpace::local || !roadSpline) return {axisX, axisY};
    const auto road = std::find_if(project_.roadSplines().begin(), project_.roadSplines().end(),
        [&](const auto& candidate) { return candidate.id == targetId; });
    if (road == project_.roadSplines().end()) return {axisX, axisY};
    try {
        const auto curve = curveForRoad(*road);
        const auto evaluation = curve.evaluateAtStation(curve.totalLength() * 0.5);
        const auto tangentLength = std::hypot(evaluation.tangent.x, evaluation.tangent.y);
        if (tangentLength <= 1.0e-12) return {axisX, axisY};
        axisX = {evaluation.tangent.x / tangentLength, evaluation.tangent.y / tangentLength};
        if (road->direction == "end-to-start") {
            axisX.x = -axisX.x;
            axisX.y = -axisX.y;
        }
        axisY = {-axisX.y, axisX.x};
    } catch (const std::exception&) {
        return {{1.0, 0.0}, {0.0, 1.0}};
    }
    return {axisX, axisY};
}

std::optional<CanvasWidget::TransformGizmoHit> CanvasWidget::transformGizmoHit(QPointF screenPoint) const {
    const auto selectedId = selection_.primaryId();
    atlas::geometry::Point2D origin;
    bool isRoadSpline = false;
    const auto road = std::find_if(project_.roadSplines().begin(), project_.roadSplines().end(),
        [&](const auto& candidate) { return candidate.id == selectedId; });
    if (road != project_.roadSplines().end()) {
        try {
            const auto curve = curveForRoad(*road);
            origin = curve.evaluateAtStation(curve.totalLength() * 0.5).position;
            isRoadSpline = true;
        } catch (const std::exception&) {
            return std::nullopt;
        }
    } else {
        const auto* object = project_.rootMap().findObject(selectedId);
        if (object == nullptr || object->type() != "core.Point" || object->locked() ||
            !object->geometry().is_object() || !object->geometry().contains("x") ||
            !object->geometry().contains("y")) return std::nullopt;
        origin = {object->geometry().value("x", 0.0), object->geometry().value("y", 0.0)};
    }

    const auto center = toScreen(camera_, origin);
    const auto [axisX, axisY] = transformAxes(selectedId, isRoadSpline);
    const auto axisSegment = [&](atlas::geometry::Point2D axis) {
        const auto start = atlas::geometry::Point2D{
            origin.x + axis.x * 16.0 / camera_.zoom(), origin.y + axis.y * 16.0 / camera_.zoom()};
        const auto end = atlas::geometry::Point2D{
            origin.x + axis.x * 42.0 / camera_.zoom(), origin.y + axis.y * 42.0 / camera_.zoom()};
        return std::pair{toScreen(camera_, start), toScreen(camera_, end)};
    };
    const auto distanceToSegment = [screenPoint](QPointF start, QPointF end) {
        const auto dx = end.x() - start.x();
        const auto dy = end.y() - start.y();
        const auto lengthSquared = dx * dx + dy * dy;
        const auto projection = lengthSquared == 0.0 ? 0.0
            : std::clamp(((screenPoint.x() - start.x()) * dx + (screenPoint.y() - start.y()) * dy) /
                lengthSquared, 0.0, 1.0);
        return QLineF(screenPoint, {start.x() + projection * dx, start.y() + projection * dy}).length();
    };
    if (isRoadSpline && std::abs(QLineF(center, screenPoint).length() - 54.0) <= 7.0) {
        return TransformGizmoHit{TransformHandle::rotation, selectedId, origin, true};
    }
    const auto [xStart, xEnd] = axisSegment(axisX);
    const auto [yStart, yEnd] = axisSegment(axisY);
    if (distanceToSegment(xStart, xEnd) <= 7.0) {
        return TransformGizmoHit{TransformHandle::worldX, selectedId, origin, isRoadSpline};
    }
    if (distanceToSegment(yStart, yEnd) <= 7.0) {
        return TransformGizmoHit{TransformHandle::worldY, selectedId, origin, isRoadSpline};
    }
    return std::nullopt;
}

bool CanvasWidget::transformRoad(
    atlas::domain::RoadSpline& road,
    atlas::geometry::Point2D origin,
    double translateX,
    double translateY,
    double rotationRadians) const {
    const auto cosine = std::cos(rotationRadians);
    const auto sine = std::sin(rotationRadians);
    bool changed = false;
    const auto transformPoint = [&](nlohmann::json& point) {
        const auto x = point.value("x", 0.0) - origin.x;
        const auto y = point.value("y", 0.0) - origin.y;
        point["x"] = origin.x + x * cosine - y * sine + translateX;
        point["y"] = origin.y + x * sine + y * cosine + translateY;
        changed = true;
    };
    for (auto& primitive : road.primitives) {
        if (primitive.value("kind", "") == "cubic-bezier") {
            auto& points = primitive.contains("controlPoints") ? primitive["controlPoints"] : primitive["points"];
            for (auto& point : points) transformPoint(point);
        } else {
            transformPoint(primitive["start"]);
            transformPoint(primitive["end"]);
        }
    }
    return changed;
}

bool CanvasWidget::moveControlPoint(
    atlas::domain::RoadSpline& road,
    const std::string& pointId,
    atlas::geometry::Point2D position) const {
    bool changed = false;
    for (auto& item : road.primitives) {
        const auto kind = item.value("kind", "");
        if (kind == "cubic-bezier") {
            auto& ids = item["controlPointIds"];
            auto& points = item.contains("controlPoints") ? item["controlPoints"] : item["points"];
            for (std::size_t index = 0; index < ids.size(); ++index) {
                if (ids[index].get<std::string>() == pointId) {
                    points[index] = {{"x", position.x}, {"y", position.y}};
                    changed = true;
                }
            }
        } else {
            for (const auto& endpoint : {std::string("start"), std::string("end")}) {
                if (item.value(endpoint + "ControlPointId", "") == pointId) {
                    item[endpoint] = {{"x", position.x}, {"y", position.y}};
                    changed = true;
                }
            }
        }
    }
    return changed;
}

void CanvasWidget::drawGrid(QPainter& painter) {
    const auto viewport = camera_.viewport();
    const auto topLeft = camera_.screenToWorld({0.0, 0.0});
    const auto bottomRight = camera_.screenToWorld({viewport.width, viewport.height});
    const auto worldExtent = std::max(
        std::abs(bottomRight.x - topLeft.x), std::abs(bottomRight.y - topLeft.y));
    const auto rawStep = worldExtent / 12.0;
    const auto exponent = std::floor(std::log10(std::max(rawStep, 1.0e-12)));
    const auto base = std::pow(10.0, exponent);
    const auto normalized = rawStep / base;
    const auto step = (normalized < 2.0 ? 1.0 : normalized < 5.0 ? 2.0 : 5.0) * base;

    painter.setPen(QColor(45, 49, 56));
    const auto firstX = std::floor(topLeft.x / step) * step;
    for (auto x = firstX; x <= bottomRight.x; x += step) {
        const auto screen = camera_.worldToScreen({x, 0.0});
        painter.drawLine(QPointF(screen.x, 0.0), QPointF(screen.x, viewport.height));
    }
    const auto firstY = std::floor(bottomRight.y / step) * step;
    for (auto y = firstY; y <= topLeft.y; y += step) {
        const auto screen = camera_.worldToScreen({0.0, y});
        painter.drawLine(QPointF(0.0, screen.y), QPointF(viewport.width, screen.y));
    }

    painter.setPen(QPen(QColor(180, 190, 205), 1.5));
    const auto horizontalAxis = camera_.worldToScreen({0.0, 0.0});
    painter.drawLine(QPointF(0.0, horizontalAxis.y), QPointF(viewport.width, horizontalAxis.y));
    painter.drawLine(QPointF(horizontalAxis.x, 0.0), QPointF(horizontalAxis.x, viewport.height));
}

std::vector<atlas::render::HitTarget> CanvasWidget::hitTargets() const {
    std::vector<atlas::render::HitTarget> targets;
    for (const auto& object : project_.rootMap().objects) {
        if (!object.geometry().is_object() || !object.geometry().contains("x") ||
            !object.geometry().contains("y")) {
            continue;
        }
        targets.push_back({
            object.id(), object.type(),
            {object.geometry().value("x", 0.0), object.geometry().value("y", 0.0)},
            object.type() == "core.Point" ? 0 : 1,
            object.visible(), object.locked()});
    }
    return targets;
}

std::string CanvasWidget::segmentAt(const std::string& roadSplineId, double station) const {
    const auto& project = previewProject_ ? *previewProject_ : project_;
    const auto road = std::find_if(project.roadSplines().begin(), project.roadSplines().end(),
        [&](const auto& candidate) { return candidate.id == roadSplineId; });
    if (road == project.roadSplines().end()) return {};
    for (std::size_t index = 0; index < road->segmentIds.size(); ++index) {
        const auto segment = std::find_if(project.roadSegments().begin(), project.roadSegments().end(),
            [&](const auto& candidate) { return candidate.id == road->segmentIds[index]; });
        if (segment == project.roadSegments().end()) continue;
        const auto startStation = stationForAnchor(*road, segment->startAnchorId);
        const auto endStation = stationForAnchor(*road, segment->endAnchorId);
        if (station >= startStation && (station < endStation || index + 1 == road->segmentIds.size())) {
            return segment->id;
        }
    }
    return {};
}

void CanvasWidget::drawObjects(QPainter& painter) {
    for (const auto& object : project_.rootMap().objects) {
        if (!object.visible() || !object.geometry().is_object() ||
            !object.geometry().contains("x") || !object.geometry().contains("y")) {
            continue;
        }
        const auto& displayedObject = draggedMapObject_ && draggedMapObject_->id() == object.id()
            ? *draggedMapObject_ : object;
        const auto screen = camera_.worldToScreen({
            displayedObject.geometry().value("x", 0.0), displayedObject.geometry().value("y", 0.0)});
        const auto selected = object.id() == selection_.primaryId();
        const auto hovered = object.id() == hoveredObjectId_;
        const auto color = object.locked() ? QColor(145, 150, 160) : QColor(90, 190, 255);
        painter.setPen(QPen(selected ? QColor(255, 220, 100) : hovered ? QColor(125, 210, 255) : color,
            selected || hovered ? 3.0 : 2.0));
        painter.setBrush(object.locked() ? Qt::NoBrush : color);
        const auto radius = selected ? 7.0 : hovered ? 6.5 : 5.0;
        painter.drawEllipse(QPointF(screen.x, screen.y), radius, radius);
        if (object.locked()) {
            painter.drawLine(QPointF(screen.x - 6.0, screen.y - 6.0), QPointF(screen.x + 6.0, screen.y + 6.0));
        }
    }
}

void CanvasWidget::drawMeasurement(QPainter& painter) {
    if (tool_ != Tool::measure || !firstMeasurePoint_) return;
    const auto start = toScreen(camera_, firstMeasurePoint_->position);
    painter.setPen(QPen(QColor(255, 220, 100), 2.0));
    painter.setBrush(QColor(255, 220, 100));
    painter.drawEllipse(start, 6.0, 6.0);
    const auto* endPoint = secondMeasurePoint_ ? &*secondMeasurePoint_
        : hoveredMeasurementPoint_ ? &*hoveredMeasurementPoint_ : nullptr;
    if (endPoint == nullptr || endPoint->roadSplineId != firstMeasurePoint_->roadSplineId) return;
    const auto end = toScreen(camera_, endPoint->position);
    painter.setPen(QPen(QColor(105, 225, 205), 2.0,
        secondMeasurePoint_ ? Qt::SolidLine : Qt::DashLine));
    painter.setBrush(QColor(105, 225, 205));
    painter.drawLine(start, end);
    painter.drawEllipse(end, 6.0, 6.0);
    const auto midpoint = (start + end) * 0.5 + QPointF(8.0, -8.0);
    painter.setPen(QColor(245, 250, 255));
    painter.drawText(midpoint, QStringLiteral("%1 m")
        .arg(std::abs(endPoint->station - firstMeasurePoint_->station), 0, 'f', 3));
}

void CanvasWidget::drawTransformGizmo(QPainter& painter) {
    if (tool_ != Tool::select && tool_ != Tool::edit) return;
    const auto selectedId = selection_.primaryId();
    atlas::geometry::Point2D origin;
    bool isRoadSpline = false;
    const auto road = std::find_if(project_.roadSplines().begin(), project_.roadSplines().end(),
        [&](const auto& candidate) { return candidate.id == selectedId; });
    if (road != project_.roadSplines().end()) {
        try {
            const auto curve = curveForRoad(*road);
            origin = curve.evaluateAtStation(curve.totalLength() * 0.5).position;
            isRoadSpline = true;
        } catch (const std::exception&) {
            return;
        }
    } else {
        const auto* object = project_.rootMap().findObject(selectedId);
        if (object == nullptr || object->type() != "core.Point" || object->locked() ||
            !object->geometry().is_object() || !object->geometry().contains("x") ||
            !object->geometry().contains("y")) return;
        origin = {object->geometry().value("x", 0.0), object->geometry().value("y", 0.0)};
    }

    const auto center = toScreen(camera_, origin);
    const auto [axisX, axisY] = transformAxes(selectedId, isRoadSpline);
    const auto xEnd = toScreen(camera_, {origin.x + axisX.x * 42.0 / camera_.zoom(),
        origin.y + axisX.y * 42.0 / camera_.zoom()});
    const auto yEnd = toScreen(camera_, {origin.x + axisY.x * 42.0 / camera_.zoom(),
        origin.y + axisY.y * 42.0 / camera_.zoom()});
    painter.save();
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(QColor(245, 105, 85), 3.0));
    painter.drawLine(center, xEnd);
    painter.setPen(QPen(QColor(105, 225, 205), 3.0));
    painter.drawLine(center, yEnd);
    painter.setPen(QColor(255, 255, 255));
    painter.drawText(xEnd + QPointF(xEnd.x() >= center.x() ? 5.0 : -12.0,
        xEnd.y() >= center.y() ? 12.0 : -5.0),
        QStringLiteral("X"));
    painter.drawText(yEnd + QPointF(yEnd.x() >= center.x() ? 5.0 : -12.0,
        yEnd.y() >= center.y() ? 12.0 : -5.0),
        QStringLiteral("Y"));
    painter.setPen(QPen(QColor(220, 225, 235), 1.5));
    painter.drawText(center + QPointF(-18.0, 72.0),
        transformSpace_ == TransformSpace::local && isRoadSpline ? QStringLiteral("LOCAL") : QStringLiteral("WORLD"));
    if (isRoadSpline) {
        painter.setPen(QPen(QColor(235, 225, 150), 1.5));
        painter.drawEllipse(center, 54.0, 54.0);
        painter.setBrush(QColor(235, 225, 150));
        painter.drawEllipse(center + QPointF(38.0, -38.0), 5.0, 5.0);
        painter.setPen(QColor(255, 255, 255));
        painter.drawText(center + QPointF(43.0, -40.0), QStringLiteral("R"));
    }
    painter.restore();
}

void CanvasWidget::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        const auto position = event->position();
        lastPointerScreen_ = position;
        const auto renderWorld = camera_.screenToWorld({position.x(), position.y()});
        const atlas::geometry::Point2D world{renderWorld.x, renderWorld.y};
        lastPointerWorld_ = world;
        hasLastPointerWorld_ = true;
        keyboardCursor_ = world;
        if (tool_ == Tool::select || tool_ == Tool::edit) {
            if (const auto gizmoHit = transformGizmoHit(position)) {
                activeTransformHandle_ = gizmoHit->handle;
                setAccessibleDescription(gizmoHit->handle == TransformHandle::rotation
                    ? QStringLiteral("World rotation handle. Drag to rotate the selected RoadSpline.")
                    : gizmoHit->handle == TransformHandle::worldX
                        ? QStringLiteral("World X translation handle. Drag to move along the X axis.")
                        : QStringLiteral("World Y translation handle. Drag to move along the Y axis."));
                transformOrigin_ = gizmoHit->origin;
                transformStartPointer_ = world;
                transformStartAngle_ = std::atan2(world.y - transformOrigin_.y, world.x - transformOrigin_.x);
                draggingTransformGizmo_ = true;
                if (gizmoHit->roadSpline) {
                    const auto road = std::find_if(project_.roadSplines().begin(), project_.roadSplines().end(),
                        [&](const auto& candidate) { return candidate.id == gizmoHit->targetId; });
                    if (road != project_.roadSplines().end()) {
                        draggedRoad_ = *road;
                        draggedRoadOriginal_ = *road;
                        controlPointDragChanged_ = false;
                    }
                } else {
                    const auto* object = project_.rootMap().findObject(gizmoHit->targetId);
                    if (object != nullptr) {
                        draggedMapObject_ = *object;
                        draggedMapObjectOriginal_ = *object;
                        mapObjectDragChanged_ = false;
                    }
                }
                update();
                event->accept();
                return;
            }
            if (const auto hit = controlPointAt(position, controlPointHitRadiusPixels)) {
                selection_.select(hit->roadSplineId);
                if (roadSelectedHandler_) roadSelectedHandler_(hit->roadSplineId);
                const auto road = std::find_if(project_.roadSplines().begin(), project_.roadSplines().end(),
                    [&](const auto& candidate) { return candidate.id == hit->roadSplineId; });
                if (road != project_.roadSplines().end()) {
                    draggedRoad_ = *road;
                    draggedRoadOriginal_ = *road;
                    draggedControlPointId_ = hit->controlPointId;
                    selectedControlPointId_ = hit->controlPointId;
                    selectedControlPointRoadId_ = hit->roadSplineId;
                    setAccessibleDescription(QStringLiteral("Focused road control point %1. Use arrow keys to move it.")
                        .arg(QString::fromStdString(hit->controlPointId)));
                    controlPointDragChanged_ = false;
                    draggingControlPoint_ = true;
                    update();
                    event->accept();
                    return;
                }
            }
            const auto hits = hitTester_.orderedHits(world, hitTargets(), canvasTargetHitRadiusPixels / camera_.zoom());
            if (!hits.empty()) {
                const auto object = project_.rootMap().findObject(hits.front().objectId);
                if (object != nullptr) {
                    selection_.select(object->id());
                    selectedControlPointId_.clear();
                    selectedControlPointRoadId_.clear();
                    transformSpace_ = TransformSpace::world;
                    if (selectionChangedHandler_) selectionChangedHandler_(false);
                    if (object->type() == "core.Point" && !object->locked()) {
                        draggedMapObject_ = *object;
                        draggingMapObject_ = true;
                        mapObjectDragChanged_ = false;
                    }
                    update();
                    event->accept();
                    return;
                }
            }
        }
        if (tool_ == Tool::road) {
            provisionalDraftPoint_ = snapRoadDraftToGrid_
                ? atlas::geometry::snapToGrid(world, 1.0, {}) : world;
            keyboardCursor_ = *provisionalDraftPoint_;
            update();
            event->accept();
            return;
        }
        if (tool_ == Tool::split) {
            if (const auto road = roadAt(world, roadHitRadiusPixels / camera_.zoom()); road && roadStationHandler_) {
                roadStationHandler_(road->first, road->second);
            }
            event->accept();
            return;
        }
        if (tool_ == Tool::measure) {
            if (const auto road = roadAt(world, roadHitRadiusPixels / camera_.zoom())) {
                const auto roadSpline = std::find_if(project_.roadSplines().begin(), project_.roadSplines().end(),
                    [&](const auto& candidate) { return candidate.id == road->first; });
                if (roadSpline != project_.roadSplines().end()) {
                    try {
                        const auto position = curveForRoad(*roadSpline).evaluateAtStation(road->second).position;
                        const MeasurementPoint point{road->first, road->second, position};
                        if (!firstMeasurePoint_ || secondMeasurePoint_) {
                            firstMeasurePoint_ = point;
                            secondMeasurePoint_.reset();
                            hoveredMeasurementPoint_.reset();
                        } else if (firstMeasurePoint_->roadSplineId == road->first) {
                            secondMeasurePoint_ = point;
                            hoveredMeasurementPoint_.reset();
                        } else if (statusHandler_) {
                            statusHandler_(QStringLiteral("Choose both measurement points on the same RoadSpline."));
                        }
                        if (statusHandler_ && !secondMeasurePoint_) {
                            statusHandler_(QStringLiteral("Measure start: %1 at %2 m")
                                .arg(QString::fromStdString(road->first)).arg(road->second, 0, 'f', 3));
                        } else if (statusHandler_ && secondMeasurePoint_) {
                            statusHandler_(QStringLiteral("Road distance: %1 m")
                                .arg(std::abs(secondMeasurePoint_->station - firstMeasurePoint_->station), 0, 'f', 3));
                        }
                        update();
                    } catch (const std::exception&) {
                        if (statusHandler_) statusHandler_(QStringLiteral("Unable to measure this road station."));
                    }
                }
            }
            event->accept();
            return;
        }
        overlapCandidateIds_.clear();
        for (const auto& road : roadCandidatesAt(world, canvasTargetHitRadiusPixels / camera_.zoom())) {
            overlapCandidateIds_.push_back(road.first);
        }
        const auto hits = hitTester_.orderedHits(world, hitTargets(), canvasTargetHitRadiusPixels / camera_.zoom());
        for (const auto& hit : hits) {
            if (std::find(overlapCandidateIds_.begin(), overlapCandidateIds_.end(), hit.objectId) ==
                overlapCandidateIds_.end()) overlapCandidateIds_.push_back(hit.objectId);
        }
        if (!overlapCandidateIds_.empty()) {
            selection_.select(overlapCandidateIds_.front());
            selectedControlPointId_.clear();
            selectedControlPointRoadId_.clear();
            const auto selectedRoad = std::find_if(project_.roadSplines().begin(), project_.roadSplines().end(),
                [&](const auto& road) { return road.id == selection_.primaryId(); });
            if (selectionChangedHandler_) selectionChangedHandler_(selectedRoad != project_.roadSplines().end());
            if (selectedRoad != project_.roadSplines().end()) {
                if (roadSelectedHandler_) roadSelectedHandler_(selection_.primaryId());
                const auto roadHit = roadAt(world, canvasTargetHitRadiusPixels / camera_.zoom());
                if (roadHit && roadHit->first == selectedRoad->id && segmentSelectedHandler_) {
                    const auto segmentId = segmentAt(roadHit->first, roadHit->second);
                    if (!segmentId.empty()) segmentSelectedHandler_(roadHit->first, segmentId);
                }
            }
        } else selection_.clear();
        update();
        event->accept();
        return;
    }
    if (event->button() == Qt::MiddleButton) {
        panning_ = true;
        lastMousePosition_ = event->position().toPoint();
        event->accept();
        return;
    }
    QOpenGLWidget::mousePressEvent(event);
}

void CanvasWidget::mouseMoveEvent(QMouseEvent* event) {
    if (panning_) {
        const auto currentPosition = event->position().toPoint();
        const auto delta = currentPosition - lastMousePosition_;
        camera_.panByScreenDelta({static_cast<double>(delta.x()), static_cast<double>(delta.y())});
        lastMousePosition_ = currentPosition;
        update();
        event->accept();
        return;
    }
    if (provisionalDraftPoint_ && (event->buttons() & Qt::LeftButton)) {
        const auto position = event->position();
        const auto worldPosition = camera_.screenToWorld({position.x(), position.y()});
        const atlas::geometry::Point2D world{worldPosition.x, worldPosition.y};
        provisionalDraftPoint_ = snapRoadDraftToGrid_ ? atlas::geometry::snapToGrid(world, 1.0, {}) : world;
        keyboardCursor_ = *provisionalDraftPoint_;
        update();
        event->accept();
        return;
    }
    if (draggingTransformGizmo_) {
        const auto position = event->position();
        const auto worldPosition = camera_.screenToWorld({position.x(), position.y()});
        const atlas::geometry::Point2D world{worldPosition.x, worldPosition.y};
        const auto targetId = draggedRoadOriginal_ ? draggedRoadOriginal_->id
            : draggedMapObjectOriginal_ ? draggedMapObjectOriginal_->id() : std::string{};
        const auto [axisX, axisY] = transformAxes(targetId, draggedRoadOriginal_.has_value());
        const auto axis = activeTransformHandle_ == TransformHandle::worldX ? axisX : axisY;
        const auto pointerDeltaX = world.x - transformStartPointer_.x;
        const auto pointerDeltaY = world.y - transformStartPointer_.y;
        const auto projectedDelta = pointerDeltaX * axis.x + pointerDeltaY * axis.y;
        const auto translateX = axis.x * projectedDelta;
        const auto translateY = axis.y * projectedDelta;
        if (draggedRoadOriginal_ && draggedRoad_) {
            double rotation = 0.0;
            if (activeTransformHandle_ == TransformHandle::rotation) {
                rotation = std::atan2(world.y - transformOrigin_.y, world.x - transformOrigin_.x) -
                    transformStartAngle_;
            }
            *draggedRoad_ = *draggedRoadOriginal_;
            transformRoad(*draggedRoad_, transformOrigin_,
                activeTransformHandle_ == TransformHandle::rotation ? 0.0 : translateX,
                activeTransformHandle_ == TransformHandle::rotation ? 0.0 : translateY,
                rotation);
            controlPointDragChanged_ = draggedRoad_->normalizedJson() != draggedRoadOriginal_->normalizedJson();
        } else if (draggedMapObjectOriginal_) {
            auto geometry = draggedMapObjectOriginal_->geometry();
            geometry["x"] = transformOrigin_.x + translateX;
            geometry["y"] = transformOrigin_.y + translateY;
            draggedMapObject_ = draggedMapObjectOriginal_->withGeometry(std::move(geometry));
            mapObjectDragChanged_ = draggedMapObject_->normalizedJson() !=
                draggedMapObjectOriginal_->normalizedJson();
        }
        update();
        event->accept();
        return;
    }
    if (draggingControlPoint_ && draggedRoad_) {
        const auto position = event->position();
        const auto worldPosition = camera_.screenToWorld({position.x(), position.y()});
        moveControlPoint(*draggedRoad_, draggedControlPointId_, {worldPosition.x, worldPosition.y});
        controlPointDragChanged_ = draggedRoadOriginal_ &&
            draggedRoad_->normalizedJson() != draggedRoadOriginal_->normalizedJson();
        update();
        event->accept();
        return;
    }
    if (draggingMapObject_ && draggedMapObject_) {
        const auto position = event->position();
        const auto worldPosition = camera_.screenToWorld({position.x(), position.y()});
        auto geometry = draggedMapObject_->geometry();
        geometry["x"] = worldPosition.x;
        geometry["y"] = worldPosition.y;
        draggedMapObject_ = draggedMapObject_->withGeometry(std::move(geometry));
        mapObjectDragChanged_ = true;
        update();
        event->accept();
        return;
    }
    const auto position = event->position();
    lastPointerScreen_ = position;
    const auto world = camera_.screenToWorld({position.x(), position.y()});
    lastPointerWorld_ = {world.x, world.y};
    hasLastPointerWorld_ = true;
    const auto controlPoint = (tool_ == Tool::select || tool_ == Tool::edit)
        ? controlPointAt(position, controlPointHitRadiusPixels) : std::nullopt;
    const auto objectHits = (tool_ == Tool::select || tool_ == Tool::edit)
        ? hitTester_.orderedHits(lastPointerWorld_, hitTargets(), canvasTargetHitRadiusPixels / camera_.zoom())
        : std::vector<atlas::render::HitResult>{};
    const auto newHoveredObjectId = objectHits.empty() ? std::string{} : objectHits.front().objectId;
    const auto roadHit = (tool_ == Tool::select || tool_ == Tool::edit)
        ? roadAt(lastPointerWorld_, canvasTargetHitRadiusPixels / camera_.zoom()) : std::nullopt;
    const auto newHoveredRoadId = roadHit ? roadHit->first : std::string{};
    const auto newHoveredSegmentId = roadHit ? segmentAt(roadHit->first, roadHit->second) : std::string{};
    if (tool_ == Tool::measure && firstMeasurePoint_) {
        hoveredMeasurementPoint_.reset();
        if (const auto measureHit = roadAt(lastPointerWorld_, roadHitRadiusPixels / camera_.zoom());
            measureHit && measureHit->first == firstMeasurePoint_->roadSplineId) {
            const auto road = std::find_if(project_.roadSplines().begin(), project_.roadSplines().end(),
                [&](const auto& candidate) { return candidate.id == measureHit->first; });
            if (road != project_.roadSplines().end()) {
                try {
                    const auto position = curveForRoad(*road).evaluateAtStation(measureHit->second).position;
                    hoveredMeasurementPoint_ = MeasurementPoint{measureHit->first, measureHit->second, position};
                } catch (const std::exception&) {
                }
            }
        }
        update();
    }
    const auto controlPointChanged = controlPoint.has_value() != hoveredControlPoint_.has_value() ||
        (controlPoint && hoveredControlPoint_ && (controlPoint->roadSplineId != hoveredControlPoint_->roadSplineId ||
            controlPoint->controlPointId != hoveredControlPoint_->controlPointId));
    if (controlPointChanged || newHoveredRoadId != hoveredRoadId_ || newHoveredObjectId != hoveredObjectId_ ||
        newHoveredSegmentId != hoveredSegmentId_) {
        hoveredControlPoint_ = controlPoint;
        hoveredRoadId_ = newHoveredRoadId;
        hoveredObjectId_ = newHoveredObjectId;
        hoveredSegmentId_ = newHoveredSegmentId;
        if (controlPoint) {
            setAccessibleDescription(QStringLiteral("Road control point target %1. Press Tab to focus; use arrow keys to move.")
                .arg(QString::fromStdString(controlPoint->controlPointId)));
        } else if (!newHoveredSegmentId.empty()) {
            setAccessibleDescription(QStringLiteral("RoadSegment target %1.")
                .arg(QString::fromStdString(newHoveredSegmentId)));
        } else if (!newHoveredRoadId.empty()) {
            setAccessibleDescription(QStringLiteral("RoadSpline target %1.")
                .arg(QString::fromStdString(newHoveredRoadId)));
        } else if (!newHoveredObjectId.empty()) {
            setAccessibleDescription(QStringLiteral("Map object target %1.")
                .arg(QString::fromStdString(newHoveredObjectId)));
        }
        update();
    }
    if (statusHandler_) {
        auto message = QStringLiteral("Coordinates: %1 m, %2 m")
            .arg(world.x, 0, 'f', 3).arg(world.y, 0, 'f', 3);
        if (!selection_.primaryId().empty()) {
            const auto road = std::find_if(project_.roadSplines().begin(), project_.roadSplines().end(),
                [&](const auto& candidate) { return candidate.id == selection_.primaryId(); });
            if (road != project_.roadSplines().end()) {
                if (const auto hit = roadAt(lastPointerWorld_, roadHitRadiusPixels / camera_.zoom());
                    hit && hit->first == road->id) {
                    message += QStringLiteral(" | Road station: %1 m").arg(hit->second, 0, 'f', 3);
                }
            }
        }
        statusHandler_(std::move(message));
    }
    QOpenGLWidget::mouseMoveEvent(event);
}

void CanvasWidget::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() == Qt::MiddleButton) {
        panning_ = false;
        event->accept();
        return;
    }
    if (event->button() == Qt::LeftButton && provisionalDraftPoint_) {
        addRoadDraftPoint(*provisionalDraftPoint_);
        event->accept();
        return;
    }
    if (event->button() == Qt::LeftButton && draggingTransformGizmo_) {
        draggingTransformGizmo_ = false;
        if (controlPointDragChanged_ && draggedRoad_ && roadEditedHandler_) {
            roadEditedHandler_(std::move(*draggedRoad_));
        } else if (mapObjectDragChanged_ && draggedMapObject_ && mapObjectEditedHandler_) {
            mapObjectEditedHandler_(std::move(*draggedMapObject_));
        }
        draggedRoad_.reset();
        draggedRoadOriginal_.reset();
        draggedMapObject_.reset();
        draggedMapObjectOriginal_.reset();
        controlPointDragChanged_ = false;
        mapObjectDragChanged_ = false;
        update();
        event->accept();
        return;
    }
    if (event->button() == Qt::LeftButton && draggingControlPoint_ && draggedRoad_) {
        draggingControlPoint_ = false;
        draggedControlPointId_.clear();
        if (controlPointDragChanged_ && roadEditedHandler_) roadEditedHandler_(std::move(*draggedRoad_));
        draggedRoad_.reset();
        draggedRoadOriginal_.reset();
        controlPointDragChanged_ = false;
        update();
        event->accept();
        return;
    }
    if (event->button() == Qt::LeftButton && draggingMapObject_ && draggedMapObject_) {
        draggingMapObject_ = false;
        if (mapObjectDragChanged_ && mapObjectEditedHandler_) {
            mapObjectEditedHandler_(std::move(*draggedMapObject_));
        }
        draggedMapObject_.reset();
        mapObjectDragChanged_ = false;
        update();
        event->accept();
        return;
    }
    QOpenGLWidget::mouseReleaseEvent(event);
}

void CanvasWidget::mouseDoubleClickEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton && tool_ == Tool::road) {
        provisionalDraftPoint_.reset();
        finishRoadDraft();
        event->accept();
        return;
    }
    QOpenGLWidget::mouseDoubleClickEvent(event);
}

void CanvasWidget::wheelEvent(QWheelEvent* event) {
    const auto steps = static_cast<double>(event->angleDelta().y()) / 120.0;
    const auto factor = std::pow(1.2, steps);
    const auto position = event->position();
    camera_.zoomAt({position.x(), position.y()}, factor);
    if (viewChangedHandler_) viewChangedHandler_(camera_.zoom());
    update();
    event->accept();
}

void CanvasWidget::cycleCanvasSelection(bool reverse) {
    const auto viewport = camera_.viewport();
    const auto pointer = hasLastPointerWorld_
        ? lastPointerScreen_ : QPointF(viewport.width * 0.5, viewport.height * 0.5);
    auto controlPoints = controlPointsAt(pointer, controlPointHitRadiusPixels);
    if (controlPoints.empty()) {
        controlPoints = controlPointsAt(pointer, std::numeric_limits<double>::max());
    }
    if (!controlPoints.empty()) {
        const auto current = std::find_if(controlPoints.begin(), controlPoints.end(), [&](const auto& hit) {
            return hit.roadSplineId == selectedControlPointRoadId_ &&
                hit.controlPointId == selectedControlPointId_;
        });
        const auto direction = reverse ? -1 : 1;
        auto index = current == controlPoints.end() ? (direction < 0 ? 0 : -1)
            : static_cast<int>(std::distance(controlPoints.begin(), current));
        index = (index + direction + static_cast<int>(controlPoints.size())) %
            static_cast<int>(controlPoints.size());
        const auto& hit = controlPoints[static_cast<std::size_t>(index)];
        selectedControlPointRoadId_ = hit.roadSplineId;
        selectedControlPointId_ = hit.controlPointId;
        selection_.select(hit.roadSplineId);
        if (roadSelectedHandler_) roadSelectedHandler_(hit.roadSplineId);
        setAccessibleDescription(QStringLiteral("Focused road control point %1. Use arrow keys to move it.")
            .arg(QString::fromStdString(hit.controlPointId)));
        update();
        return;
    }

    auto candidates = overlapCandidateIds_;
    if (hasLastPointerWorld_) {
        candidates.clear();
        for (const auto& road : roadCandidatesAt(lastPointerWorld_, canvasTargetHitRadiusPixels / camera_.zoom())) {
            candidates.push_back(road.first);
        }
        const auto hits = hitTester_.orderedHits(
            lastPointerWorld_, hitTargets(), canvasTargetHitRadiusPixels / camera_.zoom());
        for (const auto& hit : hits) {
            if (std::find(candidates.begin(), candidates.end(), hit.objectId) == candidates.end()) {
                candidates.push_back(hit.objectId);
            }
        }
    }
    if (candidates.empty()) {
        for (const auto& road : project_.roadSplines()) candidates.push_back(road.id);
        std::sort(candidates.begin(), candidates.end());
        for (const auto& target : hitTargets()) candidates.push_back(target.objectId);
    }
    selection_.cycle(candidates, reverse);
    selectedControlPointId_.clear();
    selectedControlPointRoadId_.clear();
    const auto selectedRoad = std::any_of(project_.roadSplines().begin(), project_.roadSplines().end(),
        [&](const auto& road) { return road.id == selection_.primaryId(); });
    if (!selectedRoad) transformSpace_ = TransformSpace::world;
    if (selectionChangedHandler_) selectionChangedHandler_(selectedRoad);
    if (roadSelectedHandler_ && std::any_of(project_.roadSplines().begin(), project_.roadSplines().end(),
        [&](const auto& road) { return road.id == selection_.primaryId(); })) {
        roadSelectedHandler_(selection_.primaryId());
    }
    update();
}

void CanvasWidget::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Escape && provisionalDraftPoint_) {
        provisionalDraftPoint_.reset();
        update();
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_Escape && draggingTransformGizmo_) {
        draggingTransformGizmo_ = false;
        draggedRoad_.reset();
        draggedRoadOriginal_.reset();
        draggedMapObject_.reset();
        draggedMapObjectOriginal_.reset();
        controlPointDragChanged_ = false;
        mapObjectDragChanged_ = false;
        update();
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_Escape && draggingMapObject_) {
        draggingMapObject_ = false;
        draggedMapObject_.reset();
        mapObjectDragChanged_ = false;
        update();
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_Escape && draggingControlPoint_) {
        draggingControlPoint_ = false;
        draggedControlPointId_.clear();
        draggedRoad_.reset();
        draggedRoadOriginal_.reset();
        controlPointDragChanged_ = false;
        selectedControlPointId_.clear();
        selectedControlPointRoadId_.clear();
        update();
        event->accept();
        return;
    }
    const auto viewport = camera_.viewport();
    const auto center = atlas::render::Point2D{viewport.width * 0.5, viewport.height * 0.5};
    const auto panAmount = event->modifiers().testFlag(Qt::ShiftModifier) ? 80.0 : 40.0;
    if (event->key() == Qt::Key_Tab && event->modifiers().testFlag(Qt::ControlModifier)) {
        focusNextPrevChild(!event->modifiers().testFlag(Qt::ShiftModifier));
        event->accept();
        return;
    }
    if (event->modifiers() == Qt::NoModifier) {
        switch (event->key()) {
        case Qt::Key_V: setTool(Tool::select); event->accept(); return;
        case Qt::Key_R: setTool(Tool::road); event->accept(); return;
        case Qt::Key_E: setTool(Tool::edit); event->accept(); return;
        case Qt::Key_S: setTool(Tool::split); event->accept(); return;
        case Qt::Key_M: setTool(Tool::measure); event->accept(); return;
        default: break;
        }
    }
    if ((tool_ == Tool::select || tool_ == Tool::edit) && !selectedControlPointId_.empty() &&
        (event->key() == Qt::Key_Left || event->key() == Qt::Key_Right ||
         event->key() == Qt::Key_Up || event->key() == Qt::Key_Down) &&
        (event->modifiers() == Qt::NoModifier || event->modifiers() == Qt::ShiftModifier)) {
        const auto road = std::find_if(project_.roadSplines().begin(), project_.roadSplines().end(),
            [&](const auto& candidate) { return candidate.id == selectedControlPointRoadId_; });
        if (road != project_.roadSplines().end()) {
            if (const auto point = controlPointPosition(*road, selectedControlPointId_)) {
                const auto step = event->modifiers().testFlag(Qt::ShiftModifier) ? 0.1 : 1.0;
                auto position = *point;
                if (event->key() == Qt::Key_Left) position.x -= step;
                if (event->key() == Qt::Key_Right) position.x += step;
                if (event->key() == Qt::Key_Up) position.y += step;
                if (event->key() == Qt::Key_Down) position.y -= step;
                auto editedRoad = *road;
                if (moveControlPoint(editedRoad, selectedControlPointId_, position) && roadEditedHandler_) {
                    roadEditedHandler_(std::move(editedRoad));
                }
            }
        }
        event->accept();
        return;
    }
    if (tool_ == Tool::road && (event->key() == Qt::Key_Left || event->key() == Qt::Key_Right ||
        event->key() == Qt::Key_Up || event->key() == Qt::Key_Down)) {
        const auto step = event->modifiers().testFlag(Qt::ShiftModifier) ? 0.1 : 1.0;
        if (event->key() == Qt::Key_Left) keyboardCursor_.x -= step;
        if (event->key() == Qt::Key_Right) keyboardCursor_.x += step;
        if (event->key() == Qt::Key_Up) keyboardCursor_.y += step;
        if (event->key() == Qt::Key_Down) keyboardCursor_.y -= step;
        if (statusHandler_) statusHandler_(QStringLiteral("Road cursor: %1 m, %2 m")
            .arg(keyboardCursor_.x, 0, 'f', 3).arg(keyboardCursor_.y, 0, 'f', 3));
        update();
        event->accept();
        return;
    }
    switch (event->key()) {
    case Qt::Key_Left: camera_.panByScreenDelta({panAmount, 0.0}); break;
    case Qt::Key_Right: camera_.panByScreenDelta({-panAmount, 0.0}); break;
    case Qt::Key_Up: camera_.panByScreenDelta({0.0, panAmount}); break;
    case Qt::Key_Down: camera_.panByScreenDelta({0.0, -panAmount}); break;
    case Qt::Key_Plus:
    case Qt::Key_Equal:
        camera_.zoomAt(center, 1.2);
        if (viewChangedHandler_) viewChangedHandler_(camera_.zoom());
        break;
    case Qt::Key_Minus:
        camera_.zoomAt(center, 1.0 / 1.2);
        if (viewChangedHandler_) viewChangedHandler_(camera_.zoom());
        break;
    case Qt::Key_Tab:
        cycleCanvasSelection(event->modifiers().testFlag(Qt::ShiftModifier));
        update();
        event->accept();
        return;
    case Qt::Key_Escape:
        if (!draftPoints_.empty()) cancelRoadDraft();
        else if (firstMeasurePoint_) {
            firstMeasurePoint_.reset();
            hoveredMeasurementPoint_.reset();
            secondMeasurePoint_.reset();
        }
        else {
            selection_.clear();
            selectedControlPointId_.clear();
            selectedControlPointRoadId_.clear();
            transformSpace_ = TransformSpace::world;
            if (selectionChangedHandler_) selectionChangedHandler_(false);
        }
        update();
        event->accept();
        return;
    case Qt::Key_Space:
        if (tool_ == Tool::road) {
            addRoadDraftPoint(keyboardCursor_);
            event->accept();
            return;
        }
        break;
    case Qt::Key_Backspace:
        if (tool_ == Tool::road && !draftPoints_.empty()) {
            draftRedoPoints_.push_back(draftPoints_.back());
            draftPoints_.pop_back();
            update();
            event->accept();
            return;
        }
        break;
    case Qt::Key_Z:
        if (tool_ == Tool::road && event->modifiers().testFlag(Qt::ControlModifier) && !draftPoints_.empty()) {
            draftRedoPoints_.push_back(draftPoints_.back());
            draftPoints_.pop_back();
            update();
            event->accept();
            return;
        }
        break;
    case Qt::Key_Y:
        if (tool_ == Tool::road && event->modifiers().testFlag(Qt::ControlModifier) && !draftRedoPoints_.empty()) {
            draftPoints_.push_back(draftRedoPoints_.back());
            draftRedoPoints_.pop_back();
            update();
            event->accept();
            return;
        }
        break;
    case Qt::Key_Return:
    case Qt::Key_Enter:
        if (tool_ == Tool::road) {
            finishRoadDraft();
            event->accept();
            return;
        }
        break;
    default:
        QOpenGLWidget::keyPressEvent(event);
        return;
    }
    update();
    event->accept();
}

} // namespace atlas::ui
