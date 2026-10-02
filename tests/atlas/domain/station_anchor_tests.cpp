#include "atlas/domain/station_anchor.hpp"

#include <gtest/gtest.h>

namespace {

atlas::geometry::CurveKernel straightCurve(double length) {
    return atlas::geometry::CurveKernel({atlas::geometry::PolylinePrimitive{
        "line", "a", "b", {0.0, 0.0}, {length, 0.0}}});
}

atlas::domain::StationAnchor anchor(
    atlas::domain::AnchorAffinity affinity,
    double station,
    double normalized,
    std::string primitiveId = "line",
    double primitiveT = 0.5,
    atlas::geometry::Point2D worldPosition = {5.0, 0.0}) {
    return {"anchor", station, affinity, {normalized, std::move(primitiveId), primitiveT, worldPosition}};
}

} // namespace

// Verifies that start-locked anchors preserve distance from the RoadSpline start.
TEST(StationAnchor, StartLockedPreservesStartDistance) {
    const auto result = atlas::domain::remapAnchor(
        anchor(atlas::domain::AnchorAffinity::startLocked, 4.0, 0.4), 10.0,
        straightCurve(20.0), {}, 2.0);

    ASSERT_TRUE(result.resolved);
    EXPECT_DOUBLE_EQ(result.anchor.resolvedStation, 4.0);
}

// Verifies that end-locked anchors preserve distance from the RoadSpline end.
TEST(StationAnchor, EndLockedPreservesEndDistance) {
    const auto result = atlas::domain::remapAnchor(
        anchor(atlas::domain::AnchorAffinity::endLocked, 4.0, 0.4), 10.0,
        straightCurve(20.0), {}, 2.0);

    ASSERT_TRUE(result.resolved);
    EXPECT_DOUBLE_EQ(result.anchor.resolvedStation, 14.0);
}

// Verifies that normalized anchors preserve their fraction of the edited curve length.
TEST(StationAnchor, NormalizedPreservesLengthFraction) {
    const auto result = atlas::domain::remapAnchor(
        anchor(atlas::domain::AnchorAffinity::normalized, 4.0, 0.4), 10.0,
        straightCurve(20.0), {}, 2.0);

    ASSERT_TRUE(result.resolved);
    EXPECT_DOUBLE_EQ(result.anchor.resolvedStation, 8.0);
}

// Verifies that geometry-locked anchors use transaction-local primitive lineage first.
TEST(StationAnchor, GeometryLockedUsesPrimitiveRemapTable) {
    const auto result = atlas::domain::remapAnchor(
        anchor(atlas::domain::AnchorAffinity::geometryLocked, 5.0, 0.5, "old", 0.61), 10.0,
        atlas::geometry::CurveKernel({
            atlas::geometry::PolylinePrimitive{"first", "a", "mid", {0.0, 0.0}, {3.0, 0.0}},
            atlas::geometry::PolylinePrimitive{"second", "mid", "b", {3.0, 0.0}, {10.0, 0.0}}}),
        {{"old", 0.37, 1.0, "second", 0.0, 1.0}}, 2.0);

    ASSERT_TRUE(result.resolved);
    EXPECT_EQ(result.anchor.remapSignature.primitiveId, "second");
    EXPECT_NEAR(result.anchor.remapSignature.primitiveT, 0.381, 1.0e-3);
}

// Verifies that world-locked anchors use bounded deterministic projection fallback.
TEST(StationAnchor, WorldLockedUsesWorldPositionFallback) {
    const auto result = atlas::domain::remapAnchor(
        anchor(atlas::domain::AnchorAffinity::worldLocked, 4.0, 0.4, "missing", 0.5, {7.0, 0.1}), 10.0,
        straightCurve(10.0), {}, 1.0);

    ASSERT_TRUE(result.resolved);
    EXPECT_NEAR(result.anchor.resolvedStation, 7.0, 0.2);
}

// Verifies that ambiguous or out-of-domain remaps remain unresolved for explicit command resolution.
TEST(StationAnchor, AmbiguousAndOutOfDomainRemapsAreReported) {
    const auto outOfDomain = atlas::domain::remapAnchor(
        anchor(atlas::domain::AnchorAffinity::startLocked, 9.0, 0.9), 10.0,
        straightCurve(5.0), {}, 2.0);
    const auto ambiguous = atlas::domain::remapAnchor(
        anchor(atlas::domain::AnchorAffinity::worldLocked, 5.0, 0.5, "missing", 0.5, {0.0, 5.0}), 10.0,
        atlas::geometry::CurveKernel({
            atlas::geometry::PolylinePrimitive{"first", "a", "b", {0.0, 0.0}, {0.0, 10.0}},
            atlas::geometry::PolylinePrimitive{"second", "b", "c", {0.0, 10.0}, {0.0, 0.0}},
            atlas::geometry::PolylinePrimitive{"third", "c", "d", {0.0, 0.0}, {0.0, 10.0}},
            atlas::geometry::PolylinePrimitive{"fourth", "d", "e", {0.0, 10.0}, {0.0, 20.0}}}), {}, 1.0);

    EXPECT_FALSE(outOfDomain.resolved);
    EXPECT_FALSE(outOfDomain.diagnostic.empty());
    EXPECT_FALSE(ambiguous.resolved);
    EXPECT_TRUE(ambiguous.ambiguous);
}

// Verifies that identical remap inputs produce identical resolved anchor output.
TEST(StationAnchor, RemappingIsDeterministic) {
    const auto curve = straightCurve(20.0);
    const auto first = atlas::domain::remapAnchor(
        anchor(atlas::domain::AnchorAffinity::normalized, 4.0, 0.4), 10.0, curve, {}, 2.0);
    const auto second = atlas::domain::remapAnchor(
        anchor(atlas::domain::AnchorAffinity::normalized, 4.0, 0.4), 10.0, curve, {}, 2.0);

    EXPECT_EQ(first.anchor.resolvedStation, second.anchor.resolvedStation);
    EXPECT_EQ(first.anchor.remapSignature.primitiveId, second.anchor.remapSignature.primitiveId);
    EXPECT_EQ(first.anchor.remapSignature.primitiveT, second.anchor.remapSignature.primitiveT);
}
