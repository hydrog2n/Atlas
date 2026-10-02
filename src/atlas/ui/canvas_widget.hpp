#pragma once

#include "atlas/application/selection.hpp"
#include "atlas/domain/project.hpp"
#include "atlas/geometry/envelope.hpp"
#include "atlas/render/camera.hpp"
#include "atlas/render/hit_test.hpp"

#include <QOpenGLWidget>
#include <QString>

#include <functional>
#include <vector>

class QMouseEvent;
class QPainter;
class QWheelEvent;
class QKeyEvent;

namespace atlas::ui {

class CanvasWidget final : public QOpenGLWidget {
public:
    enum class Tool { select, road, edit, split, measure };
    enum class RoadDrawingMode { polyline, bezier, fixedRadiusArc };

    explicit CanvasWidget(QWidget* parent = nullptr);

    const atlas::render::Camera2D& camera() const noexcept;
    void setProject(atlas::domain::Project project);
    const atlas::application::SelectionState& selection() const noexcept;
    void setTool(Tool tool);
    Tool tool() const noexcept;
    void setRoadDrawingMode(RoadDrawingMode mode);
    void setRoadSnapToGrid(bool enabled);
    void setArcRadiusMeters(double radius);
    void setRoadWidthMeters(double width);
    void setRoadCreatedHandler(std::function<void(atlas::domain::RoadSpline)> handler);
    void setRoadEditedHandler(std::function<void(atlas::domain::RoadSpline)> handler);
    void setRoadSelectedHandler(std::function<void(std::string)> handler);
    void setRoadStationHandler(std::function<void(std::string, double)> handler);
    void setStatusHandler(std::function<void(QString)> handler);
    void setToolChangedHandler(std::function<void(Tool)> handler);
    void setPreviewProject(std::optional<atlas::domain::Project> project);
    void selectId(const std::string& id);
    void fitRoad(const std::string& roadSplineId);

protected:
    void initializeGL() override;
    void resizeGL(int width, int height) override;
    void paintGL() override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private:
    void drawGrid(QPainter& painter);
    void drawObjects(QPainter& painter);
    void drawRoads(QPainter& painter);
    void drawRoadDraft(QPainter& painter);
    void addRoadDraftPoint(atlas::geometry::Point2D point);
    void finishRoadDraft();
    void cancelRoadDraft();
    std::optional<std::pair<std::string, double>> roadAt(
        atlas::geometry::Point2D point, double toleranceMeters) const;
    std::vector<std::pair<std::string, double>> roadCandidatesAt(
        atlas::geometry::Point2D point, double toleranceMeters) const;
    bool moveControlPoint(atlas::domain::RoadSpline& road, const std::string& pointId,
        atlas::geometry::Point2D position) const;
    std::vector<atlas::render::HitTarget> hitTargets() const;

    atlas::render::Camera2D camera_;
    atlas::domain::Project project_ = atlas::domain::Project::empty("project", "root-map");
    atlas::application::SelectionState selection_;
    atlas::render::HitTester hitTester_;
    atlas::geometry::RoadEnvelopeCache envelopeCache_;
    std::optional<atlas::domain::Project> previewProject_;
    std::optional<atlas::domain::RoadSpline> draggedRoad_;
    std::optional<std::pair<std::string, double>> firstMeasurePoint_;
    std::vector<std::string> overlapCandidateIds_;
    std::vector<atlas::geometry::Point2D> draftPoints_;
    atlas::geometry::Point2D keyboardCursor_{};
    atlas::geometry::Point2D lastPointerWorld_{};
    Tool tool_ = Tool::select;
    RoadDrawingMode drawingMode_ = RoadDrawingMode::polyline;
    double arcRadiusMeters_ = 20.0;
    double roadWidthMeters_ = 8.0;
    std::function<void(atlas::domain::RoadSpline)> roadCreatedHandler_;
    std::function<void(atlas::domain::RoadSpline)> roadEditedHandler_;
    std::function<void(std::string)> roadSelectedHandler_;
    std::function<void(std::string, double)> roadStationHandler_;
    std::function<void(QString)> statusHandler_;
    std::function<void(Tool)> toolChangedHandler_;
    std::string draggedControlPointId_;
    bool draggingControlPoint_ = false;
    bool snapRoadDraftToGrid_ = true;
    bool hasLastPointerWorld_ = false;
    QPoint lastMousePosition_;
    bool panning_ = false;
};

} // namespace atlas::ui
