#include "atlas/application/selection.hpp"

#include <gtest/gtest.h>

// Verifies that selecting an object replaces the prior primary selection by default.
TEST(SelectionState, SelectReplacesPrimarySelection) {
    atlas::application::SelectionState selection;
    selection.select("first");
    selection.select("second");

    ASSERT_EQ(selection.selectedIds().size(), 1);
    EXPECT_EQ(selection.primaryId(), "second");
}

// Verifies that additive selection retains all selected IDs and updates the primary ID.
TEST(SelectionState, AdditiveSelectionRetainsSelectedObjects) {
    atlas::application::SelectionState selection;
    selection.select("first");
    selection.select("second", true);

    EXPECT_EQ(selection.selectedIds(), (std::vector<std::string>{"first", "second"}));
    EXPECT_EQ(selection.primaryId(), "second");
}

// Verifies that cycling reaches every candidate in deterministic order.
TEST(SelectionState, CycleReachesEveryCandidate) {
    atlas::application::SelectionState selection;
    const std::vector<std::string> candidates{"a", "b", "c"};

    ASSERT_TRUE(selection.cycle(candidates));
    EXPECT_EQ(selection.primaryId(), "a");
    ASSERT_TRUE(selection.cycle(candidates));
    EXPECT_EQ(selection.primaryId(), "b");
    ASSERT_TRUE(selection.cycle(candidates));
    EXPECT_EQ(selection.primaryId(), "c");
}

// Verifies that clearing selection removes both selected IDs and primary context.
TEST(SelectionState, ClearRemovesSelectionContext) {
    atlas::application::SelectionState selection;
    selection.select("first");
    selection.clear();

    EXPECT_TRUE(selection.selectedIds().empty());
    EXPECT_TRUE(selection.primaryId().empty());
}
