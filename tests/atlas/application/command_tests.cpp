#include "atlas/application/command.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <functional>
#include <random>

// Application tests cover transaction isolation, history, dependencies,
// diagnostics, repairs, destructive impacts, rebuild failures, and cache results.
namespace {

class InvalidPreconditionCommand final : public atlas::application::Command {
public:
    const char* name() const noexcept override { return "invalid-precondition"; }
    std::optional<std::uint64_t> expectedRevision() const noexcept override { return 0; }
    std::string coalesceKey() const override { return {}; }
    std::vector<atlas::application::Diagnostic> validate(
        const atlas::application::Revision& current) const override {
        return {atlas::application::Diagnostic{
            "VAL-CORE-002", atlas::application::Severity::error, {current.project.id()},
            "Required reference is unresolved.", current.number, {"repair-project"}}};
    }
    atlas::application::Revision apply(const atlas::application::Revision& current) const override {
        return current;
    }
};

} // namespace

// Verifies that preview and cancellation leave the live revision unchanged.
TEST(CommandProcessor, PreviewDoesNotMutateLiveRevision) {
    const auto first = atlas::domain::Project::empty("first", "root-map");
    const auto second = atlas::domain::Project::empty("second", "root-map");
    atlas::application::CommandProcessor processor({0, first, {}});

    const auto preview = processor.preview(
        atlas::application::ReplaceProjectCommand(second, 0));

    EXPECT_EQ(processor.current().project.id(), "first");
    EXPECT_EQ(preview.candidate.project.id(), "second");
    EXPECT_EQ(preview.candidate.number, 1);
    processor.cancel(preview);
    EXPECT_EQ(processor.current().project.id(), "first");
}

// Verifies that commit, undo, and redo restore source state and selection.
TEST(CommandProcessor, CommitUndoAndRedoRestoreAuthoritativeState) {
    atlas::application::CommandProcessor processor({
        0, atlas::domain::Project::empty("first", "root-map"), {{"selected", "first"}}});

    processor.commit(atlas::application::ReplaceProjectCommand(
        atlas::domain::Project::empty("second", "root-map"), 0));
    EXPECT_EQ(processor.current().project.id(), "second");
    EXPECT_TRUE(processor.undo());
    EXPECT_EQ(processor.current().project.id(), "first");
    EXPECT_TRUE(processor.redo());
    EXPECT_EQ(processor.current().project.id(), "second");
    EXPECT_EQ(processor.current().selection["selected"], "first");
}

// Verifies that a command based on an older revision cannot commit.
TEST(CommandProcessor, StaleRevisionIsRejected) {
    atlas::application::CommandProcessor processor({
        4, atlas::domain::Project::empty("first", "root-map"), {}});

    EXPECT_THROW(
        processor.commit(atlas::application::ReplaceProjectCommand(
            atlas::domain::Project::empty("second", "root-map"), 3)),
        std::runtime_error);
    EXPECT_EQ(processor.current().project.id(), "first");
}

// Verifies that diagnostics retain stable rule identifiers and revisions.
TEST(CommandProcessor, DiagnosticCarriesStableRuleAndRevision) {
    atlas::application::CommandProcessor processor({
        7, atlas::domain::Project::empty("project", "root-map"), {}});

    processor.addDiagnostic({"VAL-CORE-001", atlas::application::Severity::error,
        {"project"}, "Invalid project state", 7, {"repair-project"}});

    ASSERT_EQ(processor.diagnostics().size(), 1);
    EXPECT_EQ(processor.diagnostics().front().ruleId, "VAL-CORE-001");
    EXPECT_EQ(processor.diagnostics().front().revision, 7);
    EXPECT_EQ(processor.diagnostics().front().repairIds.front(), "repair-project");
}

// Verifies that consecutive project edits coalesce into one undo entry.
TEST(CommandProcessor, CoalescesContinuousProjectCommands) {
    atlas::application::CommandProcessor processor({
        0, atlas::domain::Project::empty("first", "root-map"), {}});

    processor.commit(atlas::application::ReplaceProjectCommand(
        atlas::domain::Project::empty("second", "root-map"), 0));
    processor.commit(atlas::application::ReplaceProjectCommand(
        atlas::domain::Project::empty("third", "root-map"), 1));

    ASSERT_TRUE(processor.undo());
    EXPECT_EQ(processor.current().project.id(), "first");
    EXPECT_FALSE(processor.canUndo());
}

// Verifies that results from obsolete source revisions are rejected.
TEST(CommandProcessor, DependencyResultsRejectStaleRevisions) {
    atlas::application::CommandProcessor processor({
        2, atlas::domain::Project::empty("project", "root-map"), {}});
    processor.dependencies().dependentsOf("source");

    const auto stale = atlas::application::VersionedResult{"cache", 1, "old"};
    const auto current = atlas::application::VersionedResult{"cache", 2, "new"};

    EXPECT_FALSE(processor.acceptResult(stale));
    EXPECT_TRUE(processor.acceptResult(current));
    ASSERT_NE(processor.cachedResult("cache"), nullptr);
    EXPECT_EQ(*processor.cachedResult("cache"), "new");
}

// Verifies that dependency lookup returns each derived product once.
TEST(CommandProcessor, DependencyGraphReturnsUniqueDependents) {
    atlas::application::DependencyGraph graph;
    graph.addDependency("source", "geometry");
    graph.addDependency("source", "geometry");
    graph.addDependency("source", "diagnostics");

    const auto dependents = graph.dependentsOf("source");
    ASSERT_EQ(dependents.size(), 2);
    EXPECT_EQ(dependents[0], "geometry");
    EXPECT_EQ(dependents[1], "diagnostics");
}

// Verifies that range-scoped invalidation excludes unrelated dependents.
TEST(CommandProcessor, DependencyInvalidationOnlyTouchesAffectedRanges) {
    atlas::application::DependencyGraph graph;
    graph.addDependency("road", "near", atlas::application::StationRange{0.0, 10.0});
    graph.addDependency("road", "far", atlas::application::StationRange{20.0, 30.0});
    graph.addDependency("road", "global");

    const auto affected = graph.dependentsFor(atlas::application::Invalidation{
        "road", "geometry", 4, atlas::application::StationRange{8.0, 12.0}, {}});

    ASSERT_EQ(affected.size(), 2);
    EXPECT_EQ(affected[0], "near");
    EXPECT_EQ(affected[1], "global");
}

// Verifies that overlapping ranges coalesce without merging unrelated ranges.
TEST(CommandProcessor, OverlappingInvalidationsCoalesceWithoutMergingUnrelatedRanges) {
    const auto coalesced = atlas::application::coalesceInvalidations({
        {"road", "geometry", 5, atlas::application::StationRange{0.0, 10.0}, {}},
        {"road", "geometry", 5, atlas::application::StationRange{8.0, 16.0}, {}},
        {"road", "geometry", 5, atlas::application::StationRange{30.0, 40.0}, {}},
        {"other", "geometry", 5, atlas::application::StationRange{0.0, 40.0}, {}}});

    ASSERT_EQ(coalesced.size(), 3);
    ASSERT_TRUE(coalesced[0].stationRange.has_value());
    EXPECT_DOUBLE_EQ(coalesced[0].stationRange->start, 0.0);
    EXPECT_DOUBLE_EQ(coalesced[0].stationRange->end, 16.0);
}

// Verifies that destructive changes require an explicit resolution.
TEST(CommandProcessor, DestructiveImpactRequiresExplicitResolution) {
    const auto cancelled = atlas::application::resolveDestructiveImpact(
        {"source", "dependent"}, atlas::application::ImpactResolution::cancel);
    const auto accepted = atlas::application::resolveDestructiveImpact(
        {"source", "dependent"}, atlas::application::ImpactResolution::retarget);

    EXPECT_FALSE(cancelled.resolved);
    EXPECT_TRUE(accepted.resolved);
    EXPECT_EQ(accepted.affectedIds.size(), 2);
}

// Verifies that reverse references distinguish required and optional dependents.
TEST(CommandProcessor, ReverseReferencesExposeRequiredAndOptionalDependents) {
    atlas::application::ReverseReferenceIndex index;
    index.addReference("source", "required-dependent", atlas::application::ReferenceStrength::required);
    index.addReference("source", "optional-dependent", atlas::application::ReferenceStrength::optional);

    EXPECT_EQ(index.dependentsOf("source"),
        (std::vector<std::string>{"required-dependent", "optional-dependent"}));
    EXPECT_EQ(index.requiredDependentsOf("source"),
        (std::vector<std::string>{"required-dependent"}));
}

// Verifies that destructive impact resolution uses reverse-reference data.
TEST(CommandProcessor, DestructiveImpactUsesReverseReferences) {
    atlas::application::ReverseReferenceIndex index;
    index.addReference("source", "dependent", atlas::application::ReferenceStrength::required);

    const auto cancelled = atlas::application::resolveDestructiveImpact(
        index, "source", atlas::application::ImpactResolution::cancel);
    const auto deleted = atlas::application::resolveDestructiveImpact(
        index, "source", atlas::application::ImpactResolution::deleteDependents);

    EXPECT_FALSE(cancelled.resolved);
    EXPECT_TRUE(deleted.resolved);
    EXPECT_EQ(deleted.affectedIds, (std::vector<std::string>{"dependent"}));
}

// Verifies that repairs use the normal preview and commit path.
TEST(CommandProcessor, RepairRunsThroughPreviewAndCommitPath) {
    atlas::application::CommandProcessor processor({
        0, atlas::domain::Project::empty("broken", "root-map"), {}});

    const auto preview = processor.preview(
        atlas::application::RepairProjectCommand(
            atlas::domain::Project::empty("repaired", "root-map"), 0));
    EXPECT_EQ(processor.current().project.id(), "broken");
    EXPECT_EQ(preview.candidate.project.id(), "repaired");

    processor.commit(atlas::application::RepairProjectCommand(
        atlas::domain::Project::empty("repaired", "root-map"), 0));
    EXPECT_EQ(processor.current().project.id(), "repaired");
}

