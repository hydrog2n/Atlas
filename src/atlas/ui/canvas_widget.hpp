#pragma once

    enum class TransformSpace { world, local };
#include "atlas/application/selection.hpp"
#include "atlas/domain/project.hpp"
#include "atlas/geometry/envelope.hpp"
#include "atlas/render/camera.hpp"
#include "atlas/render/hit_test.hpp"

#include <QOpenGLWidget>
#include <QString>

#include <functional>
#include <optional>
#include <string>
#include <vector>

class QMouseEvent;
class QPainter;
class QWheelEvent;
class QKeyEvent;
class QEvent;

namespace atlas::ui {

class CanvasWidget final : public QOpenGLWidget {
public:
    enum class Tool { select, road, edit, split, measure };
    enum class RoadDrawingMode { polyline, bezier, fixedRadiusArc };
    enum class TransformSpace { world, local };
    enum class InteractionState { idle, hovering, selected, drafting, manipulating, measuring };

    explicit CanvasWidget(QWidget* parent = nullptr);

    const atlas::render::Camera2D& camera() const noexcept;
    void setProject(atlas::domain::Project project);
    const atlas::application::SelectionState& selection() const noexcept;
    void setTool(Tool tool);
    Tool tool() const noexcept;
    InteractionState interactionState() const noexcept;
    void setTransformSpace(TransformSpace transformSpace);
    TransformSpace transformSpace() const noexcept;
    void setRoadDrawingMode(RoadDrawingMode mode);
    void setRoadSnapToGrid(bool enabled);
    void setArcRadiusMeters(double radius);
    void setRoadWidthMeters(double width);
    void finishRoadDrawing();
    void setRoadCreatedHandler(std::function<void(atlas::domain::RoadSpline)> handler);
    void setRoadEditedHandler(std::function<void(atlas::domain::RoadSpline)> handler);
    void setMapObjectEditedHandler(std::function<void(atlas::domain::MapObject)> handler);
    void setRoadSelectedHandler(std::function<void(std::string)> handler);
    void setSelectionChangedHandler(std::function<void(bool)> handler);
    void setSegmentSelectedHandler(std::function<void(std::string, std::string)> handler);
    void setRoadStationHandler(std::function<void(std::string, double)> handler);
    void setStatusHandler(std::function<void(QString)> handler);
    void setToolChangedHandler(std::function<void(Tool)> handler);
    void setViewChangedHandler(std::function<void(double)> handler);
    void setPreviewProject(std::optional<atlas::domain::Project> project);
    void selectId(const std::string& id);
    void setSelectedSegment(const std::string& segmentId);
    void fitSelection();
    void fitRoad(const std::string& roadSplineId);

protected:
    bool event(QEvent* event) override;
    void initializeGL() override;
    void resizeGL(int width, int height) override;
    void paintGL() override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private:
    struct ControlPointHit {
        std::string roadSplineId;
        std::string controlPointId;
        atlas::geometry::Point2D position;
        double screenDistance = 0.0;
    };

    struct MeasurementPoint {
        std::string roadSplineId;
        double station = 0.0;
        atlas::geometry::Point2D position;
    };

    enum class TransformHandle { worldX, worldY, rotation };

    struct TransformGizmoHit {
        TransformHandle handle;
        std::string targetId;
        atlas::geometry::Point2D origin;
        bool roadSpline = false;
    };

