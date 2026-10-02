#include "atlas/geometry/curve.hpp"
#include "atlas/geometry/envelope.hpp"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

atlas::geometry::PolylinePrimitive line(
    std::string id, std::string startId, std::string endId,
    atlas::geometry::Point2D start, atlas::geometry::Point2D end) {
    return {std::move(id), std::move(startId), std::move(endId), start, end};
}

} // namespace

// Verifies analytic line length, station evaluation, and the left-handed moving frame.
TEST(CurveKernel, PolylineLengthStationAndFrameAreAnalytic) {
    atlas::geometry::CurveKernel curve({line("line", "a", "b", {0.0, 0.0}, {3.0, 4.0})});

    EXPECT_DOUBLE_EQ(curve.totalLength(), 5.0);
    const auto midpoint = curve.evaluateAtStation(2.5);
    EXPECT_NEAR(midpoint.position.x, 1.5, 1.0e-12);
    EXPECT_NEAR(midpoint.position.y, 2.0, 1.0e-12);
    EXPECT_NEAR(midpoint.tangent.x, 0.6, 1.0e-12);
    EXPECT_NEAR(midpoint.tangent.y, 0.8, 1.0e-12);
    EXPECT_NEAR(midpoint.normal.x, -0.8, 1.0e-12);
    EXPECT_NEAR(midpoint.normal.y, 0.6, 1.0e-12);
}

// Verifies station and primitive parameter round trips for cubic Bezier curves.
TEST(CurveKernel, CubicBezierStationParameterRoundTrips) {
    const atlas::geometry::CubicBezierPrimitive bezier{
        "bezier", {"p0", "p1", "p2", "p3"},
        {{{0.0, 0.0}, {1.0, 0.0}, {2.0, 1.0}, {3.0, 1.0}}}};
    atlas::geometry::CurveKernel curve({bezier});

    auto previousStation = -1.0;
    for (const auto parameter : {0.0, 0.1, 0.25, 0.5, 0.75, 0.9, 1.0}) {
        const auto station = curve.stationOfPrimitiveParameter("bezier", parameter);
        EXPECT_GT(station, previousStation);
        previousStation = station;
        const auto resolved = curve.primitiveParameterAtStation(station);
        EXPECT_EQ(resolved.primitiveId, "bezier");
        EXPECT_NEAR(resolved.t, parameter, 2.0e-4);
        EXPECT_LE(std::abs(curve.stationOfPrimitiveParameter(resolved.primitiveId, resolved.t) - station),
            1.0e-4);
    }
    EXPECT_GT(curve.totalLength(), 3.0);
}

// Verifies primitive outputs against geometry-engine-versioned golden records.
TEST(CurveKernelGolden, VersionedPrimitiveCasesMatchExpectedOutput) {
    std::ifstream input(std::filesystem::path(ATLAS_SOURCE_DIR) /
        "tests" / "fixtures" / "geometry" / "curve-kernel-v1.json");
    ASSERT_TRUE(input.good());
    const auto fixture = nlohmann::json::parse(input);
    EXPECT_EQ(fixture["schemaGeneration"], 1);
    EXPECT_EQ(fixture["geometryEngineVersion"].get<std::string>(),
        std::string(atlas::geometry::curveKernelVersion));

    for (const auto& item : fixture["cases"]) {
        const auto points = item["points"];
        const auto ids = item["controlPointIds"];
        atlas::geometry::CurvePrimitive primitive;
        if (item["kind"] == "polyline") {
            primitive = atlas::geometry::PolylinePrimitive{
                item["primitiveId"], ids[0], ids[1],
                {points[0][0], points[0][1]}, {points[1][0], points[1][1]}};
        } else if (item["kind"] == "cubic-bezier") {
            primitive = atlas::geometry::CubicBezierPrimitive{
                item["primitiveId"], {ids[0], ids[1], ids[2], ids[3]},
                {{{points[0][0], points[0][1]}, {points[1][0], points[1][1]},
                    {points[2][0], points[2][1]}, {points[3][0], points[3][1]}}}};
        } else {
            primitive = atlas::geometry::CircularArcPrimitive{
                item["primitiveId"], ids[0], ids[1],
                {points[0][0], points[0][1]}, {points[1][0], points[1][1]},
                item["radius"], item["side"] == "left"
                    ? atlas::geometry::ArcSide::left : atlas::geometry::ArcSide::right};
        }
        atlas::geometry::CurveKernel curve({std::move(primitive)});
        const auto evaluation = curve.evaluateAtStation(item["station"]);
        EXPECT_NEAR(curve.totalLength(), item["length"], 1.0e-9);
        EXPECT_NEAR(evaluation.position.x, item["position"][0], 1.0e-9);
        EXPECT_NEAR(evaluation.position.y, item["position"][1], 1.0e-9);
        EXPECT_NEAR(evaluation.tangent.x, item["tangent"][0], 1.0e-9);
        EXPECT_NEAR(evaluation.tangent.y, item["tangent"][1], 1.0e-9);
    }
}

