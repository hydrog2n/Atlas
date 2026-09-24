#pragma once

#include "atlas/application/selection.hpp"
#include "atlas/domain/project.hpp"
#include "atlas/render/camera.hpp"
#include "atlas/render/hit_test.hpp"

#include <QOpenGLWidget>

class QMouseEvent;
class QPainter;
class QWheelEvent;
class QKeyEvent;

namespace atlas::ui {

class CanvasWidget final : public QOpenGLWidget {
public:
    explicit CanvasWidget(QWidget* parent = nullptr);

    const atlas::render::Camera2D& camera() const noexcept;
    void setProject(atlas::domain::Project project);
    const atlas::application::SelectionState& selection() const noexcept;

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
    std::vector<atlas::render::HitTarget> hitTargets() const;

    atlas::render::Camera2D camera_;
    atlas::domain::Project project_ = atlas::domain::Project::empty("project", "root-map");
    atlas::application::SelectionState selection_;
    atlas::render::HitTester hitTester_;
    QPoint lastMousePosition_;
    bool panning_ = false;
};

} // namespace atlas::ui
