#include "atlas/domain/project.hpp"

#include <gtest/gtest.h>

TEST(Project, EmptyProjectHasStableRootMap) {
    const auto project = atlas::domain::Project::empty("project", "root-map");

    EXPECT_EQ(project.id(), "project");
    EXPECT_EQ(project.rootMap().id, "root-map");
}

TEST(Project, NormalizedJsonIsDeterministic) {
    const auto first = atlas::domain::Project::empty("project", "root-map");
    const auto second = atlas::domain::Project::empty("project", "root-map");

    EXPECT_EQ(first.normalizedJson(), second.normalizedJson());
}