// Verifies that repair transactions participate in undo and redo.
TEST(CommandProcessor, RepairCanBeUndoneAndRedone) {
    atlas::application::CommandProcessor processor({
        0, atlas::domain::Project::empty("broken", "root-map"), {}});

    processor.commit(atlas::application::RepairProjectCommand(
        atlas::domain::Project::empty("repaired", "root-map"), 0));
    ASSERT_TRUE(processor.undo());
    EXPECT_EQ(processor.current().project.id(), "broken");
    ASSERT_TRUE(processor.redo());
    EXPECT_EQ(processor.current().project.id(), "repaired");
}

// Verifies that structural preview errors are diagnosed and block commit.
TEST(CommandProcessor, InvalidPreviewIsDiagnosedAndCannotCommit) {
    atlas::application::CommandProcessor processor({
        0, atlas::domain::Project::empty("project", "root-map"), {}});

    const auto preview = processor.preview(InvalidPreconditionCommand{});
    ASSERT_EQ(preview.diagnostics.size(), 1);
    EXPECT_EQ(preview.diagnostics.front().severity, atlas::application::Severity::error);
    EXPECT_THROW(processor.commit(InvalidPreconditionCommand{}), std::runtime_error);
    EXPECT_EQ(processor.current().number, 0);
    EXPECT_FALSE(processor.canUndo());
    ASSERT_EQ(processor.diagnostics().size(), 1);
    EXPECT_EQ(processor.diagnostics().front().ruleId, "VAL-CORE-002");
}

// Verifies that command failure leaves revision and history unchanged.
TEST(CommandProcessor, FailedCommandLeavesRevisionAndHistoryUnchanged) {
    class FailingCommand final : public atlas::application::Command {
    public:
        const char* name() const noexcept override { return "failing"; }
        std::optional<std::uint64_t> expectedRevision() const noexcept override { return 0; }
        std::string coalesceKey() const override { return {}; }
        atlas::application::Revision apply(const atlas::application::Revision&) const override {
            throw std::runtime_error("injected command failure");
        }
    } command;

    atlas::application::CommandProcessor processor({
        0, atlas::domain::Project::empty("project", "root-map"), {}});
    EXPECT_THROW(processor.commit(command), std::runtime_error);
    EXPECT_EQ(processor.current().number, 0);
    EXPECT_EQ(processor.current().project.id(), "project");
    EXPECT_FALSE(processor.canUndo());
}

// Verifies that a deterministic command sequence can be fully undone.
TEST(CommandProcessor, DeterministicCommandSequenceCanBeFullyUndone) {
    atlas::application::CommandProcessor processor({
        0, atlas::domain::Project::empty("0", "root-map"), {}});

    for (int index = 1; index <= 20; ++index) {
        processor.commit(atlas::application::RepairProjectCommand(
            atlas::domain::Project::empty(std::to_string(index), "root-map"),
            processor.current().number));
    }
    for (int index = 0; index < 20; ++index) {
        ASSERT_TRUE(processor.undo());
    }

    EXPECT_EQ(processor.current().project.id(), "0");
    EXPECT_FALSE(processor.canUndo());
}

// Verifies that rebuild failure preserves source and records a repair diagnostic.
TEST(CommandProcessor, RebuildFailurePreservesSourceAndAddsRepairDiagnostic) {
    atlas::application::CommandProcessor processor({
        3, atlas::domain::Project::empty("project", "root-map"), {}});
    const auto sourceBefore = processor.current().normalizedSource();

    processor.recordRebuildFailure("project", "DEPS-CORE-004", "Geometry rebuild failed", {"repair-project"});

    EXPECT_EQ(processor.current().normalizedSource(), sourceBefore);
    ASSERT_EQ(processor.diagnostics().size(), 1);
    const auto& diagnostic = processor.diagnostics().front();
    EXPECT_EQ(diagnostic.severity, atlas::application::Severity::error);
    EXPECT_EQ(diagnostic.affectedIds, (std::vector<std::string>{"project"}));
    EXPECT_EQ(diagnostic.revision, 3);
    EXPECT_EQ(diagnostic.repairIds, (std::vector<std::string>{"repair-project"}));
}

// Verifies that rebuild failure does not replace an existing good cache.
TEST(CommandProcessor, RebuildFailureDoesNotReplaceAnExistingGoodCache) {
    atlas::application::CommandProcessor processor({
        3, atlas::domain::Project::empty("project", "root-map"), {}});
    ASSERT_TRUE(processor.acceptResult({"geometry", 3, "good", {"project"}}));

    processor.recordRebuildFailure("project", "DEPS-CORE-004", "Geometry rebuild failed");

    ASSERT_NE(processor.cachedResult("geometry"), nullptr);
    EXPECT_EQ(*processor.cachedResult("geometry"), "good");
}

// Verifies that cache results identify their complete source dependencies.
TEST(CommandProcessor, CacheResultsRequireTheCompleteDependencySet) {
    atlas::application::CommandProcessor processor({
        2, atlas::domain::Project::empty("project", "root-map"), {}});
    processor.registerCacheDependencies("geometry", {"road", "map"});

    EXPECT_FALSE(processor.acceptResult({"geometry", 2, "wrong", {"road"}}));
    EXPECT_TRUE(processor.acceptResult({"geometry", 2, "right", {"map", "road"}}));
    ASSERT_NE(processor.cachedResult("geometry"), nullptr);
    EXPECT_EQ(*processor.cachedResult("geometry"), "right");
}

// Verifies that duplicate current results are accepted deterministically.
TEST(CommandProcessor, DuplicateCurrentResultsAreDeterministic) {
    atlas::application::CommandProcessor processor({
        2, atlas::domain::Project::empty("project", "root-map"), {}});
    const atlas::application::VersionedResult result{"geometry", 2, "same", {"project"}};

    EXPECT_TRUE(processor.acceptResult(result));
    EXPECT_TRUE(processor.acceptResult(result));
    EXPECT_EQ(*processor.cachedResult("geometry"), "same");
}

// Verifies that fixed-seed command sequences restore source and selection.
TEST(CommandProcessor, FixedSeedCommandSequenceFullyRestoresSourceAndSelection) {
    const atlas::application::Revision initial{
        0, atlas::domain::Project::empty("0", "root-map"), {{"selected", "0"}}};
    atlas::application::CommandProcessor processor(initial);
    std::mt19937 generator(0xA71A5u);
    std::uniform_int_distribution<int> projectId(1, 1000);

    for (int index = 0; index < 50; ++index) {
        const auto id = std::to_string(projectId(generator));
        processor.commit(atlas::application::RepairProjectCommand(
            atlas::domain::Project::empty(id, "root-map"), processor.current().number));
    }
    while (processor.canUndo()) ASSERT_TRUE(processor.undo());

    EXPECT_EQ(processor.current().normalizedSource(), initial.normalizedSource());
    EXPECT_EQ(processor.current().selection, initial.selection);
    EXPECT_EQ(processor.current().number, initial.number);
}

// Verifies that generic object creation commits through the revision processor.
TEST(CommandProcessor, CreateMapObjectRunsThroughTransaction) {
    atlas::application::CommandProcessor processor({
        0, atlas::domain::Project::empty("project", "root-map"), {}});
    const auto object = atlas::domain::MapObject::create("point-1", "core.Point");

    processor.commit(atlas::application::CreateMapObjectCommand(object, 0));

    ASSERT_NE(processor.current().project.rootMap().findObject("point-1"), nullptr);
    EXPECT_EQ(processor.current().number, 1);
}

// Verifies that generic object editing preserves identity while changing geometry.
TEST(CommandProcessor, EditMapObjectPreservesIdentity) {
    const auto object = atlas::domain::MapObject::create("point-1", "core.Point", {{"x", 1.0}});
    const auto base = atlas::domain::Project::empty("project", "root-map")
        .withRootMap(atlas::domain::Project::empty("project", "root-map").rootMap().withObject(object));
    atlas::application::CommandProcessor processor({0, base, {}});

    processor.commit(atlas::application::EditMapObjectCommand(
        object.withGeometry({{"x", 2.0}}), 0));

    const auto* edited = processor.current().project.rootMap().findObject("point-1");
    ASSERT_NE(edited, nullptr);
    EXPECT_EQ(edited->id(), "point-1");
    EXPECT_DOUBLE_EQ(edited->geometry()["x"], 2.0);
}

// Verifies that generic object deletion can be undone and redone.
TEST(CommandProcessor, DeleteMapObjectSupportsUndoAndRedo) {
    const auto object = atlas::domain::MapObject::create("point-1", "core.Point");
    const auto base = atlas::domain::Project::empty("project", "root-map")
        .withRootMap(atlas::domain::Project::empty("project", "root-map").rootMap().withObject(object));
    atlas::application::CommandProcessor processor({0, base, {}});

    processor.commit(atlas::application::DeleteMapObjectCommand("point-1", 0));
    EXPECT_EQ(processor.current().project.rootMap().findObject("point-1"), nullptr);
    ASSERT_TRUE(processor.undo());
    ASSERT_NE(processor.current().project.rootMap().findObject("point-1"), nullptr);
    ASSERT_TRUE(processor.redo());
    EXPECT_EQ(processor.current().project.rootMap().findObject("point-1"), nullptr);
}

// Verifies that creating an existing object fails without changing revision or history.
TEST(CommandProcessor, CreateExistingMapObjectFailsAtomically) {
    const auto object = atlas::domain::MapObject::create("point-1", "core.Point");
    const auto base = atlas::domain::Project::empty("project", "root-map")
        .withRootMap(atlas::domain::Project::empty("project", "root-map").rootMap().withObject(object));
    atlas::application::CommandProcessor processor({0, base, {}});

    EXPECT_THROW(processor.commit(atlas::application::CreateMapObjectCommand(object, 0)), std::invalid_argument);
    EXPECT_EQ(processor.current().number, 0);
    EXPECT_FALSE(processor.canUndo());
}