// Verifies that a fixed-radius arc uses the requested minor-arc side and exact length.
TEST(CurveKernel, FixedRadiusArcSelectsRequestedMinorSide) {
    const atlas::geometry::CircularArcPrimitive arc{
        "arc", "start", "end", {0.0, 0.0}, {2.0, 0.0}, std::sqrt(2.0), atlas::geometry::ArcSide::left};
    atlas::geometry::CurveKernel curve({arc});
    const auto midpoint = curve.evaluateAtStation(curve.totalLength() * 0.5);

    EXPECT_NEAR(curve.totalLength(), std::acos(-1.0) * std::sqrt(2.0) * 0.5, 1.0e-12);
    EXPECT_GT(midpoint.position.y, 0.0);
}

// Verifies a right-side fixed-radius arc bulges to the right of its directed chord.
TEST(CurveKernel, FixedRadiusArcSelectsRightSide) {
    const atlas::geometry::CircularArcPrimitive arc{
        "arc", "start", "end", {0.0, 0.0}, {2.0, 0.0}, std::sqrt(2.0), atlas::geometry::ArcSide::right};
    atlas::geometry::CurveKernel curve({arc});

    EXPECT_LT(curve.evaluateAtStation(curve.totalLength() * 0.5).position.y, 0.0);
}

// Verifies signed curvature is analytic for line and arc primitives and derivative-based for Beziers.
TEST(CurveKernel, PrimitiveParameterEvaluationAndCurvatureAreAnalytic) {
    atlas::geometry::CurveKernel lineCurve({line("line", "a", "b", {0.0, 0.0}, {4.0, 0.0})});
    EXPECT_DOUBLE_EQ(lineCurve.curvatureAtPrimitiveParameter("line", 0.5), 0.0);
    EXPECT_DOUBLE_EQ(lineCurve.evaluateAtPrimitiveParameter("line", 0.5).position.x, 2.0);

    atlas::geometry::CurveKernel leftArc({atlas::geometry::CircularArcPrimitive{
        "left", "a", "b", {0.0, 0.0}, {2.0, 0.0}, std::sqrt(2.0), atlas::geometry::ArcSide::left}});
    atlas::geometry::CurveKernel rightArc({atlas::geometry::CircularArcPrimitive{
        "right", "a", "b", {0.0, 0.0}, {2.0, 0.0}, std::sqrt(2.0), atlas::geometry::ArcSide::right}});
    EXPECT_NEAR(leftArc.curvatureAtPrimitiveParameter("left", 0.5), -1.0 / std::sqrt(2.0), 1.0e-12);
    EXPECT_NEAR(rightArc.curvatureAtPrimitiveParameter("right", 0.5), 1.0 / std::sqrt(2.0), 1.0e-12);

    const atlas::geometry::CubicBezierPrimitive bezier{
        "bezier", {"p0", "p1", "p2", "p3"},
        {{{0.0, 0.0}, {1.0, 0.0}, {2.0, 1.0}, {3.0, 1.0}}}};
    atlas::geometry::CurveKernel bezierCurve({bezier});
    EXPECT_NEAR(bezierCurve.curvatureAtPrimitiveParameter("bezier", 0.0), 2.0 / 3.0, 1.0e-12);
    EXPECT_THROW(bezierCurve.curvatureAtPrimitiveParameter("missing", 0.5), std::invalid_argument);
}

