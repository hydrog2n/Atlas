#include "atlas/render/camera.hpp"

#include <gtest/gtest.h>

namespace {

constexpr double coordinateTolerance = 1.0e-9;

} // namespace

// Verifies that world and screen coordinates round-trip at ordinary and large extents.
TEST(Camera2D, WorldAndScreenTransformsRoundTrip) {
    atlas::render::Camera2D camera({1600.0, 900.0});
    camera.setCenter({1.0e9, -1.0e9});
    camera.setZoom(2.5);

    const auto world = atlas::render::Point2D{1.0e9 + 123.25, -1.0e9 + 77.5};
    const auto restored = camera.screenToWorld(camera.worldToScreen(world));

    EXPECT_NEAR(restored.x, world.x, coordinateTolerance);
    EXPECT_NEAR(restored.y, world.y, coordinateTolerance);
}

// Verifies that panning changes the camera without changing the coordinate transform contract.
TEST(Camera2D, PanMovesTheWorldRelativeToTheViewport) {
    atlas::render::Camera2D camera({1000.0, 800.0});
    const auto before = camera.worldToScreen({10.0, 5.0});

    camera.panByScreenDelta({100.0, -50.0});
    const auto after = camera.worldToScreen({10.0, 5.0});

    EXPECT_DOUBLE_EQ(after.x, before.x + 100.0);
    EXPECT_DOUBLE_EQ(after.y, before.y - 50.0);
}

// Verifies that zooming around a screen point keeps the anchored world point fixed.
TEST(Camera2D, ZoomAtPreservesScreenAnchor) {
    atlas::render::Camera2D camera({1000.0, 800.0});
    camera.setCenter({25.0, -10.0});
    const auto screenAnchor = atlas::render::Point2D{725.0, 260.0};
    const auto worldAnchor = camera.screenToWorld(screenAnchor);

    camera.zoomAt(screenAnchor, 4.0);

    const auto restoredScreen = camera.worldToScreen(worldAnchor);
    EXPECT_NEAR(restoredScreen.x, screenAnchor.x, coordinateTolerance);
    EXPECT_NEAR(restoredScreen.y, screenAnchor.y, coordinateTolerance);
}

// Verifies that invalid zoom factors and out-of-range zoom values are safely bounded.
TEST(Camera2D, ZoomIsBoundedAndRejectsNonPositiveFactors) {
    atlas::render::Camera2D camera;
    camera.setZoom(-1.0);
    EXPECT_GT(camera.zoom(), 0.0);
    const auto before = camera.zoom();

    camera.zoomAt({0.0, 0.0}, 0.0);
    EXPECT_DOUBLE_EQ(camera.zoom(), before);
}

// Verifies that fit-to-bounds centers the camera and keeps the requested bounds visible.
TEST(Camera2D, FitToBoundsCentersAndScalesViewport) {
    atlas::render::Camera2D camera({1000.0, 800.0});

    camera.fitToBounds({-10.0, -5.0}, {10.0, 5.0});

    EXPECT_DOUBLE_EQ(camera.center().x, 0.0);
    EXPECT_DOUBLE_EQ(camera.center().y, 0.0);
    EXPECT_LE(camera.worldToScreen({-10.0, 0.0}).x, 32.0 + 1.0e-9);
    EXPECT_GE(camera.worldToScreen({10.0, 0.0}).x, 968.0 - 1.0e-9);
}