// Verifies that editing a missing object fails without mutating source state.
TEST(CommandProcessor, EditMissingMapObjectFailsAtomically) {
    atlas::application::CommandProcessor processor({
        0, atlas::domain::Project::empty("project", "root-map"), {}});

    EXPECT_THROW(processor.commit(atlas::application::EditMapObjectCommand(
        atlas::domain::MapObject::create("missing", "core.Point"), 0)), std::invalid_argument);
    EXPECT_EQ(processor.current().number, 0);
    EXPECT_FALSE(processor.canUndo());
}

// Verifies that deleting a missing object fails without mutating source state.
TEST(CommandProcessor, DeleteMissingMapObjectFailsAtomically) {
    atlas::application::CommandProcessor processor({
        0, atlas::domain::Project::empty("project", "root-map"), {}});

    EXPECT_THROW(processor.commit(atlas::application::DeleteMapObjectCommand("missing", 0)), std::invalid_argument);
    EXPECT_EQ(processor.current().number, 0);
    EXPECT_FALSE(processor.canUndo());
}

// Verifies that every v0.4 generic object family can enter the command path.
TEST(CommandProcessor, GenericObjectFamiliesCanBeCreatedThroughCommands) {
    atlas::application::CommandProcessor processor({
        0, atlas::domain::Project::empty("project", "root-map"), {}});
    const std::vector<std::string> types{
        "core.Point", "core.Polyline", "core.Polygon", "core.Rectangle",
        "core.Circle", "core.Text", "core.ReferenceImage", "core.Guide"};

    for (std::size_t index = 0; index < types.size(); ++index) {
        processor.commit(atlas::application::CreateMapObjectCommand(
            atlas::domain::MapObject::create("object-" + std::to_string(index), types[index]),
            processor.current().number));
    }

    EXPECT_EQ(processor.current().project.rootMap().objects.size(), types.size());
    EXPECT_EQ(processor.current().number, types.size());
}

// Verifies that generic object undo restores the original selection context.
TEST(CommandProcessor, GenericObjectUndoRestoresSelectionContext) {
    atlas::application::CommandProcessor processor({
        0, atlas::domain::Project::empty("project", "root-map"), {{"primaryId", "point-1"}}});
    const auto object = atlas::domain::MapObject::create("point-1", "core.Point");

    processor.commit(atlas::application::CreateMapObjectCommand(object, 0));
    ASSERT_TRUE(processor.undo());

    EXPECT_EQ(processor.current().selection["primaryId"], "point-1");
}

namespace {

atlas::domain::RoadSpline commandRoad() {
    return {
        "road-1", "root-map",
        nlohmann::json::array({nlohmann::json{
            {"id", "line"},
            {"kind", "line"},
            {"startControlPointId", "a"},
            {"endControlPointId", "b"},
            {"start", {{"x", 0.0}, {"y", 0.0}}},
            {"end", {{"x", 10.0}, {"y", 0.0}}}}}),
        "start-to-end", {}, nullptr, nlohmann::json::object(), nlohmann::json::array(),
        {{"anchor-1", 8.0, atlas::domain::AnchorAffinity::geometryLocked,
            {0.8, "line", 0.8, {8.0, 0.0}}}}};
}

atlas::application::CommandProcessor processorWithRoad() {
    atlas::application::CommandProcessor processor({
        0, atlas::domain::Project::empty("project", "root-map"), {}});
    processor.commit(atlas::application::CreateRoadSplineCommand(commandRoad(), 0));
    return processor;
}

} // namespace

// Verifies that RoadSpline creation uses the normal revision and undo boundary.
TEST(CommandProcessor, CreateRoadSplineCommitsAsAuthoritativeSource) {
    auto processor = processorWithRoad();

    ASSERT_EQ(processor.current().project.roadSplines().size(), 1);
    EXPECT_EQ(processor.current().project.roadSplines().front().id, "road-1");
    ASSERT_EQ(processor.current().project.roadSegments().size(), 1);
    EXPECT_DOUBLE_EQ(processor.current().project.roadSegments().front().crossSectionState["totalWidthMeters"], 8.0);
    EXPECT_EQ(processor.current().project.roadSegments().front().crossSectionState["joinStyle"], "round");
    ASSERT_EQ(processor.lastInvalidations().size(), 2);
    EXPECT_NE(std::find_if(processor.lastInvalidations().begin(), processor.lastInvalidations().end(),
        [](const auto& invalidation) {
            return invalidation.propertyClass == "road-envelope" && invalidation.stationRange &&
                invalidation.stationRange->start == 0.0 && invalidation.stationRange->end > 0.0;
        }), processor.lastInvalidations().end());
    EXPECT_EQ(processor.current().project.roadSegments().front().startAnchorId, "road-1/anchor/start");
    EXPECT_EQ(processor.current().project.roadSegments().front().endAnchorId, "road-1/anchor/end");
    EXPECT_TRUE(processor.undo());
    EXPECT_TRUE(processor.current().project.roadSplines().empty());
    EXPECT_TRUE(processor.current().project.roadSegments().empty());
}

// Verifies the configurable creation preset stores its selected physical width rather than the future default.
TEST(CommandProcessor, RoadCreationPersistsConfiguredPlaceholderWidth) {
    atlas::application::CommandProcessor processor({
        0, atlas::domain::Project::empty("project", "root-map"), {}});
    processor.commit(atlas::application::CreateRoadSplineCommand(commandRoad(), 0, 11.5));
    EXPECT_DOUBLE_EQ(processor.current().project.roadSegments().front().crossSectionState["totalWidthMeters"], 11.5);

    EXPECT_THROW(processor.commit(atlas::application::CreateRoadSplineCommand(
        commandRoad(), processor.current().number, 0.0)), std::invalid_argument);
    EXPECT_EQ(processor.current().project.roadSegments().front().crossSectionState["totalWidthMeters"], 11.5);
}

// Verifies a valid centerline with an unbuildable offset previews an error but commits source under Model B.
TEST(CommandProcessor, InvalidDerivedEnvelopeDoesNotBlockRoadSourceCommit) {
    atlas::application::CommandProcessor processor({
        0, atlas::domain::Project::empty("project", "root-map"), {}});
    auto road = commandRoad();
    road.id = "tight-road";
    road.primitives = nlohmann::json::array({nlohmann::json{
        {"id", "tight-arc"}, {"kind", "circular-arc"},
        {"startControlPointId", "start"}, {"endControlPointId", "end"},
        {"start", {{"x", 0.0}, {"y", 0.0}}}, {"end", {{"x", 2.0}, {"y", 0.0}}},
        {"radius", 3.0}, {"side", "left"}}});
    road.stationAnchors.clear();

    const auto preview = processor.preview(atlas::application::CreateRoadSplineCommand(road, 0));
    ASSERT_EQ(preview.roadEnvelopes.size(), 1);
    EXPECT_FALSE(preview.roadEnvelopes.front().result.valid);
    EXPECT_FALSE(preview.roadEnvelopes.front().result.boundedPreview.empty());
    EXPECT_LE(preview.roadEnvelopes.front().result.boundedPreview.size(), 2048);
    ASSERT_FALSE(preview.diagnostics.empty());
    EXPECT_EQ(preview.diagnostics.front().severity, atlas::application::Severity::error);
    EXPECT_FALSE(preview.diagnostics.front().blocksCommit);

    processor.commit(atlas::application::CreateRoadSplineCommand(road, 0));
    ASSERT_EQ(processor.current().project.roadSplines().size(), 1);
    ASSERT_EQ(processor.current().project.roadSegments().size(), 1);
    ASSERT_FALSE(processor.diagnostics().empty());
    EXPECT_EQ(processor.diagnostics().front().severity, atlas::application::Severity::error);
    EXPECT_FALSE(processor.diagnostics().front().blocksCommit);
}

// Verifies repeated previews reuse derived road-envelope output without changing authoritative source.
TEST(CommandProcessor, RoadEnvelopePreviewCacheReusesUnchangedCandidate) {
    auto processor = processorWithRoad();
    auto road = processor.current().project.roadSplines().front();
    road.metadata["cacheProbe"] = true;
    const auto command = atlas::application::EditRoadSplineCommand(road, processor.current().number);
    const auto before = processor.current().normalizedSource();
    const auto coldPreview = processor.preview(command);
    const auto warmPreview = processor.preview(command);

    ASSERT_EQ(coldPreview.roadEnvelopes.size(), 1);
    ASSERT_EQ(warmPreview.roadEnvelopes.size(), 1);
    EXPECT_FALSE(coldPreview.roadEnvelopes.front().cacheHit);
    EXPECT_TRUE(warmPreview.roadEnvelopes.front().cacheHit);
    EXPECT_EQ(processor.current().normalizedSource(), before);
}

// Verifies malformed authoritative width remains a commit-blocking source error under Model B.
TEST(CommandProcessor, InvalidRoadWidthStillBlocksSourceCommit) {
    auto processor = processorWithRoad();
    const auto road = processor.current().project.roadSplines().front();
    auto segments = processor.current().project.roadSegments();
    segments.front().crossSectionState["totalWidthMeters"] = 0.0;
    const auto candidate = processor.current().project.withReplacedRoadTopology(road, segments);
    const auto before = processor.current().normalizedSource();

    EXPECT_THROW(processor.commit(atlas::application::ReplaceProjectCommand(candidate, processor.current().number)),
        std::runtime_error);
    EXPECT_EQ(processor.current().normalizedSource(), before);
    ASSERT_FALSE(processor.diagnostics().empty());
    EXPECT_TRUE(processor.diagnostics().back().blocksCommit);
}