// Verifies a persisted total width creates a centered, deterministic, version-tagged road envelope.
TEST(RoadEnvelope, StraightRoadProducesDeterministicCenteredPolygon) {
    atlas::geometry::CurveKernel curve({line("line", "a", "b", {0.0, 0.0}, {4.0, 0.0})});
    atlas::geometry::RoadEnvelopeOptions options;
    options.totalWidthMeters = 8.0;
    const auto first = atlas::geometry::generateRoadEnvelope(curve, options);
    const auto second = atlas::geometry::generateRoadEnvelope(curve, options);

    ASSERT_TRUE(first.valid);
    ASSERT_EQ(first.polygon.size(), first.boundedPreview.size());
    EXPECT_EQ(first.polygon.front().y, 4.0);
    EXPECT_EQ(first.polygon.back().y, -4.0);
    EXPECT_EQ(first.deterministicHash, second.deterministicHash);
    EXPECT_EQ(first.algorithmVersion, atlas::geometry::roadEnvelopeAlgorithmVersion);
    EXPECT_EQ(first.toleranceVersion, atlas::geometry::roadEnvelopeToleranceVersion);
}

// Verifies derived envelope cache keys include authoritative source and options and remain bounded.
TEST(RoadEnvelopeCache, ReusesExactInputsAndSeparatesChangedSourcesOrOptions) {
    atlas::geometry::CurveKernel curve({line("line", "a", "b", {0.0, 0.0}, {10.0, 0.0})});
    atlas::geometry::RoadEnvelopeOptions options;
    options.totalWidthMeters = 8.0;
    atlas::geometry::RoadEnvelopeCache cache(2);

    const auto cold = cache.getOrGenerate("source-a", curve, options);
    const auto warm = cache.getOrGenerate("source-a", curve, options);
    EXPECT_FALSE(cold.cacheHit);
    EXPECT_TRUE(warm.cacheHit);
    EXPECT_EQ(cold.result, warm.result);

    options.totalWidthMeters = 6.0;
    EXPECT_FALSE(cache.getOrGenerate("source-a", curve, options).cacheHit);
    EXPECT_FALSE(cache.getOrGenerate("source-b", curve, options).cacheHit);
    EXPECT_LE(cache.size(), 2);
    cache.clear();
    EXPECT_EQ(cache.size(), 0);
    EXPECT_THROW(atlas::geometry::RoadEnvelopeCache(0), std::invalid_argument);
    EXPECT_THROW(cache.getOrGenerate("", curve, options), std::invalid_argument);
}

// Verifies cubic offset subdivision meets the configured measured chord-deviation tolerance.
TEST(RoadEnvelope, CubicBezierOffsetHonorsChordErrorAndSamplingBounds) {
    const atlas::geometry::CubicBezierPrimitive bezier{
        "bezier", {"p0", "p1", "p2", "p3"},
        {{{0.0, 0.0}, {3.0, 0.0}, {7.0, 1.0}, {10.0, 0.0}}}};
    atlas::geometry::CurveKernel curve({bezier});
    atlas::geometry::RoadEnvelopeOptions options;
    options.totalWidthMeters = 2.0;
    options.maximumStationStep = 2.0;
    options.maximumChordErrorMeters = 0.001;
    const auto result = atlas::geometry::generateRoadEnvelope(curve, options);
    const auto repeated = atlas::geometry::generateRoadEnvelope(curve, options);

    ASSERT_TRUE(result.valid) << (result.issues.empty() ? "no issue" : result.issues.front().message);
    EXPECT_LE(result.maximumMeasuredChordDeviationMeters, options.maximumChordErrorMeters);
    EXPECT_LE(result.boundedPreview.size(), options.maximumPreviewVertices);
    EXPECT_EQ(result.deterministicHash, repeated.deterministicHash);
}

