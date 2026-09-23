#include "atlas/domain/project.hpp"

#include <gtest/gtest.h>

// These tests describe the first project-level requirements.
// They are intentionally small and direct: they verify identity, serialization,
// validation, and preservation of extension data.

TEST(Project, R010_003_EmptyProjectHasStableRootMap) {
    const auto project = atlas::domain::Project::empty("project", "root-map");

    EXPECT_EQ(project.id(), "project");
    EXPECT_EQ(project.rootMap().id, "root-map");
}

TEST(Project, R010_005_NormalizedJsonIsDeterministic) {
    const auto first = atlas::domain::Project::empty("project", "root-map");
    const auto second = atlas::domain::Project::empty("project", "root-map");

    EXPECT_EQ(first.normalizedJson(), second.normalizedJson());
}

TEST(Project, R010_005_JsonRoundTripPreservesIdentityAndRootMap) {
    const auto original = atlas::domain::Project::empty("project", "root-map");
    const auto reloaded = atlas::domain::Project::fromJson(original.normalizedJson());

    EXPECT_EQ(reloaded.id(), original.id());
    EXPECT_EQ(reloaded.rootMap().id, original.rootMap().id);
    EXPECT_EQ(reloaded.normalizedJson(), original.normalizedJson());
}

TEST(Project, R010_005_JsonParserRejectsMissingProjectId) {
    const auto invalid = R"({
        "schemaVersion": 1,
        "units": "m",
        "maps": [
            {"id": "root-map", "parentMapId": null, "objects": [], "networkObjects": []}
        ]
    })";

    EXPECT_THROW(atlas::domain::Project::fromJson(invalid), std::invalid_argument);
}

TEST(Project, R010_005_JsonParserPreservesUnknownExtensionData) {
    const auto json = R"({
        "id": "project",
        "schemaVersion": 1,
        "units": "m",
        "maps": [
            {"id": "root-map", "parentMapId": null, "objects": [], "networkObjects": []}
        ],
        "extensions": {
            "custom": {"flag": true},
            "tags": ["a", "b"]
        }
    })";

    const auto project = atlas::domain::Project::fromJson(json);

    EXPECT_TRUE(project.extensions().contains("custom"));
    EXPECT_EQ(project.extensions()["tags"][1], "b");
    EXPECT_EQ(project.normalizedJson(), project.normalizedJson());
}

TEST(Project, R020_001_JsonParserPreservesUnknownFields) {
    const auto json = R"({
        "id": "project",
        "schemaVersion": 1,
        "units": "m",
        "maps": [
            {"id": "root-map", "parentMapId": null, "objects": [], "networkObjects": []}
        ],
        "futureField": {"value": 42}
    })";

    const auto project = atlas::domain::Project::fromJson(json);

    EXPECT_EQ(project.unknownFields()["futureField"]["value"], 42);
    EXPECT_EQ(project.normalizedJson(), project.normalizedJson());
}