// Verifies valid segment-owned widths that cannot form one envelope commit with a bounded Model B preview.
TEST(CommandProcessor, SegmentWidthDiscontinuityIsNonBlockingDerivedFailure) {
    auto processor = processorWithRoad();
    processor.commit(atlas::application::SplitRoadSegmentCommand(
        "road-1", "road-1/segment/initial", 4.0, processor.current().number));
    const auto road = processor.current().project.roadSplines().front();
    auto segments = processor.current().project.roadSegments();
    segments[1].crossSectionState["totalWidthMeters"] = 6.0;
    const auto candidate = processor.current().project.withReplacedRoadTopology(road, segments);
    const auto command = atlas::application::ReplaceProjectCommand(candidate, processor.current().number);
    const auto preview = processor.preview(command);

    ASSERT_EQ(preview.roadEnvelopes.size(), 1);
    EXPECT_FALSE(preview.roadEnvelopes.front().result.valid);
    EXPECT_FALSE(preview.roadEnvelopes.front().result.boundedPreview.empty());
    ASSERT_FALSE(preview.diagnostics.empty());
    EXPECT_EQ(preview.diagnostics.back().ruleId, "GEOM-CORE-003");
    EXPECT_FALSE(preview.diagnostics.back().blocksCommit);

    processor.commit(command);
    EXPECT_DOUBLE_EQ(processor.current().project.roadSegments()[1].crossSectionState["totalWidthMeters"], 6.0);
}

// Verifies changing the persisted project miter ratio regenerates the derived road envelope.
TEST(CommandProcessor, ProjectMiterPolicyChangeRebuildsRoadEnvelope) {
    auto processor = processorWithRoad();
    auto road = processor.current().project.roadSplines().front();
    road.primitives = nlohmann::json::array({
        nlohmann::json{{"id", "first"}, {"kind", "line"}, {"startControlPointId", "a"},
            {"endControlPointId", "joint"}, {"start", {{"x", 0.0}, {"y", 0.0}}},
            {"end", {{"x", 4.0}, {"y", 0.0}}}},
        nlohmann::json{{"id", "second"}, {"kind", "line"}, {"startControlPointId", "joint"},
            {"endControlPointId", "b"}, {"start", {{"x", 4.0}, {"y", 0.0}}},
            {"end", {{"x", 4.0}, {"y", 4.0}}}}});
    road.stationAnchors.clear();
    auto segment = processor.current().project.roadSegments().front();
    segment.crossSectionState["totalWidthMeters"] = 2.0;
    segment.crossSectionState["joinStyle"] = "miter";
    auto project = processor.current().project.withReplacedRoadTopology(road, {segment})
        .withGeometryPolicy({{"miterLimitRatio", 1.0}});
    const auto initialPolicyCommand = atlas::application::ReplaceProjectCommand(project, processor.current().number);
    const auto initialPreview = processor.preview(initialPolicyCommand);
    ASSERT_EQ(initialPreview.roadEnvelopes.size(), 1);
    EXPECT_FALSE(initialPreview.roadEnvelopes.front().result.valid);
    EXPECT_NE(std::find_if(initialPreview.diagnostics.begin(), initialPreview.diagnostics.end(),
        [](const auto& diagnostic) {
            return diagnostic.ruleId == "GEOM-CORE-004" && !diagnostic.blocksCommit;
        }), initialPreview.diagnostics.end());
    const auto firstHash = initialPreview.roadEnvelopes.front().result.deterministicHash;
    processor.commit(initialPolicyCommand);

    const auto policyChange = atlas::application::ReplaceProjectCommand(
        processor.current().project.withGeometryPolicy({{"miterLimitRatio", 4.0}}),
        processor.current().number);
    const auto preview = processor.preview(policyChange);
    ASSERT_EQ(preview.roadEnvelopes.size(), 1);
    EXPECT_TRUE(preview.roadEnvelopes.front().result.valid);
    ASSERT_FALSE(firstHash.empty());
    EXPECT_NE(preview.roadEnvelopes.front().result.deterministicHash, firstHash);
    EXPECT_NE(std::find_if(preview.invalidations.begin(), preview.invalidations.end(), [](const auto& invalidation) {
        return invalidation.propertyClass == "road-envelope" && invalidation.stationRange.has_value();
    }), preview.invalidations.end());
}

// Verifies that a RoadSpline source edit preserves identity while replacing source data.
TEST(CommandProcessor, EditRoadSplinePreservesStableIdentity) {
    auto processor = processorWithRoad();
    auto edited = processor.current().project.roadSplines().front();
    edited.metadata["edited"] = true;

    processor.commit(atlas::application::EditRoadSplineCommand(edited, processor.current().number));

    ASSERT_EQ(processor.current().project.roadSplines().size(), 1);
    EXPECT_EQ(processor.current().project.roadSplines().front().id, "road-1");
    EXPECT_TRUE(processor.current().project.roadSplines().front().metadata["edited"]);
}

// Verifies geometry edits cannot commit when a retained anchor loses its primitive without an unambiguous remap.
TEST(CommandProcessor, EditRoadSplineBlocksUnresolvedAnchorRemap) {
    auto processor = processorWithRoad();
    auto edited = processor.current().project.roadSplines().front();
    edited.primitives[0]["id"] = "replacement-line";
    edited.primitives[0]["startControlPointId"] = "replacement-a";
    edited.primitives[0]["endControlPointId"] = "replacement-b";
    const auto before = processor.current().normalizedSource();

    const auto preview = processor.preview(
        atlas::application::EditRoadSplineCommand(edited, processor.current().number));
    ASSERT_FALSE(preview.diagnostics.empty());
    EXPECT_EQ(preview.diagnostics.front().ruleId, "STAT-CORE-003");
    EXPECT_TRUE(preview.diagnostics.front().blocksCommit);
    EXPECT_THROW(processor.commit(
        atlas::application::EditRoadSplineCommand(edited, processor.current().number)), std::runtime_error);
    EXPECT_EQ(processor.current().normalizedSource(), before);
}

// Verifies a user-selected station resolves a retained ambiguous anchor and refreshes its signature deterministically.
TEST(CommandProcessor, EditRoadSplineAcceptsExplicitAnchorStationResolution) {
    auto processor = processorWithRoad();
    auto edited = processor.current().project.roadSplines().front();
    edited.primitives[0]["id"] = "replacement-line";
    edited.primitives[0]["startControlPointId"] = "replacement-a";
    edited.primitives[0]["endControlPointId"] = "replacement-b";
    const auto before = processor.current().normalizedSource();
    const auto unresolvedPreview = processor.preview(
        atlas::application::EditRoadSplineCommand(edited, processor.current().number));
    ASSERT_FALSE(unresolvedPreview.diagnostics.empty());
    ASSERT_FALSE(unresolvedPreview.diagnostics.front().affectedIds.empty());
    const auto anchorId = unresolvedPreview.diagnostics.front().affectedIds.front();
    const std::map<std::string, double> resolutions{{anchorId, 2.5}};

    processor.commit(atlas::application::EditRoadSplineCommand(
        edited, processor.current().number, false, resolutions));
    const auto& anchors = processor.current().project.roadSplines().front().stationAnchors;
    const auto& anchor = *std::find_if(anchors.begin(), anchors.end(), [&](const auto& candidate) {
        return candidate.id == anchorId;
    });
    EXPECT_DOUBLE_EQ(anchor.resolvedStation, 2.5);
    EXPECT_EQ(anchor.remapSignature.primitiveId, "replacement-line");
    EXPECT_DOUBLE_EQ(anchor.remapSignature.primitiveT, 0.25);
    EXPECT_DOUBLE_EQ(anchor.remapSignature.worldPosition.x, 2.5);
    EXPECT_TRUE(processor.undo());
    EXPECT_EQ(processor.current().normalizedSource(), before);
}

// Verifies that extend and shorten commands update the final source endpoint through transactions.
TEST(CommandProcessor, ExtendAndShortenRoadSplineUseRevisionGuards) {
    auto processor = processorWithRoad();
    processor.commit(atlas::application::ExtendRoadSplineCommand(
        "road-1", {20.0, 0.0}, processor.current().number));
    EXPECT_DOUBLE_EQ(processor.current().project.roadSplines().front().primitives.back()["end"]["x"], 20.0);

    processor.commit(atlas::application::ShortenRoadSplineCommand(
        "road-1", {5.0, 0.0}, processor.current().number,
        atlas::application::ShortenResolution::moveDependents));
    EXPECT_DOUBLE_EQ(processor.current().project.roadSplines().front().primitives.back()["end"]["x"], 5.0);
}

// Verifies that reversing twice restores normalized source data and stable primitive identity.
TEST(CommandProcessor, ReverseRoadSplineTwiceRestoresNormalizedSource) {
    auto processor = processorWithRoad();
    const auto original = processor.current().normalizedSource();

    processor.commit(atlas::application::ReverseRoadSplineCommand("road-1", processor.current().number));
    processor.commit(atlas::application::ReverseRoadSplineCommand("road-1", processor.current().number));

    EXPECT_EQ(processor.current().normalizedSource(), original);
}

