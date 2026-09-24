#include "atlas/geometry/calibration.hpp"

#include <gtest/gtest.h>

// Verifies that valid calibration computes scale and maps the reference endpoints.
TEST(Calibration, ValidTwoPointReferenceProducesTransform) {
    const auto transform = atlas::geometry::calibrateTwoPointReference(
        {0.0, 0.0}, {2.0, 0.0}, {10.0, 10.0}, {10.0, 14.0}, 1.0e-9);

    ASSERT_TRUE(transform.valid);
    EXPECT_DOUBLE_EQ(transform.scale, 2.0);
    EXPECT_NEAR(transform.apply({2.0, 0.0}).x, 10.0, 1.0e-9);
    EXPECT_NEAR(transform.apply({2.0, 0.0}).y, 14.0, 1.0e-9);
}

// Verifies that coincident source calibration points are rejected without a transform.
TEST(Calibration, CoincidentSourcePointsAreRejected) {
    const auto transform = atlas::geometry::calibrateTwoPointReference(
        {1.0, 1.0}, {1.0, 1.0}, {0.0, 0.0}, {2.0, 0.0}, 1.0e-6);

    EXPECT_FALSE(transform.valid);
    EXPECT_FALSE(transform.error.empty());
}

// Verifies that target points below the named tolerance are rejected.
TEST(Calibration, NearCoincidentTargetPointsAreRejected) {
    const auto transform = atlas::geometry::calibrateTwoPointReference(
        {0.0, 0.0}, {1.0, 0.0}, {0.0, 0.0}, {1.0e-8, 0.0}, 1.0e-6);

    EXPECT_FALSE(transform.valid);
}

// Verifies that an invalid preview transform leaves source coordinates unchanged.
TEST(Calibration, InvalidTransformDoesNotChangeSourcePoint) {
    const auto transform = atlas::geometry::calibrateTwoPointReference(
        {0.0, 0.0}, {0.0, 0.0}, {2.0, 2.0}, {3.0, 2.0}, 1.0e-6);
    const auto source = atlas::geometry::Point2D{4.0, 5.0};

    EXPECT_DOUBLE_EQ(transform.apply(source).x, source.x);
    EXPECT_DOUBLE_EQ(transform.apply(source).y, source.y);
}
