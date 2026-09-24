#include "atlas/render/hit_test.hpp"

#include <gtest/gtest.h>

// Verifies that hit results use explicit precedence before distance and stable ID.
TEST(HitTester, OrdersOverlappingTargetsDeterministically) {
    atlas::render::HitTester tester;
    const std::vector<atlas::render::HitTarget> targets{
        {"z", "core.Point", {0.1, 0.0}, 2, true, false},
        {"a", "core.Point", {0.2, 0.0}, 1, true, false},
        {"b", "core.Point", {0.1, 0.0}, 1, true, false}};

    const auto hits = tester.orderedHits({0.0, 0.0}, targets, 1.0);

    ASSERT_EQ(hits.size(), 3);
    EXPECT_EQ(hits[0].objectId, "b");
    EXPECT_EQ(hits[1].objectId, "a");
    EXPECT_EQ(hits[2].objectId, "z");
}

// Verifies that hidden and locked targets are excluded by default.
TEST(HitTester, ExcludesHiddenAndLockedTargetsByDefault) {
    atlas::render::HitTester tester;
    const std::vector<atlas::render::HitTarget> targets{
        {"hidden", "core.Point", {0.0, 0.0}, 0, false, false},
        {"locked", "core.Point", {0.0, 0.0}, 0, true, true},
        {"editable", "core.Point", {0.0, 0.0}, 0, true, false}};

    const auto hits = tester.orderedHits({0.0, 0.0}, targets, 1.0);

    ASSERT_EQ(hits.size(), 1);
    EXPECT_EQ(hits.front().objectId, "editable");
}

// Verifies that tools can explicitly expose locked and hidden reference targets.
TEST(HitTester, ExplicitFlagsExposeOtherwiseExcludedTargets) {
    atlas::render::HitTester tester;
    const std::vector<atlas::render::HitTarget> targets{
        {"hidden", "core.ReferenceImage", {0.0, 0.0}, 0, false, false},
        {"locked", "core.Point", {0.0, 0.0}, 0, true, true}};

    const auto hits = tester.orderedHits({0.0, 0.0}, targets, 1.0, true, true);

    ASSERT_EQ(hits.size(), 2);
}

// Verifies that overlap ordering remains deterministic for a dense candidate set.
TEST(HitTester, DenseCandidateOrderingIsStable) {
    atlas::render::HitTester tester;
    std::vector<atlas::render::HitTarget> targets;
    for (int index = 0; index < 10000; ++index) {
        targets.push_back({"object-" + std::to_string(index), "core.Point", {0.0, 0.0}, index % 4, true, false});
    }

    const auto hits = tester.orderedHits({0.0, 0.0}, targets, 1.0);

    ASSERT_EQ(hits.size(), 10000);
    EXPECT_EQ(hits.front().objectId, "object-0");
    EXPECT_EQ(hits.back().precedence, 3);
}