// Verifies a single reversal swaps cross-section sides and travel directions without replacing stable element IDs.
TEST(CommandProcessor, ReverseRoadSplineRemapsCrossSectionSidesAndDirections) {
    auto processor = processorWithRoad();
    const auto road = processor.current().project.roadSplines().front();
    auto segment = processor.current().project.roadSegments().front();
    segment.crossSectionState = nlohmann::json::object({
        {"kind", "uniform-placeholder"}, {"totalWidthMeters", 8.0}, {"joinStyle", "round"},
        {"customState", {{"preserved", true}}},
        {"elements", nlohmann::json::array({
            {{"id", "lane-left"}, {"group", "left"}, {"orderKey", "left-1"},
                {"travelDirection", "forward"}},
            {{"id", "center-left"}, {"group", "center"}, {"orderKey", "center-a"},
                {"travelDirection", "none"}},
            {{"id", "center-right"}, {"group", "center"}, {"orderKey", "center-b"},
                {"travelDirection", "none"}},
            {{"id", "lane-right"}, {"group", "right"}, {"orderKey", "right-1"},
                {"travelDirection", "reverse"}}})}});
    segment.stationAttachments = nlohmann::json::array({
        {{"id", "attachment-left"}, {"stationAnchorId", "road-1/anchor/start"},
            {"crossSectionElementId", "lane-left"}}});
    processor.commit(atlas::application::ReplaceProjectCommand(
        processor.current().project.withReplacedRoadTopology(road, {segment}), processor.current().number));
    const auto beforeReverse = processor.current().normalizedSource();

    processor.commit(atlas::application::ReverseRoadSplineCommand("road-1", processor.current().number));

    const auto& reversedSegment = processor.current().project.roadSegments().front();
    const auto& elements = reversedSegment.crossSectionState["elements"];
    EXPECT_EQ(reversedSegment.crossSectionState["customState"]["preserved"], true);
    const auto laneLeft = std::find_if(elements.begin(), elements.end(), [](const auto& element) {
        return element["id"] == "lane-left";
    });
    const auto laneRight = std::find_if(elements.begin(), elements.end(), [](const auto& element) {
        return element["id"] == "lane-right";
    });
    const auto centerLeft = std::find_if(elements.begin(), elements.end(), [](const auto& element) {
        return element["id"] == "center-left";
    });
    const auto centerRight = std::find_if(elements.begin(), elements.end(), [](const auto& element) {
        return element["id"] == "center-right";
    });
    ASSERT_NE(laneLeft, elements.end());
    ASSERT_NE(laneRight, elements.end());
    ASSERT_NE(centerLeft, elements.end());
    ASSERT_NE(centerRight, elements.end());
    EXPECT_EQ((*laneLeft)["group"], "right");
    EXPECT_EQ((*laneLeft)["travelDirection"], "reverse");
    EXPECT_EQ((*laneRight)["group"], "left");
    EXPECT_EQ((*laneRight)["travelDirection"], "forward");
    EXPECT_EQ((*centerLeft)["orderKey"], "center-b");
    EXPECT_EQ((*centerRight)["orderKey"], "center-a");
    EXPECT_EQ(reversedSegment.stationAttachments[0]["id"], "attachment-left");
    EXPECT_EQ(reversedSegment.stationAttachments[0]["crossSectionElementId"], "lane-left");

    processor.commit(atlas::application::ReverseRoadSplineCommand("road-1", processor.current().number));
    EXPECT_EQ(processor.current().normalizedSource(), beforeReverse);
}

// Verifies reversing segmented roads swaps ordered segment endpoints and reverses the full segment order.
TEST(CommandProcessor, ReverseSegmentedRoadPreservesCoverageAndReversesOrder) {
    auto processor = processorWithRoad();
    processor.commit(atlas::application::SplitRoadSegmentCommand(
        "road-1", "road-1/segment/initial", 4.0, processor.current().number));
    const auto normalizedBeforeReverse = processor.current().normalizedSource();
    const auto originalRoad = processor.current().project.roadSplines().front();
    const auto originalSegments = processor.current().project.roadSegments();

    processor.commit(atlas::application::ReverseRoadSplineCommand("road-1", processor.current().number));
    const auto& reversedRoad = processor.current().project.roadSplines().front();
    const auto& reversedSegments = processor.current().project.roadSegments();
    ASSERT_EQ(reversedRoad.segmentIds.size(), 2);
    EXPECT_EQ(reversedRoad.segmentIds[0], originalRoad.segmentIds[1]);
    EXPECT_EQ(reversedRoad.segmentIds[1], originalRoad.segmentIds[0]);
    EXPECT_EQ(reversedSegments[0].startAnchorId, originalSegments[1].endAnchorId);
    EXPECT_EQ(reversedSegments[1].endAnchorId, originalSegments[0].startAnchorId);

    processor.commit(atlas::application::ReverseRoadSplineCommand("road-1", processor.current().number));
    EXPECT_EQ(processor.current().normalizedSource(), normalizedBeforeReverse);
}

// Verifies that RoadSpline reversal remaps StationAnchor stations in the same source transaction.
TEST(CommandProcessor, ReverseRoadSplineRemapsStationAnchors) {
    auto processor = processorWithRoad();

    processor.commit(atlas::application::ReverseRoadSplineCommand("road-1", processor.current().number));

    const auto& anchors = processor.current().project.roadSplines().front().stationAnchors;
    ASSERT_EQ(anchors.size(), 3);
    const auto interior = std::find_if(anchors.begin(), anchors.end(), [](const auto& anchor) {
        return anchor.id == "anchor-1";
    });
    ASSERT_NE(interior, anchors.end());
    EXPECT_DOUBLE_EQ(interior->resolvedStation, 2.0);
}

// Verifies that shortening past an anchor requires an explicit resolution.
TEST(CommandProcessor, ShortenRoadSplineCancelBlocksAnchorLoss) {
    auto processor = processorWithRoad();

    EXPECT_THROW(processor.commit(atlas::application::ShortenRoadSplineCommand(
        "road-1", {5.0, 0.0}, processor.current().number,
        atlas::application::ShortenResolution::cancel)), std::invalid_argument);
    EXPECT_EQ(processor.current().number, 1);
}

// Verifies that shortening can move a dependent StationAnchor to the new endpoint.
TEST(CommandProcessor, ShortenRoadSplineCanMoveDependentAnchor) {
    auto processor = processorWithRoad();

    processor.commit(atlas::application::ShortenRoadSplineCommand(
        "road-1", {5.0, 0.0}, processor.current().number,
        atlas::application::ShortenResolution::moveDependents));

    const auto& anchors = processor.current().project.roadSplines().front().stationAnchors;
    const auto interior = std::find_if(anchors.begin(), anchors.end(), [](const auto& anchor) {
        return anchor.id == "anchor-1";
    });
    ASSERT_NE(interior, anchors.end());
    EXPECT_DOUBLE_EQ(interior->resolvedStation, 5.0);
}

// Verifies that shortening can explicitly delete dependent StationAnchors.
TEST(CommandProcessor, ShortenRoadSplineCanDeleteDependents) {
    auto processor = processorWithRoad();

    processor.commit(atlas::application::ShortenRoadSplineCommand(
        "road-1", {5.0, 0.0}, processor.current().number,
        atlas::application::ShortenResolution::deleteDependents));

    EXPECT_EQ(processor.current().project.roadSplines().front().stationAnchors.size(), 2);
}

// Verifies that reversing a RoadSpline is undoable and preserves its stable ID.
TEST(CommandProcessor, ReverseRoadSplineSupportsUndo) {
    auto processor = processorWithRoad();
    processor.commit(atlas::application::ReverseRoadSplineCommand("road-1", processor.current().number));
    ASSERT_TRUE(processor.undo());

    ASSERT_EQ(processor.current().project.roadSplines().size(), 1);
    EXPECT_EQ(processor.current().project.roadSplines().front().id, "road-1");
    EXPECT_EQ(processor.current().project.roadSplines().front().direction, "start-to-end");
}

// Verifies that deleting a RoadSpline is blocked when RoadSegment dependents exist.
TEST(CommandProcessor, DeleteRoadSplineRequiresDependentResolution) {
    auto processor = processorWithRoad();
    processor.commit(atlas::application::ReplaceProjectCommand(
        processor.current().project.withRoadSegment({
            "segment-1", "root-map", "road-1", "start", "end",
            nlohmann::json::object(), nlohmann::json::object()}), processor.current().number));

    EXPECT_THROW(processor.commit(atlas::application::DeleteRoadSplineCommand(
        "road-1", processor.current().number)), std::invalid_argument);
    EXPECT_EQ(processor.current().project.roadSplines().size(), 1);
}

// Verifies explicit RoadSpline deletion removes owned RoadSegments in one undoable transaction.
TEST(CommandProcessor, DeleteRoadSplineCanDeleteOwnedSegmentsAtomically) {
    auto processor = processorWithRoad();
    const auto before = processor.current().normalizedSource();

    processor.commit(atlas::application::DeleteRoadSplineCommand(
        "road-1", processor.current().number,
        atlas::application::RoadDeleteResolution::deleteOwnedSegments));

    EXPECT_TRUE(processor.current().project.roadSplines().empty());
    EXPECT_TRUE(processor.current().project.roadSegments().empty());
    ASSERT_TRUE(processor.undo());
    EXPECT_EQ(processor.current().normalizedSource(), before);
}

// Verifies that stale RoadSpline commands are rejected without changing source state.
TEST(CommandProcessor, StaleRoadSplineCommandIsRejected) {
    auto processor = processorWithRoad();
    const auto before = processor.current().normalizedSource();

    EXPECT_THROW(processor.commit(atlas::application::ReverseRoadSplineCommand("road-1", 0)), std::runtime_error);
    EXPECT_EQ(processor.current().normalizedSource(), before);
}

