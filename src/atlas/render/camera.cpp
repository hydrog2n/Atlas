#include "atlas/render/camera.hpp"

#include <algorithm>

namespace atlas::render {

Camera2D::Camera2D(Viewport viewport)
    : viewport_(viewport) {}

const Viewport& Camera2D::viewport() const noexcept {
    return viewport_;
}

void Camera2D::setViewport(Viewport viewport) noexcept {
    viewport_.width = std::max(1.0, viewport.width);
    viewport_.height = std::max(1.0, viewport.height);
}

Point2D Camera2D::center() const noexcept {
    return center_;
}

void Camera2D::setCenter(Point2D center) noexcept {
    center_ = center;
}

double Camera2D::zoom() const noexcept {
    return zoom_;
}

void Camera2D::setZoom(double zoom) noexcept {
    zoom_ = std::clamp(zoom, minimumZoom, maximumZoom);
}

Point2D Camera2D::worldToScreen(Point2D world) const noexcept {
    return {
        (world.x - center_.x) * zoom_ + viewport_.width * 0.5,
        (center_.y - world.y) * zoom_ + viewport_.height * 0.5};
}

Point2D Camera2D::screenToWorld(Point2D screen) const noexcept {
    return {
        (screen.x - viewport_.width * 0.5) / zoom_ + center_.x,
        center_.y - (screen.y - viewport_.height * 0.5) / zoom_};
}

void Camera2D::panByScreenDelta(Point2D delta) noexcept {
    center_.x -= delta.x / zoom_;
    center_.y += delta.y / zoom_;
}

void Camera2D::zoomAt(Point2D screenAnchor, double factor) noexcept {
    if (factor <= 0.0) return;
    const auto worldAnchor = screenToWorld(screenAnchor);
    setZoom(zoom_ * factor);
    const auto anchoredScreen = worldToScreen(worldAnchor);
    center_.x += (anchoredScreen.x - screenAnchor.x) / zoom_;
    center_.y -= (anchoredScreen.y - screenAnchor.y) / zoom_;
}

void Camera2D::fitToBounds(Point2D minimum, Point2D maximum, double paddingPixels) noexcept {
    center_ = {(minimum.x + maximum.x) * 0.5, (minimum.y + maximum.y) * 0.5};
    const auto width = std::max(std::abs(maximum.x - minimum.x), 1.0e-12);
    const auto height = std::max(std::abs(maximum.y - minimum.y), 1.0e-12);
    const auto usableWidth = std::max(viewport_.width - 2.0 * paddingPixels, 1.0);
    const auto usableHeight = std::max(viewport_.height - 2.0 * paddingPixels, 1.0);
    setZoom(std::min(usableWidth / width, usableHeight / height));
}

} // namespace atlas::render
