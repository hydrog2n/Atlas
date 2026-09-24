#include "atlas/render/hit_test.hpp"

#include <gtest/gtest.h>

#include <chrono>
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
