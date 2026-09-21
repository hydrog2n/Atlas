#include "atlas/domain/project.hpp"

#include <iostream>

int main() {
    const auto project = atlas::domain::Project::empty(
        "00000000-0000-4000-8000-000000000001",
        "00000000-0000-4000-8000-000000000002");
    std::cout << project.normalizedJson();
    return 0;
}