// Verifies that splitting creates contiguous child intervals and preserves lineage.
TEST(CommandProcessor, SplitRoadSegmentCreatesCoveredChildren) {
    auto processor = processorWithRoad();
    auto sourceRoad = processor.current().project.roadSplines().front();
    auto sourceSegment = processor.current().project.roadSegments().front();
    sourceRoad.stationAnchors.push_back({
        "attachment-before", 2.0, atlas::domain::AnchorAffinity::geometryLocked,
        {0.2, "line", 0.2, {2.0, 0.0}}});
    sourceRoad.stationAnchors.push_back({
        "attachment-at-split", 4.0, atlas::domain::AnchorAffinity::geometryLocked,
        {0.4, "line", 0.4, {4.0, 0.0}}});
    sourceSegment.crossSectionState = {{"elements", nlohmann::json::array({
        {{"id", "lane-a"}, {"typeId", "TravelLane"}, {"group", "right"},
            {"orderKey", "1"}, {"widthProfile", {{"constant", 3.5}}}}})},
        {"totalWidthMeters", 8.0}, {"joinStyle", "round"}};
    sourceSegment.stationAttachments = nlohmann::json::array({
        {{"id", "attachment-before"}, {"stationAnchorId", "attachment-before"}},
        {{"id", "attachment-at-split"}, {"stationAnchorId", "attachment-at-split"},
            {"crossSectionElementId", "lane-a"}},
        {{"id", "attachment-after"}, {"stationAnchorId", "anchor-1"},
            {"crossSectionElementId", "lane-a"}}});
    sourceSegment.styleOverrides = {{"material", "asphalt"}};
    sourceSegment.schemaProperties = {{"classification", "local"}};
    sourceSegment.spatialReferences = nlohmann::json::array({"reference-1"});
    sourceSegment.boundaryAttachments = nlohmann::json::array({"attachment-1"});
    sourceSegment.metadata = {{"label", "main road"}};
    processor.commit(atlas::application::ReplaceProjectCommand(
        processor.current().project.withReplacedRoadTopology(sourceRoad, {sourceSegment}),
        processor.current().number));
    const auto sourceBefore = processor.current().normalizedSource();
    processor.commit(atlas::application::SplitRoadSegmentCommand(
        "road-1", "road-1/segment/initial", 4.0, processor.current().number));

    const auto& updatedRoad = processor.current().project.roadSplines().front();
    ASSERT_EQ(updatedRoad.segmentIds.size(), 2);
    ASSERT_EQ(processor.current().project.roadSegments().size(), 2);
    EXPECT_NE(updatedRoad.segmentIds[0], updatedRoad.segmentIds[1]);
    for (const auto& child : processor.current().project.roadSegments()) {
        EXPECT_EQ(child.styleOverrides, sourceSegment.styleOverrides);
        EXPECT_EQ(child.schemaProperties, sourceSegment.schemaProperties);
        EXPECT_EQ(child.spatialReferences, sourceSegment.spatialReferences);
        EXPECT_EQ(child.boundaryAttachments, sourceSegment.boundaryAttachments);
        EXPECT_EQ(child.metadata, sourceSegment.metadata);
    }
    const auto& upstreamElements = processor.current().project.roadSegments()[0].crossSectionState["elements"];
    const auto& downstreamElements = processor.current().project.roadSegments()[1].crossSectionState["elements"];
    ASSERT_EQ(upstreamElements.size(), 1);
    ASSERT_EQ(downstreamElements.size(), 1);
    EXPECT_EQ(upstreamElements[0]["id"], "lane-a");
    EXPECT_NE(downstreamElements[0]["id"], "lane-a");
    EXPECT_EQ(downstreamElements[0]["lineage"]["continuesFrom"], "lane-a");
    ASSERT_EQ(processor.current().project.roadSegments()[0].stationAttachments.size(), 1);
    EXPECT_EQ(processor.current().project.roadSegments()[0].stationAttachments[0]["id"], "attachment-before");
    ASSERT_EQ(processor.current().project.roadSegments()[1].stationAttachments.size(), 2);
    EXPECT_EQ(processor.current().project.roadSegments()[1].stationAttachments[0]["id"], "attachment-at-split");
    EXPECT_EQ(processor.current().project.roadSegments()[1].stationAttachments[1]["id"], "attachment-after");
    EXPECT_EQ(processor.current().project.roadSegments()[1].stationAttachments[0]["crossSectionElementId"],
        downstreamElements[0]["id"]);
    EXPECT_EQ(processor.current().project.roadSegments()[0].lineage["parentSegmentId"], "road-1/segment/initial");
    EXPECT_EQ(processor.current().project.roadSegments()[1].lineage["parentSegmentId"], "road-1/segment/initial");
    EXPECT_TRUE(processor.current().project.roadSegments()[0].lineage["predecessorSegmentId"].is_null());
    EXPECT_EQ(processor.current().project.roadSegments()[0].lineage["successorSegmentId"], updatedRoad.segmentIds[1]);
    EXPECT_EQ(processor.current().project.roadSegments()[1].lineage["predecessorSegmentId"], updatedRoad.segmentIds[0]);
    EXPECT_TRUE(processor.current().project.roadSegments()[1].lineage["successorSegmentId"].is_null());
    const auto sourceAfterSplit = processor.current().normalizedSource();
    processor.commit(atlas::application::MergeRoadSegmentsCommand(
        "road-1", updatedRoad.segmentIds[0], updatedRoad.segmentIds[1], processor.current().number));
    ASSERT_EQ(processor.current().project.roadSegments().size(), 1);
    const auto& mergedSegment = processor.current().project.roadSegments().front();
    EXPECT_EQ(mergedSegment.crossSectionState["elements"][0]["id"], "lane-a");
    ASSERT_EQ(mergedSegment.stationAttachments.size(), 3);
    EXPECT_EQ(mergedSegment.stationAttachments[0]["crossSectionElementId"], "lane-a");
    ASSERT_TRUE(processor.undo());
    EXPECT_EQ(processor.current().normalizedSource(), sourceAfterSplit);
    ASSERT_TRUE(processor.undo());
    EXPECT_EQ(processor.current().normalizedSource(), sourceBefore);
}

// Verifies that splitting near an endpoint is rejected without changing source or history.
TEST(CommandProcessor, SplitRoadSegmentRejectsEndpointTolerance) {
    auto processor = processorWithRoad();
    const auto before = processor.current().normalizedSource();
    const auto revisionBefore = processor.current().number;
    const auto hadUndoBefore = processor.canUndo();

    EXPECT_THROW(processor.commit(atlas::application::SplitRoadSegmentCommand(
        "road-1", "road-1/segment/initial", 1.0e-5, processor.current().number)), std::invalid_argument);
    EXPECT_EQ(processor.current().normalizedSource(), before);
    EXPECT_EQ(processor.current().number, revisionBefore);
    EXPECT_EQ(processor.canUndo(), hadUndoBefore);
}

// Verifies malformed StationAnchor references reject a split without changing source or history.
TEST(CommandProcessor, SplitRoadSegmentRejectsMissingAttachmentAnchorAtomically) {
    auto processor = processorWithRoad();
    auto road = processor.current().project.roadSplines().front();
    auto segment = processor.current().project.roadSegments().front();
    segment.stationAttachments = nlohmann::json::array({
        {{"id", "orphan-attachment"}, {"stationAnchorId", "missing-anchor"}}});
    processor.commit(atlas::application::ReplaceProjectCommand(
        processor.current().project.withReplacedRoadTopology(road, {segment}), processor.current().number));
    const auto before = processor.current().normalizedSource();
    const auto revisionBefore = processor.current().number;
    const auto canUndoBefore = processor.canUndo();

    EXPECT_THROW(processor.commit(atlas::application::SplitRoadSegmentCommand(
        "road-1", segment.id, 4.0, processor.current().number)), std::invalid_argument);
    EXPECT_EQ(processor.current().normalizedSource(), before);
    EXPECT_EQ(processor.current().number, revisionBefore);
    EXPECT_EQ(processor.canUndo(), canUndoBefore);
}

// Verifies boundary movement preserves coverage and reassigns station attachments without changing IDs.
TEST(CommandProcessor, MoveRoadSegmentBoundaryPreservesCoverage) {
    auto processor = processorWithRoad();
    processor.commit(atlas::application::SplitRoadSegmentCommand(
        "road-1", "road-1/segment/initial", 4.0, processor.current().number));
    auto roadBeforeMove = processor.current().project.roadSplines().front();
    roadBeforeMove.stationAnchors.push_back({
        "attachment-5", 5.0, atlas::domain::AnchorAffinity::geometryLocked,
        {0.5, "line", 0.5, {5.0, 0.0}}});
    auto segmentsBeforeMove = processor.current().project.roadSegments();
    segmentsBeforeMove[0].crossSectionState = {{"elements", nlohmann::json::array({
        {{"id", "lane-upstream"}, {"typeId", "TravelLane"}, {"group", "right"}}})},
        {"totalWidthMeters", 8.0}, {"joinStyle", "round"}};
    segmentsBeforeMove[1].crossSectionState = {{"elements", nlohmann::json::array({
        {{"id", "lane-downstream"}, {"typeId", "TravelLane"}, {"group", "right"},
            {"lineage", {{"continuesFrom", "lane-upstream"}}}}})},
        {"totalWidthMeters", 8.0}, {"joinStyle", "round"}};
    segmentsBeforeMove[1].stationAttachments = nlohmann::json::array({
        {{"id", "attachment-stable"}, {"stationAnchorId", "attachment-5"},
            {"crossSectionElementId", "lane-downstream"}}});
    processor.commit(atlas::application::ReplaceProjectCommand(
        processor.current().project.withReplacedRoadTopology(roadBeforeMove, segmentsBeforeMove),
        processor.current().number));
    const auto boundaryId = processor.current().project.roadSegments()[0].endAnchorId;

    processor.commit(atlas::application::MoveRoadSegmentBoundaryCommand(
        "road-1", boundaryId, 6.0, processor.current().number));

    const auto& segments = processor.current().project.roadSegments();
    const auto& road = processor.current().project.roadSplines().front();
    const auto movedAnchor = std::find_if(road.stationAnchors.begin(), road.stationAnchors.end(), [&](const auto& anchor) {
        return anchor.id == boundaryId;
    });
    ASSERT_NE(movedAnchor, road.stationAnchors.end());
    EXPECT_DOUBLE_EQ(movedAnchor->resolvedStation, 6.0);
    EXPECT_EQ(segments[0].endAnchorId, segments[1].startAnchorId);
    ASSERT_EQ(segments[0].stationAttachments.size(), 1);
    EXPECT_EQ(segments[0].stationAttachments[0]["id"], "attachment-stable");
    EXPECT_EQ(segments[0].stationAttachments[0]["crossSectionElementId"], "lane-upstream");
    EXPECT_TRUE(segments[1].stationAttachments.empty());
}

