#include "atlas/domain/project.hpp"
#include "atlas/persistence/package.hpp"

#include <filesystem>
#include <iostream>
#include <stdexcept>

namespace {

namespace domain = atlas::domain;
namespace persistence = atlas::persistence;

domain::Project createDefaultProject() {
    return domain::Project::empty(
        "00000000-0000-4000-8000-000000000001",
        "00000000-0000-4000-8000-000000000002");
}

} // namespace

// Minimal command-line entry point for the project.
int main(int argc, char* argv[]) {
    // These commands stay headless so package behavior can be tested without Qt.
    const std::string command = argc > 1 ? argv[1] : "";

    // Create builds the smallest valid project and writes it as a canonical package.
    // The package writer creates the manifest, source records, hashes, and checkpoints.
    if (argc == 3 && command == "create") {
        persistence::Package::fromProject(createDefaultProject()).save(argv[2]);
        std::cout << "Created " << argv[2] << '\n';
        return 0;
    }

    // Inspect loads and validates a package, then prints its manifest.
    // This is useful for checking identity, schema, hashes, and generator metadata
    // without opening the graphical editor or changing the package.
    if (argc == 3 && command == "inspect") {
        const auto package = persistence::Package::load(argv[2]);
        std::cout << package.manifestJson();
        return 0;
    }

    // Recover opens the newest retained checkpoint, or the current package when
    // no checkpoint exists. It exposes authoritative source data, not cache output.
    if (argc == 3 && command == "recover") {
        const auto package = persistence::Package::recover(argv[2]);
        std::cout << package.project().normalizedJson();
        return 0;
    }

    // Migrate checks the package schema and returns a machine-readable report.
    // A future schema step can add transformations without changing this CLI contract.
    if (argc == 4 && command == "migrate") {
        const auto report = persistence::Package::migrate(
            argv[2], std::stoi(argv[3]));
        std::cout << report.toJson();
        return 0;
    }

    if (argc != 1) {
        std::cerr << "Usage: atlas_cli [create|inspect|recover|migrate] <package> [schema]\n";
        return 2;
    }

    // With no command, retain the original foundation behavior: print a deterministic
    // empty project so scripts can inspect the domain model without filesystem access.
    const auto project = createDefaultProject();

    // Print the normalized project so the canonical structure is visible to tools and users.
    std::cout << project.normalizedJson();
    return 0;
}
