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

// Verifies that generic objects preserve stable identity, type, and geometry.
TEST(Project, R040_003_MapObjectPreservesIdentityAndGeometry) {
    const auto object = atlas::domain::MapObject::create(
        "point-1", "core.Point", {{"x", 12.5}, {"y", -4.0}});

    EXPECT_EQ(object.id(), "point-1");
    EXPECT_EQ(object.type(), "core.Point");
    EXPECT_DOUBLE_EQ(object.geometry()["x"], 12.5);
    EXPECT_DOUBLE_EQ(object.geometry()["y"], -4.0);
}

// Verifies that map object edits return a new map without mutating the original.
TEST(Project, R040_003_MapObjectEditsAreImmutable) {
    const auto original = atlas::domain::Project::empty("project", "root-map");
    const auto object = atlas::domain::MapObject::create("point-1", "core.Point");
    const auto editedMap = original.rootMap().withObject(object.withName("Edited"));
    const auto removedMap = editedMap.withoutObject("point-1");

    EXPECT_EQ(original.rootMap().findObject("point-1"), nullptr);
    ASSERT_NE(editedMap.findObject("point-1"), nullptr);
    EXPECT_EQ(editedMap.findObject("point-1")->name(), "Edited");
    EXPECT_EQ(removedMap.findObject("point-1"), nullptr);
}

// Verifies that objects retain independent DisplayLayer and SpatialLevel references.
TEST(Project, R040_004_MapObjectLayerAndLevelReferencesAreIndependent) {
    const auto object = atlas::domain::MapObject::create("point-1", "core.Point")
        .withDisplayLayer("annotations")
        .withSpatialLevel("upper");

    ASSERT_TRUE(object.spatialLevelId().has_value());
    EXPECT_EQ(object.primaryDisplayLayerId(), "annotations");
    EXPECT_EQ(*object.spatialLevelId(), "upper");
}

// Verifies that visibility and locking remain explicit object state rather than color-only presentation.
TEST(Project, R040_004_MapObjectVisibilityAndLockStateRoundTrip) {
    const auto object = atlas::domain::MapObject::create("point-1", "core.Point")
        .withVisibility(false)
        .withLocked(true);
    const auto reloaded = atlas::domain::MapObject::fromJson(object.normalizedJson());

    EXPECT_FALSE(reloaded.visible());
    EXPECT_TRUE(reloaded.locked());
}

// Verifies that object normalization is deterministic regardless of insertion order.
TEST(Project, R040_003_MapObjectNormalizationSortsStableIds) {
    const auto first = atlas::domain::Project::empty("project", "root-map")
        .withRootMap(atlas::domain::Project::empty("project", "root-map").rootMap()
            .withObject(atlas::domain::MapObject::create("z", "core.Point"))
            .withObject(atlas::domain::MapObject::create("a", "core.Point")));
    const auto second = atlas::domain::Project::empty("project", "root-map")
        .withRootMap(atlas::domain::Project::empty("project", "root-map").rootMap()
            .withObject(atlas::domain::MapObject::create("a", "core.Point"))
            .withObject(atlas::domain::MapObject::create("z", "core.Point")));

    EXPECT_EQ(first.normalizedJson(), second.normalizedJson());
}

// Verifies that duplicate object IDs are rejected at the authoritative parse boundary.
TEST(Project, R040_004_DuplicateMapObjectIdsAreRejected) {
    EXPECT_THROW(atlas::domain::Project::fromJson(R"({
        "id": "project", "maps": [{"id": "root-map", "objects": [
            {"id": "same", "type": "core.Point"},
            {"id": "same", "type": "core.Circle"}
        ]}]
    })"), std::invalid_argument);
}

// Verifies that object references to unknown layers and levels are rejected.
TEST(Project, R040_004_InvalidLayerAndLevelReferencesAreRejected) {
    EXPECT_THROW(atlas::domain::Project::fromJson(R"({
        "id": "project", "maps": [{"id": "root-map",
        "objects": [{"id": "point", "type": "core.Point", "primaryDisplayLayerId": "missing"}] }]
    })"), std::invalid_argument);
}
