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

// Verifies that a RoadSpline is a first-class record with explicit Map ownership.
TEST(Project, R050_001_RoadSplineUsesFirstClassRecordAndStableIdentity) {
    const auto base = atlas::domain::Project::empty("project", "root-map");
    const atlas::domain::RoadSpline road{
        "road-1", "root-map", nlohmann::json::array({{{"id", "primitive-1"}}}),
        "start-to-end", {}, nullptr, {{"name", "Main"}}, nlohmann::json::array()};
    const auto project = base.withRoadSpline(road);

    ASSERT_EQ(project.roadSplines().size(), 1);
    EXPECT_EQ(project.roadSplines().front().id, "road-1");
    EXPECT_EQ(project.roadSplines().front().mapId, "root-map");
    EXPECT_NE(project.normalizedJson().find("roadSplines"), std::string::npos);
}

// Verifies that a RoadSegment references an existing RoadSpline in the same Map.
TEST(Project, R050_001_RoadSegmentReferencesRoadSpline) {
    const auto base = atlas::domain::Project::empty("project", "root-map");
    const auto withRoad = base.withRoadSpline({
        "road-1", "root-map", nlohmann::json::array(), "start-to-end", {"segment-1"}, nullptr,
        nlohmann::json::object(), nlohmann::json::array()});
    const auto project = withRoad.withRoadSegment({
        "segment-1", "root-map", "road-1", "anchor-start", "anchor-end",
        {{"kind", "uniform-placeholder"}}, nlohmann::json::object()});

    ASSERT_EQ(project.roadSegments().size(), 1);
    EXPECT_EQ(project.roadSegments().front().roadSplineId, "road-1");
    ASSERT_EQ(project.roadSplines().front().segmentIds, (std::vector<std::string>{"segment-1"}));
    EXPECT_EQ(project.roadSegments().front().startAnchorId, "anchor-start");
    EXPECT_EQ(project.roadSegments().front().endAnchorId, "anchor-end");
    EXPECT_NE(project.normalizedJson().find("segment-1"), std::string::npos);
}

// Verifies that road IDs and ownership cannot be duplicated or moved across Maps.
TEST(Project, R050_001_RoadIdentityAndOwnershipAreValidated) {
    const auto base = atlas::domain::Project::empty("project", "root-map");
    const atlas::domain::RoadSpline road{
        "road-1", "root-map", nlohmann::json::array(), "start-to-end", {}, nullptr,
        nlohmann::json::object(), nlohmann::json::array()};
    const auto withRoad = base.withRoadSpline(road);

    EXPECT_THROW(withRoad.withRoadSpline(road), std::invalid_argument);
    EXPECT_THROW(base.withRoadSpline({
        "road-2", "other-map", nlohmann::json::array(), "start-to-end", {}, nullptr,
        nlohmann::json::object(), nlohmann::json::array()}), std::invalid_argument);
    EXPECT_THROW(withRoad.withRoadSegment({
        "segment-1", "root-map", "missing-road", "start", "end",
        nlohmann::json::object(), nlohmann::json::object()}), std::invalid_argument);
}

// Verifies that road record normalization is independent of insertion order.
TEST(Project, R050_001_RoadNormalizationIsDeterministic) {
    const auto base = atlas::domain::Project::empty("project", "root-map");
    const auto first = base.withRoadSpline({
        "road-z", "root-map", nlohmann::json::array(), "start-to-end", {}, nullptr,
        nlohmann::json::object(), nlohmann::json::array()}).withRoadSpline({
        "road-a", "root-map", nlohmann::json::array(), "start-to-end", {}, nullptr,
        nlohmann::json::object(), nlohmann::json::array()});
    const auto second = base.withRoadSpline({
        "road-a", "root-map", nlohmann::json::array(), "start-to-end", {}, nullptr,
        nlohmann::json::object(), nlohmann::json::array()}).withRoadSpline({
        "road-z", "root-map", nlohmann::json::array(), "start-to-end", {}, nullptr,
        nlohmann::json::object(), nlohmann::json::array()});

    EXPECT_EQ(first.normalizedJson(), second.normalizedJson());
}

// Verifies that first-class road records survive normalized source round trips.
TEST(Project, R050_001_RoadRecordsRoundTripThroughNormalizedSource) {
    const auto base = atlas::domain::Project::empty("project", "root-map");
    const auto project = base.withRoadSpline({
        "road-1", "root-map", nlohmann::json::array({{{"id", "primitive-1"}}}),
        "start-to-end", {"segment-1"}, nullptr, {{"style", "default"}}, nlohmann::json::array()})
        .withRoadSegment({"segment-1", "root-map", "road-1", "a", "b",
            {{"kind", "uniform-placeholder"}}, nlohmann::json::object()});

    const auto reloaded = atlas::domain::Project::fromJson(project.normalizedJson());

    EXPECT_EQ(reloaded.normalizedJson(), project.normalizedJson());
    ASSERT_EQ(reloaded.roadSplines().size(), 1);
    ASSERT_EQ(reloaded.roadSegments().size(), 1);
}

