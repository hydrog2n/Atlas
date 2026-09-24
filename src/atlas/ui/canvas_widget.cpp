#include "atlas/ui/canvas_widget.hpp"

#include <QMouseEvent>
#include <QKeyEvent>
#include <QOpenGLFunctions>
#include <QPainter>
#include <QWheelEvent>

#include <cmath>
#include <QColor>
#include <utility>

namespace atlas::ui {

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
    drawObjects(painter);
    painter.end();
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
        const auto hits = hitTester_.orderedHits(world, hitTargets(), 10.0 / camera_.zoom());
        if (!hits.empty()) selection_.select(hits.front().objectId);
        else selection_.clear();
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
    QOpenGLWidget::mouseMoveEvent(event);
}

void CanvasWidget::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() == Qt::MiddleButton) {
        panning_ = false;
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
    switch (event->key()) {
    case Qt::Key_Left: camera_.panByScreenDelta({panAmount, 0.0}); break;
    case Qt::Key_Right: camera_.panByScreenDelta({-panAmount, 0.0}); break;
    case Qt::Key_Up: camera_.panByScreenDelta({0.0, panAmount}); break;
    case Qt::Key_Down: camera_.panByScreenDelta({0.0, -panAmount}); break;
    case Qt::Key_Plus:
    case Qt::Key_Equal: camera_.zoomAt(center, 1.2); break;
    case Qt::Key_Minus: camera_.zoomAt(center, 1.0 / 1.2); break;
    case Qt::Key_Tab: {
        std::vector<std::string> candidates;
        for (const auto& target : hitTargets()) candidates.push_back(target.objectId);
        selection_.cycle(candidates, event->modifiers().testFlag(Qt::ShiftModifier));
        update();
        event->accept();
        return;
    }
    case Qt::Key_Escape:
        selection_.clear();
        update();
        event->accept();
        return;
    default:
        QOpenGLWidget::keyPressEvent(event);
        return;
    }
    update();
    event->accept();
}

} // namespace atlas::ui