// Verifies that a boundary move creating a zero-length interval fails atomically.
TEST(CommandProcessor, MoveRoadSegmentBoundaryRejectsZeroLengthInterval) {
    auto processor = processorWithRoad();
    processor.commit(atlas::application::SplitRoadSegmentCommand(
        "road-1", "road-1/segment/initial", 4.0, processor.current().number));
    const auto boundaryId = processor.current().project.roadSegments()[0].endAnchorId;
    const auto before = processor.current().normalizedSource();
    const auto revisionBefore = processor.current().number;

    EXPECT_THROW(processor.commit(atlas::application::MoveRoadSegmentBoundaryCommand(
        "road-1", boundaryId, 10.0, processor.current().number)), std::invalid_argument);
    EXPECT_EQ(processor.current().normalizedSource(), before);
    EXPECT_EQ(processor.current().number, revisionBefore);
}

// Verifies segmentation previews invalidate only the affected RoadSpline station range.
TEST(CommandProcessor, SegmentationCommandsEmitScopedInvalidations) {
    auto processor = processorWithRoad();
    processor.commit(atlas::application::SplitRoadSegmentCommand(
        "road-1", "road-1/segment/initial", 4.0, processor.current().number));
    auto road = processor.current().project.roadSplines().front();
    processor.commit(atlas::application::SplitRoadSegmentCommand(
        "road-1", road.segmentIds[1], 7.0, processor.current().number));
    road = processor.current().project.roadSplines().front();

    const auto splitPreview = processor.preview(atlas::application::SplitRoadSegmentCommand(
        "road-1", road.segmentIds[1], 5.0, processor.current().number));
    ASSERT_EQ(splitPreview.invalidations.size(), 2);
    for (const auto propertyClass : {"road-segmentation", "road-envelope"}) {
        const auto invalidation = std::find_if(splitPreview.invalidations.begin(), splitPreview.invalidations.end(),
            [&](const auto& item) { return item.propertyClass == propertyClass; });
        ASSERT_NE(invalidation, splitPreview.invalidations.end());
        EXPECT_EQ(invalidation->sourceId, "road-1");
        ASSERT_TRUE(invalidation->stationRange.has_value());
        EXPECT_DOUBLE_EQ(invalidation->stationRange->start, 4.0);
        EXPECT_DOUBLE_EQ(invalidation->stationRange->end, 7.0);
    }

    const auto& segments = processor.current().project.roadSegments();
    const auto boundaryPreview = processor.preview(atlas::application::MoveRoadSegmentBoundaryCommand(
        "road-1", segments[0].endAnchorId, 3.0, processor.current().number));
    ASSERT_EQ(boundaryPreview.invalidations.size(), 2);
    for (const auto& invalidation : boundaryPreview.invalidations) {
        ASSERT_TRUE(invalidation.stationRange.has_value());
        EXPECT_DOUBLE_EQ(invalidation.stationRange->start, 0.0);
        EXPECT_DOUBLE_EQ(invalidation.stationRange->end, 7.0);
    }

    const auto mergePreview = processor.preview(atlas::application::MergeRoadSegmentsCommand(
        "road-1", road.segmentIds[1], road.segmentIds[2], processor.current().number));
    ASSERT_EQ(mergePreview.invalidations.size(), 2);
    for (const auto& invalidation : mergePreview.invalidations) {
        ASSERT_TRUE(invalidation.stationRange.has_value());
        EXPECT_DOUBLE_EQ(invalidation.stationRange->start, 4.0);
        EXPECT_DOUBLE_EQ(invalidation.stationRange->end, 10.0);
    }

    processor.commit(atlas::application::SplitRoadSegmentCommand(
        "road-1", road.segmentIds[1], 5.0, processor.current().number));
    ASSERT_EQ(processor.lastInvalidations().size(), 2);
    for (const auto& invalidation : processor.lastInvalidations()) {
        EXPECT_DOUBLE_EQ(invalidation.stationRange->start, 4.0);
        EXPECT_DOUBLE_EQ(invalidation.stationRange->end, 7.0);
    }
    ASSERT_TRUE(processor.undo());
    ASSERT_EQ(processor.lastInvalidations().size(), 2);
    for (const auto& invalidation : processor.lastInvalidations()) {
        EXPECT_DOUBLE_EQ(invalidation.stationRange->start, 4.0);
        EXPECT_DOUBLE_EQ(invalidation.stationRange->end, 7.0);
        EXPECT_EQ(invalidation.revision, processor.current().number);
    }
    ASSERT_TRUE(processor.redo());
    EXPECT_EQ(processor.lastInvalidations()[0].revision, processor.current().number);
}

// Verifies that adjacent equivalent segments merge while retaining the upstream ID and lineage.
TEST(CommandProcessor, MergeEquivalentRoadSegmentsRetainsUpstreamIdentity) {
    auto processor = processorWithRoad();
    processor.commit(atlas::application::SplitRoadSegmentCommand(
        "road-1", "road-1/segment/initial", 4.0, processor.current().number));
    const auto road = processor.current().project.roadSplines().front();
    const auto upstreamId = road.segmentIds[0];
    const auto downstreamId = road.segmentIds[1];

    processor.commit(atlas::application::MergeRoadSegmentsCommand(
        "road-1", upstreamId, downstreamId, processor.current().number));

    ASSERT_EQ(processor.current().project.roadSegments().size(), 1);
    EXPECT_EQ(processor.current().project.roadSegments().front().id, upstreamId);
    const auto archived = processor.current().project.roadSegments().front().lineage["mergedFrom"];
    EXPECT_EQ(archived["upstream"]["id"], upstreamId);
    EXPECT_EQ(archived["downstream"]["id"], downstreamId);
    EXPECT_TRUE(archived["upstream"].contains("lineage"));
    EXPECT_TRUE(archived["downstream"].contains("lineage"));
}

// Verifies that merging adjacent segments participates in exact undo and redo snapshots.
TEST(CommandProcessor, MergeRoadSegmentsSupportsExactUndoAndRedo) {
    auto processor = processorWithRoad();
    processor.commit(atlas::application::SplitRoadSegmentCommand(
        "road-1", "road-1/segment/initial", 4.0, processor.current().number));
    const auto beforeMerge = processor.current().normalizedSource();
    const auto road = processor.current().project.roadSplines().front();
    processor.commit(atlas::application::MergeRoadSegmentsCommand(
        "road-1", road.segmentIds[0], road.segmentIds[1], processor.current().number));
    const auto afterMerge = processor.current().normalizedSource();

    ASSERT_TRUE(processor.undo());
    EXPECT_EQ(processor.current().normalizedSource(), beforeMerge);
    ASSERT_TRUE(processor.redo());
    EXPECT_EQ(processor.current().normalizedSource(), afterMerge);
}

// Verifies that non-equivalent adjacent segment states cannot merge silently.
TEST(CommandProcessor, MergeNonEquivalentRoadSegmentsIsRejected) {
    auto processor = processorWithRoad();
    processor.commit(atlas::application::SplitRoadSegmentCommand(
        "road-1", "road-1/segment/initial", 4.0, processor.current().number));
    auto road = processor.current().project.roadSplines().front();
    auto segments = processor.current().project.roadSegments();
    segments[1].crossSectionState["width"] = 12.0;
    processor.commit(atlas::application::ReplaceProjectCommand(
        processor.current().project.withReplacedRoadTopology(road, segments), processor.current().number));
    const auto currentRoad = processor.current().project.roadSplines().front();

    EXPECT_THROW(processor.commit(atlas::application::MergeRoadSegmentsCommand(
        "road-1", currentRoad.segmentIds[0], currentRoad.segmentIds[1], processor.current().number)),
        std::invalid_argument);
}

// Verifies every roadmap-defined segment equivalence field blocks a destructive merge when changed.
TEST(CommandProcessor, MergeRejectsDifferencesInEveryEquivalenceField) {
    const auto verifyRejected = [](const std::function<void(atlas::domain::RoadSegment&)>& change) {
        auto processor = processorWithRoad();
        processor.commit(atlas::application::SplitRoadSegmentCommand(
            "road-1", "road-1/segment/initial", 4.0, processor.current().number));
        const auto road = processor.current().project.roadSplines().front();
        auto segments = processor.current().project.roadSegments();
        change(segments[1]);
        processor.commit(atlas::application::ReplaceProjectCommand(
            processor.current().project.withReplacedRoadTopology(road, segments), processor.current().number));
        const auto updatedRoad = processor.current().project.roadSplines().front();
        EXPECT_THROW(processor.commit(atlas::application::MergeRoadSegmentsCommand(
            "road-1", updatedRoad.segmentIds[0], updatedRoad.segmentIds[1], processor.current().number)),
            std::invalid_argument);
    };

    verifyRejected([](auto& segment) { segment.crossSectionState["width"] = 2.0; });
    verifyRejected([](auto& segment) { segment.styleOverrides["style"] = "alternate"; });
    verifyRejected([](auto& segment) { segment.schemaProperties["custom"] = true; });
    verifyRejected([](auto& segment) { segment.spatialReferences.push_back("level-upper"); });
    verifyRejected([](auto& segment) { segment.boundaryAttachments.push_back("attachment-1"); });
    verifyRejected([](auto& segment) { segment.metadata["note"] = "different"; });
}

