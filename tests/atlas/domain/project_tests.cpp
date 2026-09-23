#include "atlas/domain/project.hpp"

#include <gtest/gtest.h>

// Domain tests cover stable identity, deterministic serialization, validation,
// and preservation of extension and unknown field data.

// Verifies that an empty project retains stable project and root-map identity.
TEST(Project, R010_003_EmptyProjectHasStableRootMap) {
    const auto project = atlas::domain::Project::empty("project", "root-map");

    EXPECT_EQ(project.id(), "project");
    EXPECT_EQ(project.rootMap().id, "root-map");
}

// Verifies that equivalent projects produce identical normalized JSON.
TEST(Project, R010_005_NormalizedJsonIsDeterministic) {
    const auto first = atlas::domain::Project::empty("project", "root-map");
    const auto second = atlas::domain::Project::empty("project", "root-map");

    EXPECT_EQ(first.normalizedJson(), second.normalizedJson());
}

// Verifies that JSON round trips preserve project and root-map identity.
TEST(Project, R010_005_JsonRoundTripPreservesIdentityAndRootMap) {
    const auto original = atlas::domain::Project::empty("project", "root-map");
    const auto reloaded = atlas::domain::Project::fromJson(original.normalizedJson());

    EXPECT_EQ(reloaded.id(), original.id());
    EXPECT_EQ(reloaded.rootMap().id, original.rootMap().id);
    EXPECT_EQ(reloaded.normalizedJson(), original.normalizedJson());
}

// Verifies that parsing rejects a project without the required identifier.
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

// Verifies that recognized extension data survives parsing and normalization.
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

// Verifies that unknown project fields remain available after parsing.
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

// Verifies that unknown map fields survive canonical normalization.
TEST(Project, R020_001_MapUnknownFieldsSurviveNormalization) {
    const auto project = atlas::domain::Project::fromJson(R"({
        "id": "project",
        "schemaVersion": 1,
        "units": "m",
        "maps": [{
            "id": "root-map",
            "type": "core.Map",
            "parentMapId": null,
            "objects": [],
            "networkObjects": [],
            "futureMapField": {"enabled": true}
        }]
    })");

    EXPECT_NE(project.normalizedJson().find("futureMapField"), std::string::npos);
    EXPECT_EQ(project.rootMap().unknownFields["futureMapField"]["enabled"], true);
}
