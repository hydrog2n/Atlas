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
#include <QColor>
#include <utility>

namespace atlas::ui {

namespace {

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
    update();
}

const atlas::application::SelectionState& CanvasWidget::selection() const noexcept {
    return selection_;
}

void CanvasWidget::setTool(Tool tool) {
    if (tool_ == Tool::road && tool != Tool::road) cancelRoadDraft();
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

void CanvasWidget::setRoadDrawingMode(RoadDrawingMode mode) {
    drawingMode_ = mode;
    cancelRoadDraft();
}

void CanvasWidget::setRoadSnapToGrid(bool enabled) { snapRoadDraftToGrid_ = enabled; }

void CanvasWidget::setArcRadiusMeters(double radius) { arcRadiusMeters_ = radius; }
void CanvasWidget::setRoadWidthMeters(double width) { roadWidthMeters_ = width; update(); }

void CanvasWidget::setRoadCreatedHandler(std::function<void(atlas::domain::RoadSpline)> handler) {
    roadCreatedHandler_ = std::move(handler);
}

void CanvasWidget::setRoadEditedHandler(std::function<void(atlas::domain::RoadSpline)> handler) {
    roadEditedHandler_ = std::move(handler);
}

void CanvasWidget::setRoadSelectedHandler(std::function<void(std::string)> handler) {
    roadSelectedHandler_ = std::move(handler);
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

void CanvasWidget::setPreviewProject(std::optional<atlas::domain::Project> project) {
    previewProject_ = std::move(project);
    update();
}

void CanvasWidget::selectId(const std::string& id) {
    if (id.empty()) selection_.clear();
    else selection_.select(id);
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
        update();
    } catch (const std::exception&) {
        return;
    }
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
                painter.setPen(QPen(envelope.valid ? QColor(64, 140, 195, 210) : QColor(220, 90, 70),
                    1.0, envelope.valid ? Qt::SolidLine : Qt::DashLine));
                painter.setBrush(envelope.valid ? QColor(55, 135, 190, 42) : QColor(220, 90, 70, 28));
                painter.drawPolygon(screenPolygon);
            }

            const bool selected = selection_.primaryId() == road.id;
            painter.setBrush(Qt::NoBrush);
            painter.setPen(QPen(selected ? QColor(255, 220, 100) : QColor(190, 220, 240),
                selected ? 3.0 : 2.0));
            painter.drawPath(centerline);
            if (selected || tool_ == Tool::edit) {
                painter.setBrush(QColor(255, 220, 100));
                for (const auto& item : renderRoad.primitives) {
                    if (item.value("kind", "") == "cubic-bezier") {
                        const auto& points = item.contains("controlPoints") ? item.at("controlPoints") : item.at("points");
                        for (const auto& point : points) painter.drawEllipse(toScreen(camera_, readPoint(point)), 4.0, 4.0);
                    } else {
                        painter.drawEllipse(toScreen(camera_, readPoint(item.at("start"))), 4.0, 4.0);
                        painter.drawEllipse(toScreen(camera_, readPoint(item.at("end"))), 4.0, 4.0);
                    }
                }
            }
        } catch (const std::exception&) {
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
    painter.drawEllipse(toScreen(camera_, keyboardCursor_), 4.0, 4.0);
}

void CanvasWidget::addRoadDraftPoint(atlas::geometry::Point2D point) {
    if (snapRoadDraftToGrid_) point = atlas::geometry::snapToGrid(point, 1.0, {});
    if (drawingMode_ == RoadDrawingMode::bezier && draftPoints_.size() >= 4) return;
    if (drawingMode_ == RoadDrawingMode::fixedRadiusArc && draftPoints_.size() >= 2) return;
    draftPoints_.push_back(point);
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
    keyboardCursor_ = {};
    if (roadCreatedHandler_) roadCreatedHandler_(std::move(road));
    update();
}

void CanvasWidget::cancelRoadDraft() {
    draftPoints_.clear();
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

void CanvasWidget::drawObjects(QPainter& painter) {
    for (const auto& object : project_.rootMap().objects) {
        if (!object.visible() || !object.geometry().is_object() ||
            !object.geometry().contains("x") || !object.geometry().contains("y")) {
            continue;
        }
        const auto screen = camera_.worldToScreen({
            object.geometry().value("x", 0.0), object.geometry().value("y", 0.0)});
        const auto selected = object.id() == selection_.primaryId();
        const auto color = object.locked() ? QColor(145, 150, 160) : QColor(90, 190, 255);
        painter.setPen(QPen(selected ? QColor(255, 220, 100) : color, selected ? 3.0 : 2.0));
        painter.setBrush(object.locked() ? Qt::NoBrush : color);
        painter.drawEllipse(QPointF(screen.x, screen.y), selected ? 7.0 : 5.0, selected ? 7.0 : 5.0);
        if (object.locked()) {
            painter.drawLine(QPointF(screen.x - 6.0, screen.y - 6.0), QPointF(screen.x + 6.0, screen.y + 6.0));
        }
    }
}

void CanvasWidget::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        const auto position = event->position();
        const auto renderWorld = camera_.screenToWorld({position.x(), position.y()});
        const atlas::geometry::Point2D world{renderWorld.x, renderWorld.y};
        lastPointerWorld_ = world;
        hasLastPointerWorld_ = true;
        keyboardCursor_ = world;
        if (tool_ == Tool::road) {
            addRoadDraftPoint(world);
            event->accept();
            return;
        }
        if (tool_ == Tool::split) {
            if (const auto road = roadAt(world, 12.0 / camera_.zoom()); road && roadStationHandler_) {
                roadStationHandler_(road->first, road->second);
            }
            event->accept();
            return;
        }
        if (tool_ == Tool::measure) {
            if (const auto road = roadAt(world, 12.0 / camera_.zoom())) {
                if (!firstMeasurePoint_) {
                    firstMeasurePoint_ = road;
                    if (statusHandler_) statusHandler_(QStringLiteral("Measure start: %1 at %2 m")
                        .arg(QString::fromStdString(road->first)).arg(road->second, 0, 'f', 3));
                } else {
                    if (firstMeasurePoint_->first == road->first && statusHandler_) {
                        statusHandler_(QStringLiteral("Road distance: %1 m")
                            .arg(std::abs(road->second - firstMeasurePoint_->second), 0, 'f', 3));
                    } else if (statusHandler_) {
                        statusHandler_(QStringLiteral("Choose both measurement points on the same road."));
                    }
                    firstMeasurePoint_.reset();
                }
            }
            event->accept();
            return;
        }
        if (tool_ == Tool::edit && !selection_.primaryId().empty()) {
            const auto road = std::find_if(project_.roadSplines().begin(), project_.roadSplines().end(),
                [&](const auto& candidate) { return candidate.id == selection_.primaryId(); });
            if (road != project_.roadSplines().end()) {
                auto closestDistance = 12.0;
                std::string closestId;
                for (const auto& item : road->primitives) {
                    if (item.value("kind", "") == "cubic-bezier") {
                        const auto& ids = item.at("controlPointIds");
                        const auto& points = item.contains("controlPoints") ? item.at("controlPoints") : item.at("points");
                        for (std::size_t index = 0; index < ids.size(); ++index) {
                            const auto screen = toScreen(camera_, readPoint(points[index]));
                            const auto distance = QLineF(screen, position).length();
                            if (distance < closestDistance) {
                                closestDistance = distance;
                                closestId = ids[index].get<std::string>();
                            }
                        }
                    } else {
                        for (const auto& endpoint : {std::string("start"), std::string("end")}) {
                            const auto screen = toScreen(camera_, readPoint(item.at(endpoint)));
                            const auto distance = QLineF(screen, position).length();
                            if (distance < closestDistance) {
                                closestDistance = distance;
                                closestId = item.value(endpoint + "ControlPointId", "");
                            }
                        }
                    }
                }
                if (!closestId.empty()) {
                    draggedRoad_ = *road;
                    draggedControlPointId_ = std::move(closestId);
                    draggingControlPoint_ = true;
                    event->accept();
                    return;
                }
            }
        }
        overlapCandidateIds_.clear();
        for (const auto& road : roadCandidatesAt(world, 10.0 / camera_.zoom())) {
            overlapCandidateIds_.push_back(road.first);
        }
        const auto hits = hitTester_.orderedHits(world, hitTargets(), 10.0 / camera_.zoom());
        for (const auto& hit : hits) {
            if (std::find(overlapCandidateIds_.begin(), overlapCandidateIds_.end(), hit.objectId) ==
                overlapCandidateIds_.end()) overlapCandidateIds_.push_back(hit.objectId);
        }
        if (!overlapCandidateIds_.empty()) {
            selection_.select(overlapCandidateIds_.front());
            if (roadSelectedHandler_ && std::any_of(project_.roadSplines().begin(), project_.roadSplines().end(),
                [&](const auto& road) { return road.id == selection_.primaryId(); })) {
                roadSelectedHandler_(selection_.primaryId());
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
    if (draggingControlPoint_ && draggedRoad_) {
        const auto position = event->position();
        const auto worldPosition = camera_.screenToWorld({position.x(), position.y()});
        moveControlPoint(*draggedRoad_, draggedControlPointId_, {worldPosition.x, worldPosition.y});
        update();
        event->accept();
        return;
    }
    const auto position = event->position();
    const auto world = camera_.screenToWorld({position.x(), position.y()});
    lastPointerWorld_ = {world.x, world.y};
    hasLastPointerWorld_ = true;
    if (statusHandler_) {
        auto message = QStringLiteral("Coordinates: %1 m, %2 m")
            .arg(world.x, 0, 'f', 3).arg(world.y, 0, 'f', 3);
        if (!selection_.primaryId().empty()) {
            const auto road = std::find_if(project_.roadSplines().begin(), project_.roadSplines().end(),
                [&](const auto& candidate) { return candidate.id == selection_.primaryId(); });
            if (road != project_.roadSplines().end()) {
                if (const auto hit = roadAt(lastPointerWorld_, 12.0 / camera_.zoom());
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
    if (event->button() == Qt::LeftButton && draggingControlPoint_ && draggedRoad_) {
        draggingControlPoint_ = false;
        draggedControlPointId_.clear();
        if (roadEditedHandler_) roadEditedHandler_(std::move(*draggedRoad_));
        draggedRoad_.reset();
        update();
        event->accept();
        return;
    }
    QOpenGLWidget::mouseReleaseEvent(event);
}

void CanvasWidget::wheelEvent(QWheelEvent* event) {
    const auto steps = static_cast<double>(event->angleDelta().y()) / 120.0;
    const auto factor = std::pow(1.2, steps);
    const auto position = event->position();
    camera_.zoomAt({position.x(), position.y()}, factor);
    update();
    event->accept();
}

void CanvasWidget::keyPressEvent(QKeyEvent* event) {
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
    case Qt::Key_Equal: camera_.zoomAt(center, 1.2); break;
    case Qt::Key_Minus: camera_.zoomAt(center, 1.0 / 1.2); break;
    case Qt::Key_Tab: {
        auto candidates = overlapCandidateIds_;
        if (hasLastPointerWorld_) {
            candidates.clear();
            for (const auto& road : roadCandidatesAt(lastPointerWorld_, 10.0 / camera_.zoom())) {
                candidates.push_back(road.first);
            }
            const auto hits = hitTester_.orderedHits(lastPointerWorld_, hitTargets(), 10.0 / camera_.zoom());
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
        selection_.cycle(candidates, event->modifiers().testFlag(Qt::ShiftModifier));
        if (roadSelectedHandler_ && std::any_of(project_.roadSplines().begin(), project_.roadSplines().end(),
            [&](const auto& road) { return road.id == selection_.primaryId(); })) {
            roadSelectedHandler_(selection_.primaryId());
        }
        update();
        event->accept();
        return;
    }
    case Qt::Key_Escape:
        if (!draftPoints_.empty()) cancelRoadDraft();
        else if (firstMeasurePoint_) firstMeasurePoint_.reset();
        else selection_.clear();
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
            draftPoints_.pop_back();
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