// Verifies merge conflicts are exposed by field and each field can be resolved independently.
TEST(CommandProcessor, MergeReportsAndResolvesFieldConflicts) {
    auto processor = processorWithRoad();
    processor.commit(atlas::application::SplitRoadSegmentCommand(
        "road-1", "road-1/segment/initial", 4.0, processor.current().number));
    auto road = processor.current().project.roadSplines().front();
    auto segments = processor.current().project.roadSegments();
    segments[1].styleOverrides["material"] = "concrete";
    segments[1].metadata["source"] = "downstream";
    processor.commit(atlas::application::ReplaceProjectCommand(
        processor.current().project.withReplacedRoadTopology(road, segments), processor.current().number));
    road = processor.current().project.roadSplines().front();
    const auto merge = atlas::application::MergeRoadSegmentsCommand(
        "road-1", road.segmentIds[0], road.segmentIds[1], processor.current().number);
    EXPECT_EQ(merge.conflictingFields(processor.current()),
        (std::vector<std::string>{"styleOverrides", "metadata"}));
    EXPECT_THROW(processor.commit(merge), std::invalid_argument);

    const auto resolvedMerge = atlas::application::MergeRoadSegmentsCommand(
        "road-1", road.segmentIds[0], road.segmentIds[1], processor.current().number,
        {{"styleOverrides", atlas::application::SegmentMergeSource::downstream},
            {"metadata", atlas::application::SegmentMergeSource::upstream}});
    processor.commit(resolvedMerge);
    const auto& merged = processor.current().project.roadSegments().front();
    EXPECT_EQ(merged.styleOverrides["material"], "concrete");
    EXPECT_FALSE(merged.metadata.contains("source"));
}

// Verifies seeded split sequences preserve complete non-overlapping station coverage after every edit.
TEST(CommandProcessor, RepeatedRoadSegmentSplitsPreserveCoverage) {
    auto processor = processorWithRoad();
    std::mt19937 randomGenerator(0xA71A5u);
    std::uniform_real_distribution<double> splitFraction(0.25, 0.75);
    for (int splitIndex = 0; splitIndex < 32; ++splitIndex) {
        const auto& project = processor.current().project;
        const auto& road = project.roadSplines().front();
        struct CandidateInterval {
            std::string segmentId;
            double start;
            double end;
        };
        std::vector<CandidateInterval> candidateIntervals;
        for (const auto& segment : project.roadSegments()) {
            const auto start = std::find_if(road.stationAnchors.begin(), road.stationAnchors.end(), [&](const auto& anchor) {
                return anchor.id == segment.startAnchorId;
            })->resolvedStation;
            const auto end = std::find_if(road.stationAnchors.begin(), road.stationAnchors.end(), [&](const auto& anchor) {
                return anchor.id == segment.endAnchorId;
            })->resolvedStation;
            if (end - start > 2.0e-4) {
                candidateIntervals.push_back({segment.id, start, end});
            }
        }
        ASSERT_FALSE(candidateIntervals.empty());
        std::uniform_int_distribution<std::size_t> chooseInterval(0, candidateIntervals.size() - 1);
        const auto& selectedInterval = candidateIntervals[chooseInterval(randomGenerator)];
        const auto splitStation = selectedInterval.start +
            (selectedInterval.end - selectedInterval.start) * splitFraction(randomGenerator);
        processor.commit(atlas::application::SplitRoadSegmentCommand(
            "road-1", selectedInterval.segmentId, splitStation, processor.current().number));

        const auto& updatedRoad = processor.current().project.roadSplines().front();
        const auto& segments = processor.current().project.roadSegments();
        ASSERT_EQ(segments.size(), updatedRoad.segmentIds.size());
        EXPECT_NEAR(std::find_if(updatedRoad.stationAnchors.begin(), updatedRoad.stationAnchors.end(),
            [&](const auto& anchor) { return anchor.id == segments.front().startAnchorId; })->resolvedStation,
            0.0, 1.0e-9);
        for (std::size_t index = 0; index < segments.size(); ++index) {
            const auto start = std::find_if(updatedRoad.stationAnchors.begin(), updatedRoad.stationAnchors.end(),
                [&](const auto& anchor) { return anchor.id == segments[index].startAnchorId; })->resolvedStation;
            const auto end = std::find_if(updatedRoad.stationAnchors.begin(), updatedRoad.stationAnchors.end(),
                [&](const auto& anchor) { return anchor.id == segments[index].endAnchorId; })->resolvedStation;
            EXPECT_GT(end - start, 1.0e-4);
            if (index > 0) {
                const auto previousEnd = std::find_if(updatedRoad.stationAnchors.begin(), updatedRoad.stationAnchors.end(),
                    [&](const auto& anchor) { return anchor.id == segments[index - 1].endAnchorId; })->resolvedStation;
                EXPECT_DOUBLE_EQ(previousEnd, start);
            }
        }
        const auto finalEnd = std::find_if(updatedRoad.stationAnchors.begin(), updatedRoad.stationAnchors.end(),
            [&](const auto& anchor) { return anchor.id == segments.back().endAnchorId; })->resolvedStation;
        EXPECT_NEAR(finalEnd, 10.0, 1.0e-9);
    }
}

// Verifies seeded split, move, and merge sequences retain ordered gap-free coverage.
TEST(CommandProcessor, SeededSplitMoveMergeSequencesPreserveCoverage) {
    for (std::uint32_t seed = 0; seed < 5; ++seed) {
        auto processor = processorWithRoad();
        std::mt19937 randomGenerator(0xA71A5u + seed);
        std::uniform_int_distribution<int> chooseOperation(0, 2);
        std::uniform_real_distribution<double> chooseFraction(0.25, 0.75);

        for (int editIndex = 0; editIndex < 40; ++editIndex) {
            const auto& project = processor.current().project;
            const auto& road = project.roadSplines().front();
            std::vector<const atlas::domain::RoadSegment*> orderedSegments;
            for (const auto& segmentId : road.segmentIds) {
                const auto segment = std::find_if(project.roadSegments().begin(), project.roadSegments().end(),
                    [&](const auto& candidate) { return candidate.id == segmentId; });
                ASSERT_NE(segment, project.roadSegments().end());
                orderedSegments.push_back(&*segment);
            }

            const auto operation = chooseOperation(randomGenerator);
            if (operation == 0 || orderedSegments.size() == 1) {
                std::vector<std::pair<const atlas::domain::RoadSegment*, std::pair<double, double>>> splittable;
                for (const auto* segment : orderedSegments) {
                    const auto start = std::find_if(road.stationAnchors.begin(), road.stationAnchors.end(),
                        [&](const auto& anchor) { return anchor.id == segment->startAnchorId; })->resolvedStation;
                    const auto end = std::find_if(road.stationAnchors.begin(), road.stationAnchors.end(),
                        [&](const auto& anchor) { return anchor.id == segment->endAnchorId; })->resolvedStation;
                    if (end - start > 2.0e-4) splittable.push_back({segment, {start, end}});
                }
                ASSERT_FALSE(splittable.empty());
                std::uniform_int_distribution<std::size_t> chooseSegment(0, splittable.size() - 1);
                const auto& selected = splittable[chooseSegment(randomGenerator)];
                const auto splitStation = selected.second.first +
                    (selected.second.second - selected.second.first) * chooseFraction(randomGenerator);
                processor.commit(atlas::application::SplitRoadSegmentCommand(
                    road.id, selected.first->id, splitStation, processor.current().number));
            } else if (operation == 1) {
                std::uniform_int_distribution<std::size_t> chooseBoundary(1, orderedSegments.size() - 1);
                const auto boundaryIndex = chooseBoundary(randomGenerator);
                const auto pairStart = std::find_if(road.stationAnchors.begin(), road.stationAnchors.end(),
                    [&](const auto& anchor) { return anchor.id == orderedSegments[boundaryIndex - 1]->startAnchorId; }
                    )->resolvedStation;
                const auto pairEnd = std::find_if(road.stationAnchors.begin(), road.stationAnchors.end(),
                    [&](const auto& anchor) { return anchor.id == orderedSegments[boundaryIndex]->endAnchorId; }
                    )->resolvedStation;
                const auto newStation = pairStart + (pairEnd - pairStart) * chooseFraction(randomGenerator);
                processor.commit(atlas::application::MoveRoadSegmentBoundaryCommand(
                    road.id, orderedSegments[boundaryIndex - 1]->endAnchorId,
                    newStation, processor.current().number));
            } else {
                std::uniform_int_distribution<std::size_t> choosePair(0, orderedSegments.size() - 2);
                const auto pairIndex = choosePair(randomGenerator);
                processor.commit(atlas::application::MergeRoadSegmentsCommand(
                    road.id, orderedSegments[pairIndex]->id, orderedSegments[pairIndex + 1]->id,
                    processor.current().number));
            }

            const auto& updatedProject = processor.current().project;
            const auto& updatedRoad = updatedProject.roadSplines().front();
            ASSERT_EQ(updatedRoad.segmentIds.size(), updatedProject.roadSegments().size());
            double previousEnd = 0.0;
            for (std::size_t index = 0; index < updatedRoad.segmentIds.size(); ++index) {
                const auto segment = std::find_if(updatedProject.roadSegments().begin(), updatedProject.roadSegments().end(),
                    [&](const auto& candidate) { return candidate.id == updatedRoad.segmentIds[index]; });
                ASSERT_NE(segment, updatedProject.roadSegments().end());
                const auto start = std::find_if(updatedRoad.stationAnchors.begin(), updatedRoad.stationAnchors.end(),
                    [&](const auto& anchor) { return anchor.id == segment->startAnchorId; })->resolvedStation;
                const auto end = std::find_if(updatedRoad.stationAnchors.begin(), updatedRoad.stationAnchors.end(),
                    [&](const auto& anchor) { return anchor.id == segment->endAnchorId; })->resolvedStation;
                EXPECT_GT(end, start);
                EXPECT_DOUBLE_EQ(start, previousEnd);
                previousEnd = end;
            }
            EXPECT_DOUBLE_EQ(previousEnd, 10.0);
        }
    }
}