// Verifies the basic road envelope against a version-tagged deterministic golden fixture.
TEST(RoadEnvelopeGolden, VersionedStraightEnvelopeMatchesExpectedPolygon) {
    std::ifstream input(std::filesystem::path(ATLAS_SOURCE_DIR) /
        "tests" / "fixtures" / "geometry" / "road-envelope-v1.json");
    ASSERT_TRUE(input.good());
    const auto fixture = nlohmann::json::parse(input);
    EXPECT_EQ(fixture["schemaGeneration"], 1);
    EXPECT_EQ(fixture["geometryEngineVersion"], std::string(atlas::geometry::curveKernelVersion));
    EXPECT_EQ(fixture["envelopeAlgorithmVersion"],
        std::string(atlas::geometry::roadEnvelopeAlgorithmVersion));
    EXPECT_EQ(fixture["toleranceVersion"], std::string(atlas::geometry::roadEnvelopeToleranceVersion));

    for (const auto& testCase : fixture["cases"]) {
        const auto start = testCase["start"];
        const auto end = testCase["end"];
        atlas::geometry::CurveKernel curve = [&]() {
            if (testCase["kind"] == "line") {
                return atlas::geometry::CurveKernel({line(testCase["name"], "start", "end",
                    {start[0], start[1]}, {end[0], end[1]})});
            }
            return atlas::geometry::CurveKernel({atlas::geometry::CircularArcPrimitive{
                testCase["name"], "start", "end", {start[0], start[1]}, {end[0], end[1]},
                testCase["radius"], testCase["side"] == "left"
                    ? atlas::geometry::ArcSide::left : atlas::geometry::ArcSide::right}});
        }();
        atlas::geometry::RoadEnvelopeOptions options;
        options.totalWidthMeters = testCase["totalWidthMeters"];
        options.maximumStationStep = testCase["maximumStationStep"];
        options.maximumChordErrorMeters = testCase.value("maximumChordErrorMeters", 0.01);
        const auto result = atlas::geometry::generateRoadEnvelope(curve, options);
        ASSERT_TRUE(result.valid);
        if (testCase.contains("polygon")) {
            ASSERT_EQ(result.polygon.size(), testCase["polygon"].size());
            for (std::size_t index = 0; index < result.polygon.size(); ++index) {
                EXPECT_NEAR(result.polygon[index].x, testCase["polygon"][index][0], 1.0e-12);
                EXPECT_NEAR(result.polygon[index].y, testCase["polygon"][index][1], 1.0e-12);
            }
        } else {
            EXPECT_EQ(result.polygon.size(), testCase["expectedVertexCount"].get<std::size_t>());
            EXPECT_EQ(result.deterministicHash, testCase["expectedHash"].get<std::string>());
            EXPECT_LE(result.maximumMeasuredChordDeviationMeters, options.maximumChordErrorMeters);
        }
    }
}

// Verifies polyline corner output follows the selected round, bevel, and bounded-miter join styles.
TEST(RoadEnvelope, PolylineCornersHonorExplicitJoinStyles) {
    atlas::geometry::CurveKernel curve({
        line("first", "a", "joint", {0.0, 0.0}, {4.0, 0.0}),
        line("second", "joint", "b", {4.0, 0.0}, {4.0, 4.0})});
    atlas::geometry::RoadEnvelopeOptions options;
    options.totalWidthMeters = 2.0;
    const auto round = atlas::geometry::generateRoadEnvelope(curve, options);
    options.joinStyle = atlas::geometry::JoinStyle::bevel;
    const auto bevel = atlas::geometry::generateRoadEnvelope(curve, options);
    options.joinStyle = atlas::geometry::JoinStyle::miter;
    options.miterLimit = 4.0;
    const auto miter = atlas::geometry::generateRoadEnvelope(curve, options);

    ASSERT_TRUE(round.valid) << (round.issues.empty() ? "no issue" : round.issues.front().message);
    ASSERT_TRUE(bevel.valid);
    ASSERT_TRUE(miter.valid);
    EXPECT_GT(round.polygon.size(), bevel.polygon.size());
    EXPECT_NE(bevel.deterministicHash, miter.deterministicHash);
    EXPECT_LE(round.maximumMeasuredChordDeviationMeters, options.maximumChordErrorMeters);
    options.miterLimit = 1.0;
    const auto exceededMiter = atlas::geometry::generateRoadEnvelope(curve, options);
    EXPECT_FALSE(exceededMiter.valid);
    EXPECT_NE(std::find_if(exceededMiter.issues.begin(), exceededMiter.issues.end(), [](const auto& issue) {
        return issue.code == atlas::geometry::EnvelopeIssueCode::miterLimitExceeded;
    }), exceededMiter.issues.end());
}