// Verifies segment style, schema, spatial, attachment, and metadata values survive normalized source round trips.
TEST(Project, R050_004_RoadSegmentEquivalenceFieldsRoundTrip) {
    const auto base = atlas::domain::Project::empty("project", "root-map");
    const auto project = base.withRoadSpline({
        "road-1", "root-map", nlohmann::json::array(), "start-to-end", {"segment-1"}, nullptr,
        nlohmann::json::object(), nlohmann::json::array()}).withRoadSegment({
        "segment-1", "root-map", "road-1", "start", "end",
        {{"width", 8.0}}, {{"origin", "test"}}, {{"style", "bridge"}},
        {{"custom", 1}}, {"upper-level"}, {"attachment-1"}, {{"note", "kept"}},
        nlohmann::json::array({{{"id", "fixture-attachment"}, {"stationAnchorId", "anchor-2"}}})});

    const auto reloaded = atlas::domain::Project::fromJson(project.normalizedJson());
    ASSERT_EQ(reloaded.roadSegments().size(), 1);
    const auto& segment = reloaded.roadSegments().front();
    EXPECT_EQ(segment.crossSectionState["width"], 8.0);
    EXPECT_EQ(segment.styleOverrides["style"], "bridge");
    EXPECT_EQ(segment.schemaProperties["custom"], 1);
    EXPECT_EQ(segment.spatialReferences, (nlohmann::json{"upper-level"}));
    EXPECT_EQ(segment.boundaryAttachments, (nlohmann::json{"attachment-1"}));
    EXPECT_EQ(segment.metadata["note"], "kept");
    EXPECT_EQ(segment.lineage["origin"], "test");
    EXPECT_EQ(segment.stationAttachments,
        nlohmann::json::array({{{"id", "fixture-attachment"}, {"stationAnchorId", "anchor-2"}}}));
}

// Verifies project geometry policy defaults, validates, and round-trips independently of application defaults.
TEST(Project, R050_005_GeometryPolicyPersistsMiterLimitRatio) {
    const auto project = atlas::domain::Project::empty("project", "root-map");
    EXPECT_DOUBLE_EQ(project.geometryPolicy()["miterLimitRatio"], 4.0);

    auto legacySource = nlohmann::json::parse(project.normalizedJson());
    legacySource.erase("extensions");
    const auto legacyProject = atlas::domain::Project::fromJson(legacySource.dump());
    EXPECT_DOUBLE_EQ(legacyProject.geometryPolicy()["miterLimitRatio"], 4.0);

    const auto customized = project.withGeometryPolicy({{"miterLimitRatio", 6.0}});
    const auto reloaded = atlas::domain::Project::fromJson(customized.normalizedJson());
    EXPECT_DOUBLE_EQ(reloaded.geometryPolicy()["miterLimitRatio"], 6.0);
    EXPECT_EQ(reloaded.normalizedJson(), customized.normalizedJson());
    EXPECT_THROW(project.withGeometryPolicy({{"miterLimitRatio", 0.5}}), std::invalid_argument);
    EXPECT_THROW(project.withGeometryPolicy({{"miterLimitRatio", "wide"}}), std::invalid_argument);
}

// Verifies unknown RoadSpline and RoadSegment record fields survive normalized-source round trips.
TEST(Project, R050_006_RoadRecordUnknownFieldsRoundTrip) {
    auto road = atlas::domain::RoadSpline{
        "road-1", "root-map", nlohmann::json::array({{{"id", "line"}}}), "start-to-end",
        {"segment-1"}, nullptr, nlohmann::json::object(), nlohmann::json::array()};
    road.unknownFields = {{"futureRoadField", {{"mode", "retained"}}}};
    auto segment = atlas::domain::RoadSegment{
        "segment-1", "root-map", "road-1", "start", "end", {{"totalWidthMeters", 8.0}}};
    segment.unknownFields = {{"futureSegmentField", {{"revision", 3}}}};
    const auto project = atlas::domain::Project::empty("project", "root-map")
        .withRoadSpline(road).withRoadSegment(segment);

    const auto reloaded = atlas::domain::Project::fromJson(project.normalizedJson());
    EXPECT_EQ(reloaded.roadSplines().front().normalizedJson()["futureRoadField"]["mode"], "retained");
    EXPECT_EQ(reloaded.roadSegments().front().normalizedJson()["futureSegmentField"]["revision"], 3);
    EXPECT_EQ(reloaded.normalizedJson(), project.normalizedJson());
}

// Verifies schema-1 serialization omits road records and schema-2 source requires road-capable persistence.
TEST(Project, R050_006_ProjectSchemaGenerationSerializationIsExplicit) {
    const auto empty = atlas::domain::Project::empty("project", "root-map");
    const auto generationOne = nlohmann::json::parse(empty.normalizedJson(1));
    EXPECT_EQ(generationOne["schemaVersion"], 1);
    EXPECT_FALSE(generationOne.contains("roadSplines"));
    EXPECT_FALSE(generationOne.contains("roadSegments"));

    auto road = atlas::domain::RoadSpline{
        "road-1", "root-map", nlohmann::json::array({{{"id", "line"}}}), "start-to-end",
        {}, nullptr, nlohmann::json::object(), nlohmann::json::array()};
    const auto roadProject = empty.withRoadSpline(road);
    const auto generationTwo = nlohmann::json::parse(roadProject.normalizedJson(2));
    EXPECT_EQ(generationTwo["schemaVersion"], 2);
    EXPECT_TRUE(generationTwo.contains("roadSplines"));
    EXPECT_THROW(roadProject.normalizedJson(1), std::invalid_argument);
}