    void drawGrid(QPainter& painter);
    void drawObjects(QPainter& painter);
    void drawRoads(QPainter& painter);
    void drawRoadDraft(QPainter& painter);
    void drawMeasurement(QPainter& painter);
    void drawTransformGizmo(QPainter& painter);
    void addRoadDraftPoint(atlas::geometry::Point2D point);
    void finishRoadDraft();
    void cancelRoadDraft();
    std::optional<std::pair<std::string, double>> roadAt(
        atlas::geometry::Point2D point, double toleranceMeters) const;
    std::vector<std::pair<std::string, double>> roadCandidatesAt(
        atlas::geometry::Point2D point, double toleranceMeters) const;
    std::optional<ControlPointHit> controlPointAt(QPointF screenPoint, double tolerancePixels) const;
    std::vector<ControlPointHit> controlPointsAt(QPointF screenPoint, double tolerancePixels) const;
    std::optional<atlas::geometry::Point2D> controlPointPosition(
        const atlas::domain::RoadSpline& road, const std::string& pointId) const;
    std::optional<TransformGizmoHit> transformGizmoHit(QPointF screenPoint) const;
    std::pair<atlas::geometry::Point2D, atlas::geometry::Point2D> transformAxes(
        const std::string& targetId, bool roadSpline) const;
    bool transformRoad(
        atlas::domain::RoadSpline& road, atlas::geometry::Point2D origin,
        double translateX, double translateY, double rotationRadians) const;
    bool moveControlPoint(atlas::domain::RoadSpline& road, const std::string& pointId,
        atlas::geometry::Point2D position) const;
    std::vector<atlas::render::HitTarget> hitTargets() const;
    std::string segmentAt(const std::string& roadSplineId, double station) const;
    void cycleCanvasSelection(bool reverse);

    atlas::render::Camera2D camera_;
    atlas::domain::Project project_ = atlas::domain::Project::empty("project", "root-map");
    atlas::application::SelectionState selection_;
    atlas::render::HitTester hitTester_;
    atlas::geometry::RoadEnvelopeCache envelopeCache_;
    std::optional<atlas::domain::Project> previewProject_;
    std::optional<atlas::domain::RoadSpline> draggedRoad_;
    std::optional<atlas::domain::RoadSpline> draggedRoadOriginal_;
    std::optional<atlas::domain::MapObject> draggedMapObject_;
    std::optional<atlas::domain::MapObject> draggedMapObjectOriginal_;
    std::string hoveredObjectId_;
    std::optional<ControlPointHit> hoveredControlPoint_;
    std::optional<MeasurementPoint> firstMeasurePoint_;
    std::optional<MeasurementPoint> hoveredMeasurementPoint_;
    std::optional<MeasurementPoint> secondMeasurePoint_;
    std::vector<std::string> overlapCandidateIds_;
    std::string hoveredRoadId_;
    std::string hoveredSegmentId_;
    std::string selectedSegmentId_;
    std::string selectedControlPointId_;
    std::string selectedControlPointRoadId_;
    std::vector<atlas::geometry::Point2D> draftPoints_;
    std::vector<atlas::geometry::Point2D> draftRedoPoints_;
    std::optional<atlas::geometry::Point2D> provisionalDraftPoint_;
    atlas::geometry::Point2D keyboardCursor_{};
    atlas::geometry::Point2D lastPointerWorld_{};
    Tool tool_ = Tool::select;
    RoadDrawingMode drawingMode_ = RoadDrawingMode::polyline;
    double arcRadiusMeters_ = 20.0;
    double roadWidthMeters_ = 8.0;
    std::function<void(atlas::domain::RoadSpline)> roadCreatedHandler_;
    TransformSpace transformSpace_ = TransformSpace::world;
    std::function<void(bool)> selectionChangedHandler_;
    std::function<void(atlas::domain::RoadSpline)> roadEditedHandler_;
    std::function<void(atlas::domain::MapObject)> mapObjectEditedHandler_;
    std::function<void(std::string)> roadSelectedHandler_;
    std::function<void(std::string, std::string)> segmentSelectedHandler_;
    std::function<void(std::string, double)> roadStationHandler_;
    std::function<void(QString)> statusHandler_;
    std::function<void(Tool)> toolChangedHandler_;
    std::function<void(double)> viewChangedHandler_;
    std::string draggedControlPointId_;
    bool draggingControlPoint_ = false;
    bool draggingTransformGizmo_ = false;
    bool draggingMapObject_ = false;
    bool mapObjectDragChanged_ = false;
    TransformHandle activeTransformHandle_ = TransformHandle::worldX;
    atlas::geometry::Point2D transformOrigin_{};
    atlas::geometry::Point2D transformStartPointer_{};
    double transformStartAngle_ = 0.0;
    bool controlPointDragChanged_ = false;
    bool snapRoadDraftToGrid_ = true;
    bool hasLastPointerWorld_ = false;
    QPoint lastMousePosition_;
    QPointF lastPointerScreen_;
    bool panning_ = false;
};

} // namespace atlas::ui
