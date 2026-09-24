#pragma once

namespace atlas::render {

struct Point2D {
    double x = 0.0;
    double y = 0.0;
};

struct Viewport {
    double width = 1.0;
    double height = 1.0;
};

class Camera2D {
public:
    explicit Camera2D(Viewport viewport = {});

    const Viewport& viewport() const noexcept;
    void setViewport(Viewport viewport) noexcept;

    Point2D center() const noexcept;
    void setCenter(Point2D center) noexcept;

    double zoom() const noexcept;
    void setZoom(double zoom) noexcept;

    Point2D worldToScreen(Point2D world) const noexcept;
    Point2D screenToWorld(Point2D screen) const noexcept;

    void panByScreenDelta(Point2D delta) noexcept;
    void zoomAt(Point2D screenAnchor, double factor) noexcept;
    void fitToBounds(Point2D minimum, Point2D maximum, double paddingPixels = 32.0) noexcept;

private:
    static constexpr double minimumZoom = 1.0e-9;
    static constexpr double maximumZoom = 1.0e9;

    Viewport viewport_;
    Point2D center_;
    double zoom_ = 1.0;
};

} // namespace atlas::render
