#include "atlas/application/command.hpp"

#include <gtest/gtest.h>

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