// Verifies local radius diagnostics, orientation failures, self-intersection, and bounded invalid previews.
TEST(RoadEnvelope, InvalidOffsetsReportDiagnosticsAndBoundPreview) {
    atlas::geometry::RoadEnvelopeOptions options;
    options.totalWidthMeters = 8.0;
    const auto tightCurve = atlas::geometry::CurveKernel({atlas::geometry::CircularArcPrimitive{
        "tight", "a", "b", {0.0, 0.0}, {2.0, 0.0}, 3.0, atlas::geometry::ArcSide::left}});
    const auto tight = atlas::geometry::generateRoadEnvelope(tightCurve, options);
    EXPECT_FALSE(tight.valid);
    EXPECT_LE(tight.boundedPreview.size(), options.maximumPreviewVertices);
    EXPECT_NE(tight.polygon.size(), tight.boundedPreview.size());
    EXPECT_NE(std::find_if(tight.issues.begin(), tight.issues.end(), [](const auto& issue) {
        return issue.code == atlas::geometry::EnvelopeIssueCode::tightRadius;
    }), tight.issues.end());
    EXPECT_NE(std::find_if(tight.issues.begin(), tight.issues.end(), [](const auto& issue) {
        return issue.code == atlas::geometry::EnvelopeIssueCode::orientationInversion;
    }), tight.issues.end());

    const auto cuspCurve = atlas::geometry::CurveKernel({atlas::geometry::CircularArcPrimitive{
        "cusp", "a", "b", {0.0, 0.0}, {2.0, 0.0}, 4.0, atlas::geometry::ArcSide::left}});
    const auto cusp = atlas::geometry::generateRoadEnvelope(cuspCurve, options);
    EXPECT_NE(std::find_if(cusp.issues.begin(), cusp.issues.end(), [](const auto& issue) {
        return issue.code == atlas::geometry::EnvelopeIssueCode::cusp;
    }), cusp.issues.end());

    const auto loopCurve = atlas::geometry::CurveKernel({
        line("a", "p0", "p1", {0.0, 0.0}, {4.0, 0.0}),
        line("b", "p1", "p2", {4.0, 0.0}, {0.0, 1.0}),
        line("c", "p2", "p3", {0.0, 1.0}, {4.0, 1.0})});
    options.totalWidthMeters = 4.0;
    const auto loop = atlas::geometry::generateRoadEnvelope(loopCurve, options);
    EXPECT_FALSE(loop.valid);
    EXPECT_NE(std::find_if(loop.issues.begin(), loop.issues.end(), [](const auto& issue) {
        return issue.code == atlas::geometry::EnvelopeIssueCode::selfIntersection;
    }), loop.issues.end());
}

// Verifies malformed widths, missing miter limits, and excessive curve complexity fail safely.
TEST(RoadEnvelope, InvalidOptionsAndSamplingLimitAreBounded) {
    atlas::geometry::CurveKernel curve({line("line", "a", "b", {0.0, 0.0}, {10.0, 0.0})});
    atlas::geometry::RoadEnvelopeOptions options;
    EXPECT_THROW(atlas::geometry::generateRoadEnvelope(curve, options), std::invalid_argument);
    options.totalWidthMeters = 4.0;
    options.joinStyle = atlas::geometry::JoinStyle::miter;
    EXPECT_THROW(atlas::geometry::generateRoadEnvelope(curve, options), std::invalid_argument);
    options.joinStyle = atlas::geometry::JoinStyle::round;
    options.maximumChordErrorMeters = 0.0;
    EXPECT_THROW(atlas::geometry::generateRoadEnvelope(curve, options), std::invalid_argument);
    options.maximumChordErrorMeters = 0.01;
    options.maximumSamples = 4;
    const auto bounded = atlas::geometry::generateRoadEnvelope(curve, options);
    EXPECT_FALSE(bounded.valid);
    EXPECT_LE(bounded.boundedPreview.size(), options.maximumPreviewVertices);
    EXPECT_EQ(bounded.issues.front().code, atlas::geometry::EnvelopeIssueCode::resourceLimit);
}

