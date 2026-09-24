#include "atlas/render/layer.hpp"

#include <gtest/gtest.h>

#include <string>
#include <vector>

// Verifies that derived render layers have a stable composition order.
TEST(RenderLayers, CompositionOrderIsDeterministic) {
    const atlas::render::LayerStack stack;
    const auto& ordered = stack.orderedLayers();

    ASSERT_EQ(ordered.front(), atlas::render::LayerKind::background);
    EXPECT_EQ(ordered[1], atlas::render::LayerKind::grid);
    EXPECT_EQ(ordered.back(), atlas::render::LayerKind::annotations);
}

// Verifies that presentation layers remain independent of authoritative object identity.
TEST(RenderLayers, LayerOrderDoesNotDependOnObjectIds) {
    const std::vector<std::string> objectIds{"zeta", "alpha"};

    EXPECT_NE(objectIds.front(), objectIds.back());
    const atlas::render::LayerStack stack;
    EXPECT_EQ(stack.orderedLayers()[2], atlas::render::LayerKind::geometry);
}
