#include "atlas/domain/project.hpp"

#include <iostream>

// Minimal command-line entry point for the project.
int main() {
    // The project needs a stable project ID and a root map ID.
    const auto project = atlas::domain::Project::empty(
        "00000000-0000-4000-8000-000000000001",
        "00000000-0000-4000-8000-000000000002");

    // Print the normalized project so the canonical structure is visible to tools and users.
    std::cout << project.normalizedJson();
    return 0;
}