// Verifies round-join tessellation and polygon intersection checks share a hard aggregate work bound.
TEST(RoadEnvelope, JoinAndIntersectionWorkExhaustionReturnsBoundedPreview) {
    atlas::geometry::CurveKernel corner({
        line("first", "a", "joint", {0.0, 0.0}, {4.0, 0.0}),
        line("second", "joint", "b", {4.0, 0.0}, {4.0, 4.0})});
    atlas::geometry::RoadEnvelopeOptions options;
    options.totalWidthMeters = 2.0;
    options.maximumChordErrorMeters = 1.0e-12;
    options.maximumWorkItems = 32;
    const auto joinLimited = atlas::geometry::generateRoadEnvelope(corner, options);
    EXPECT_FALSE(joinLimited.valid);
    EXPECT_LE(joinLimited.boundedPreview.size(), options.maximumPreviewVertices);
    EXPECT_NE(std::find_if(joinLimited.issues.begin(), joinLimited.issues.end(), [](const auto& issue) {
        return issue.code == atlas::geometry::EnvelopeIssueCode::resourceLimit;
    }), joinLimited.issues.end());

    auto broadCurve = atlas::geometry::CurveKernel({
        line("a", "p0", "p1", {0.0, 0.0}, {4.0, 0.0}),
        line("b", "p1", "p2", {4.0, 0.0}, {4.0, 4.0}),
        line("c", "p2", "p3", {4.0, 4.0}, {0.0, 4.0}),
        line("d", "p3", "p4", {0.0, 4.0}, {-2.0, 4.0})});
    options.maximumChordErrorMeters = 0.01;
    options.joinStyle = atlas::geometry::JoinStyle::bevel;
    options.maximumWorkItems = 8;
    const auto intersectionLimited = atlas::geometry::generateRoadEnvelope(broadCurve, options);
    EXPECT_FALSE(intersectionLimited.valid);
    EXPECT_LE(intersectionLimited.boundedPreview.size(), options.maximumPreviewVertices);
    EXPECT_NE(std::find_if(intersectionLimited.issues.begin(), intersectionLimited.issues.end(),
        [](const auto& issue) { return issue.code == atlas::geometry::EnvelopeIssueCode::resourceLimit; }),
        intersectionLimited.issues.end());

    std::vector<atlas::geometry::CurvePrimitive> manyEdgePath;
    for (std::size_t index = 0; index < 64; ++index) {
        const auto startY = index % 2 == 0 ? 0.0 : 1.0;
        const auto endY = index % 2 == 0 ? 1.0 : 0.0;
        manyEdgePath.push_back(line("edge-" + std::to_string(index),
            "point-" + std::to_string(index), "point-" + std::to_string(index + 1),
            {static_cast<double>(index) * 2.0, startY},
            {static_cast<double>(index + 1) * 2.0, endY}));
    }
    const atlas::geometry::CurveKernel manyEdgeCurve(std::move(manyEdgePath));
    options.totalWidthMeters = 0.1;
    options.maximumWorkItems = 64;
    const auto manyEdgeLimited = atlas::geometry::generateRoadEnvelope(manyEdgeCurve, options);
    EXPECT_FALSE(manyEdgeLimited.valid);
    EXPECT_LE(manyEdgeLimited.boundedPreview.size(), options.maximumPreviewVertices);
    EXPECT_NE(std::find_if(manyEdgeLimited.issues.begin(), manyEdgeLimited.issues.end(),
        [](const auto& issue) { return issue.code == atlas::geometry::EnvelopeIssueCode::resourceLimit; }),
        manyEdgeLimited.issues.end());
}

// Verifies that a shallow arc whose angular sweep is lost to floating-point resolution is rejected.
TEST(CurveKernel, NumericallyUnresolvableArcSweepIsRejected) {
    const atlas::geometry::CircularArcPrimitive arc{
        "arc", "start", "end", {0.0, 0.0}, {1.0e-6, 0.0}, 1.0e12, atlas::geometry::ArcSide::left};

    EXPECT_THROW(atlas::geometry::CurveKernel({arc}), std::invalid_argument);
}

// Verifies that shared-joint station boundaries resolve to the earlier primitive endpoint.
TEST(CurveKernel, SharedBoundaryResolvesToEarlierPrimitive) {
    atlas::geometry::CurveKernel curve({
        line("first", "a", "joint", {0.0, 0.0}, {2.0, 0.0}),
        line("second", "joint", "b", {2.0, 0.0}, {2.0, 3.0})});

    const auto resolved = curve.primitiveParameterAtStation(2.0);

    EXPECT_EQ(resolved.primitiveId, "first");
    EXPECT_DOUBLE_EQ(resolved.t, 1.0);
}

// Verifies that primitive IDs and shared-joint coordinates are structural invariants.
TEST(CurveKernel, DuplicatePrimitiveIdsAndMismatchedJointsAreRejected) {
    EXPECT_THROW(atlas::geometry::CurveKernel({
        line("same", "a", "b", {0.0, 0.0}, {1.0, 0.0}),
        line("same", "b", "c", {1.0, 0.0}, {2.0, 0.0})}), std::invalid_argument);
    EXPECT_THROW(atlas::geometry::CurveKernel({
        line("first", "a", "joint", {0.0, 0.0}, {1.0, 0.0}),
        line("second", "joint", "b", {1.1, 0.0}, {2.0, 0.0})
    }), std::invalid_argument);
}

