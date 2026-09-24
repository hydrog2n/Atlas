#include "atlas/geometry/snap.hpp"

#include <gtest/gtest.h>

#include <cmath>

// Verifies that grid snapping rounds coordinates using the configured spacing.
TEST(Geometry, GridSnapUsesConfiguredSpacing) {
    const auto snapped = atlas::geometry::snapToGrid({1.24, -1.26}, 0.5, {});

    EXPECT_DOUBLE_EQ(snapped.x, 1.0);
    EXPECT_DOUBLE_EQ(snapped.y, -1.5);
}

// Verifies that distance constraints preserve direction while setting length.
TEST(Geometry, DistanceConstraintPreservesDirection) {
    const auto constrained = atlas::geometry::constrainDistance({0.0, 0.0}, {3.0, 4.0}, 10.0);

    EXPECT_DOUBLE_EQ(constrained.x, 6.0);
    EXPECT_DOUBLE_EQ(constrained.y, 8.0);
}

// Verifies that angle constraints use deterministic angular increments.
TEST(Geometry, AngleConstraintUsesConfiguredIncrement) {
    const auto constrained = atlas::geometry::constrainAngle({0.0, 0.0}, {1.0, 0.2}, 45.0);

    EXPECT_NEAR(constrained.x, std::sqrt(1.04), 1.0e-9);
    EXPECT_NEAR(constrained.y, 0.0, 1.0e-9);
}

// Verifies that snap candidates are filtered and ordered by kind, distance, and stable ID.
TEST(Geometry, SnapCandidatesHaveDeterministicPriorityAndTieBreak) {
    atlas::geometry::SnapEngine engine({1.0e-9, 1.0, 0.5});
    const std::vector<atlas::geometry::SnapTarget> targets{
        {"z", atlas::geometry::SnapKind::endpoint, {0.2, 0.0}},
        {"a", atlas::geometry::SnapKind::point, {0.3, 0.0}},
        {"b", atlas::geometry::SnapKind::point, {-0.3, 0.0}},
        {"far", atlas::geometry::SnapKind::point, {2.0, 0.0}}};

    const auto candidates = engine.candidates({0.0, 0.0}, targets);

    ASSERT_EQ(candidates.size(), 3);
    EXPECT_EQ(candidates[0].targetId, "a");
    EXPECT_EQ(candidates[1].targetId, "b");
    EXPECT_EQ(candidates[2].targetId, "z");
}

// Verifies that candidates at the configured tolerance boundary remain eligible.
TEST(Geometry, SnapDistanceBoundaryIsInclusive) {
    atlas::geometry::SnapEngine engine({1.0e-9, 0.25, 0.5});

    const auto candidates = engine.candidates({0.0, 0.0}, {{"edge", atlas::geometry::SnapKind::grid, {0.25, 0.0}}});

    ASSERT_EQ(candidates.size(), 1);
    EXPECT_DOUBLE_EQ(candidates.front().distance, 0.25);
}

// Verifies that degenerate constraint inputs are handled without producing non-finite values.
TEST(Geometry, DegenerateConstraintsRemainFinite) {
    const auto distanceConstrained = atlas::geometry::constrainDistance({1.0, 2.0}, {1.0, 2.0}, 5.0);
    const auto angleConstrained = atlas::geometry::constrainAngle({1.0, 2.0}, {1.0, 2.0}, 45.0);

    EXPECT_TRUE(std::isfinite(distanceConstrained.x));
    EXPECT_TRUE(std::isfinite(distanceConstrained.y));
    EXPECT_TRUE(std::isfinite(angleConstrained.x));
    EXPECT_TRUE(std::isfinite(angleConstrained.y));
}

// Verifies that invalid grid and angle spacing leave the input point unchanged.
TEST(Geometry, InvalidConstraintSpacingPreservesInput) {
    const auto point = atlas::geometry::Point2D{3.25, -4.5};

    EXPECT_DOUBLE_EQ(atlas::geometry::snapToGrid(point, 0.0, {}).x, point.x);
    EXPECT_DOUBLE_EQ(atlas::geometry::snapToGrid(point, -1.0, {}).y, point.y);
    EXPECT_DOUBLE_EQ(atlas::geometry::constrainAngle({0.0, 0.0}, point, 0.0).x, point.x);
}
