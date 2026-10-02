#include "atlas/render/hit_test.hpp"
#include "atlas/geometry/envelope.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <string>
#include <vector>

// Measures dense hit testing and records a reproducible baseline for the v0.4 viewport target.
TEST(Performance, DenseHitTestingRecordsTenThousandObjectBaseline) {
    atlas::render::HitTester tester;
    std::vector<atlas::render::HitTarget> targets;
    targets.reserve(10000);
    for (int index = 0; index < 10000; ++index) {
        targets.push_back({"object-" + std::to_string(index), "core.Point", {0.0, 0.0}, index % 4, true, false});
    }

    const auto start = std::chrono::steady_clock::now();
    const auto hits = tester.orderedHits({0.0, 0.0}, targets, 1.0);
    const auto elapsed = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - start).count();
    RecordProperty("objectCount", 10000);
    RecordProperty("elapsedMilliseconds", elapsed);

    EXPECT_EQ(hits.size(), 10000);
    EXPECT_LT(elapsed, 1000.0);
}

// Records cold generation, warm cache-hit, and station-query timings for a named deterministic road dataset.
TEST(Performance, RoadEnvelopeRecordsAnalyticAndWarmRepeatBaseline) {
    using Clock = std::chrono::steady_clock;
    const auto hardwareEnvironment = std::getenv("ATLAS_BENCHMARK_HARDWARE");
    const std::string hardware = hardwareEnvironment == nullptr ? "unspecified" : hardwareEnvironment;
    const atlas::geometry::CubicBezierPrimitive source{
        "benchmark-bezier", {"p0", "p1", "p2", "p3"},
        {{{0.0, 0.0}, {30.0, 0.0}, {70.0, 20.0}, {100.0, 20.0}}}};

    const auto curveStart = Clock::now();
    atlas::geometry::CurveKernel curve({source});
    const auto curveConstructionMilliseconds = std::chrono::duration<double, std::milli>(
        Clock::now() - curveStart).count();
    atlas::geometry::RoadEnvelopeOptions options;
    options.totalWidthMeters = 8.0;
    options.maximumStationStep = 0.5;
    options.maximumChordErrorMeters = 0.01;

    atlas::geometry::RoadEnvelopeCache envelopeCache;
    const auto coldEnvelopeStart = Clock::now();
    const auto coldEnvelopeLookup = envelopeCache.getOrGenerate("benchmark-road-source-v1", curve, options);
    const auto coldEnvelopeMilliseconds = std::chrono::duration<double, std::milli>(
        Clock::now() - coldEnvelopeStart).count();
    ASSERT_FALSE(coldEnvelopeLookup.cacheHit);
    ASSERT_TRUE(coldEnvelopeLookup.result->valid);

    std::vector<double> stationQueryMilliseconds;
    std::vector<double> envelopeMilliseconds;
    double stationChecksum = 0.0;
    for (int repeat = 0; repeat < 21; ++repeat) {
        const auto stationStart = Clock::now();
        for (int query = 0; query < 500; ++query) {
            const auto station = curve.totalLength() * static_cast<double>(query) / 500.0;
            stationChecksum += curve.evaluateAtStation(station).position.x;
        }
        stationQueryMilliseconds.push_back(std::chrono::duration<double, std::milli>(
            Clock::now() - stationStart).count());

        const auto envelopeStart = Clock::now();
        const auto envelope = envelopeCache.getOrGenerate("benchmark-road-source-v1", curve, options);
        envelopeMilliseconds.push_back(std::chrono::duration<double, std::milli>(
            Clock::now() - envelopeStart).count());
        ASSERT_TRUE(envelope.cacheHit);
        ASSERT_TRUE(envelope.result->valid);
        EXPECT_EQ(envelope.result->deterministicHash, coldEnvelopeLookup.result->deterministicHash);
    }

    const auto medianAndP95 = [](std::vector<double> samples) {
        std::sort(samples.begin(), samples.end());
        return std::pair{samples[samples.size() / 2], samples[
            static_cast<std::size_t>(std::ceil(0.95 * static_cast<double>(samples.size()))) - 1]};
    };
    const auto [stationMedian, stationP95] = medianAndP95(stationQueryMilliseconds);
    const auto [envelopeMedian, envelopeP95] = medianAndP95(envelopeMilliseconds);

    RecordProperty("hardware", hardware);
    RecordProperty("operatingSystem", "Windows");
    RecordProperty("cacheModel", "RoadEnvelopeCache keyed by authoritative source signature, algorithm/tolerance versions, and all options.");
    RecordProperty("dataset", "100m cubic-bezier RoadSpline; 8m uniform width; 0.5m max station span; 0.01m chord tolerance.");
    RecordProperty("geometryEngineVersion", std::string(atlas::geometry::curveKernelVersion));
    RecordProperty("envelopeAlgorithmVersion", std::string(atlas::geometry::roadEnvelopeAlgorithmVersion));
    RecordProperty("curveConstructionMilliseconds", curveConstructionMilliseconds);
    RecordProperty("coldEnvelopeMilliseconds", coldEnvelopeMilliseconds);
    RecordProperty("stationQueriesPerSample", 500);
    RecordProperty("stationQueryMedianMilliseconds", stationMedian);
    RecordProperty("stationQueryP95Milliseconds", stationP95);
    RecordProperty("envelopeMedianMilliseconds", envelopeMedian);
    RecordProperty("envelopeP95Milliseconds", envelopeP95);
    RecordProperty("envelopeCacheHitCount", envelopeCache.size());
    RecordProperty("envelopeVertexCount", coldEnvelopeLookup.result->boundedPreview.size());
    RecordProperty("stationChecksum", stationChecksum);
    EXPECT_LE(coldEnvelopeLookup.result->maximumMeasuredChordDeviationMeters, options.maximumChordErrorMeters);
}