// Verifies that impossible, closed, non-finite, and zero-tangent curves are rejected.
TEST(CurveKernel, InvalidPrimitiveDefinitionsAreRejected) {
    EXPECT_THROW(atlas::geometry::CurveKernel({line("zero", "a", "b", {0.0, 0.0}, {0.0, 0.0})}), std::invalid_argument);
    EXPECT_THROW(atlas::geometry::CurveKernel({line("closed", "same", "same", {0.0, 0.0}, {1.0, 0.0})}), std::invalid_argument);
    EXPECT_THROW(atlas::geometry::CurveKernel({atlas::geometry::CircularArcPrimitive{
        "arc", "a", "b", {0.0, 0.0}, {2.0, 0.0}, 0.5, atlas::geometry::ArcSide::left}}), std::invalid_argument);
    const atlas::geometry::CubicBezierPrimitive cusp{
        "cusp", {"p0", "p1", "p2", "p3"},
        {{{0.0, 0.0}, {1.0, 0.0}, {0.0, 0.0}, {1.0, 0.0}}}};
    EXPECT_THROW(atlas::geometry::CurveKernel({cusp}), std::invalid_argument);
}

// Verifies that out-of-domain stations and unknown primitive IDs fail explicitly.
TEST(CurveKernel, InvalidQueriesReturnErrors) {
    atlas::geometry::CurveKernel curve({line("line", "a", "b", {0.0, 0.0}, {1.0, 0.0})});

    EXPECT_THROW(curve.evaluateAtStation(-1.0), std::out_of_range);
    EXPECT_THROW(curve.primitiveParameterAtStation(2.0), std::out_of_range);
    EXPECT_THROW(curve.stationOfPrimitiveParameter("missing", 0.5), std::invalid_argument);
    EXPECT_THROW(curve.stationOfPrimitiveParameter("line", 1.5), std::out_of_range);
}

// Verifies deterministic station sampling includes both RoadSpline endpoints.
TEST(CurveKernel, SamplingIsDeterministicAndIncludesEndpoints) {
    atlas::geometry::CurveKernel curve({line("line", "a", "b", {0.0, 0.0}, {2.5, 0.0})});
    const auto first = curve.sampleByStationInterval(1.0);
    const auto second = curve.sampleByStationInterval(1.0);

    ASSERT_EQ(first.size(), 4);
    ASSERT_EQ(second.size(), first.size());
    EXPECT_DOUBLE_EQ(first.front().position.x, 0.0);
    EXPECT_DOUBLE_EQ(first.back().position.x, 2.5);
    for (std::size_t index = 0; index < first.size(); ++index) {
        EXPECT_DOUBLE_EQ(first[index].position.x, second[index].position.x);
    }
}

// Verifies station samples are generated from integer step indices without cumulative drift.
TEST(CurveKernel, SamplingDoesNotAccumulateStepError) {
    atlas::geometry::CurveKernel curve({line("line", "a", "b", {0.0, 0.0}, {1.0, 0.0})});

    const auto samples = curve.sampleByStationInterval(0.1);

    ASSERT_EQ(samples.size(), 11);
    EXPECT_DOUBLE_EQ(samples[9].position.x, 0.9);
    EXPECT_DOUBLE_EQ(samples.back().position.x, 1.0);
}

// Verifies finite but extreme Bezier controls fail explicitly if derivative arithmetic overflows.
TEST(CurveKernel, NonFiniteBezierEvaluationFailsExplicitly) {
    const atlas::geometry::CubicBezierPrimitive bezier{
        "bezier", {"p0", "p1", "p2", "p3"},
        {{{-1.0e308, 0.0}, {-5.0e307, 0.0}, {5.0e307, 0.0}, {1.0e308, 0.0}}}};

    EXPECT_THROW(atlas::geometry::CurveKernel({bezier}), std::runtime_error);
}

// Verifies sampling rejects invalid spacing and bounds the requested output size.
TEST(CurveKernel, SamplingRejectsInvalidIntervalsAndExcessiveCounts) {
    atlas::geometry::CurveKernel curve({line("line", "a", "b", {0.0, 0.0}, {10.0, 0.0})});

    EXPECT_THROW(curve.sampleByStationInterval(0.0), std::invalid_argument);
    EXPECT_THROW(curve.sampleByStationInterval(0.1, 5), std::length_error);
}
