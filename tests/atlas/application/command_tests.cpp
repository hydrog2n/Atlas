#include "atlas/application/command.hpp"

#include <gtest/gtest.h>

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

TEST(CommandProcessor, StaleRevisionIsRejected) {
    atlas::application::CommandProcessor processor({
        4, atlas::domain::Project::empty("first", "root-map"), {}});

    EXPECT_THROW(
        processor.commit(atlas::application::ReplaceProjectCommand(
            atlas::domain::Project::empty("second", "root-map"), 3)),
        std::runtime_error);
    EXPECT_EQ(processor.current().project.id(), "first");
}

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

TEST(CommandProcessor, DestructiveImpactRequiresExplicitResolution) {
    const auto cancelled = atlas::application::resolveDestructiveImpact(
        {"source", "dependent"}, atlas::application::ImpactResolution::cancel);
    const auto accepted = atlas::application::resolveDestructiveImpact(
        {"source", "dependent"}, atlas::application::ImpactResolution::retarget);

    EXPECT_FALSE(cancelled.resolved);
    EXPECT_TRUE(accepted.resolved);
    EXPECT_EQ(accepted.affectedIds.size(), 2);
}

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
