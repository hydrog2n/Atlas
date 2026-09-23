cmake_minimum_required(VERSION 3.25)

# This is the top-level build definition for the Atlas project.
# It tells CMake which language we use, which components to build,
# and how the project is organized into libraries, executables, and tests.
project(Atlas VERSION 0.3.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
    # MSVC needs explicit exception handling settings in many C++ projects.
    add_compile_options(/EHsc)
endif()

# The UI and tests are optional. This lets us build a lightweight headless
# version for CI or automated validation without pulling in the desktop shell.
option(ATLAS_BUILD_UI "Build the Qt Widgets desktop shell" ON)
option(ATLAS_BUILD_TESTS "Build the GoogleTest suite" ON)

find_package(nlohmann_json CONFIG REQUIRED)

# The domain library is where the authoritative project logic lives.
# This is deliberately separate from Qt and from the CLI/UI layer.
add_library(atlas_domain STATIC
    src/atlas/domain/project.cpp
)
target_include_directories(atlas_domain
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_link_libraries(atlas_domain PUBLIC nlohmann_json::nlohmann_json)
target_compile_features(atlas_domain PUBLIC cxx_std_23)

add_library(atlas_persistence STATIC
    src/atlas/persistence/package.cpp
)
# Persistence depends on the domain model but remains independent of Qt and the UI.
target_include_directories(atlas_persistence
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_link_libraries(atlas_persistence PUBLIC atlas_domain)
target_compile_features(atlas_persistence PUBLIC cxx_std_23)

add_library(atlas_application STATIC
    src/atlas/application/command.cpp
)
target_include_directories(atlas_application
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_link_libraries(atlas_application PUBLIC atlas_domain)
target_compile_features(atlas_application PUBLIC cxx_std_23)

# The command-line executable is a tiny test/demo runner for the domain model.
add_executable(atlas_cli src/atlas/cli/main.cpp)
target_link_libraries(atlas_cli PRIVATE atlas_persistence)

if(ATLAS_BUILD_UI)
    # The desktop shell is the user-facing application layer.
    # It depends on the domain layer, but the domain layer does not depend on Qt.
    find_package(Qt6 CONFIG REQUIRED COMPONENTS Widgets)

    add_executable(atlas WIN32 src/atlas/ui/main.cpp)
    target_link_libraries(atlas PRIVATE atlas_domain Qt6::Widgets)
    target_compile_definitions(atlas PRIVATE QT_NO_KEYWORDS)
endif()

if(ATLAS_BUILD_TESTS)
    # GoogleTest is used to enforce the project's core rules as executable checks.
    enable_testing()
    find_package(GTest CONFIG REQUIRED)

    add_executable(atlas_domain_tests
        tests/atlas/domain/project_tests.cpp
    )
    target_link_libraries(atlas_domain_tests
        PRIVATE
            atlas_domain
            GTest::gtest_main
    )
    include(GoogleTest)
    gtest_discover_tests(atlas_domain_tests)

    add_executable(atlas_persistence_tests
        tests/atlas/persistence/package_tests.cpp
    )
    target_link_libraries(atlas_persistence_tests
        PRIVATE
            atlas_persistence
            GTest::gtest_main
    )
    target_compile_definitions(atlas_persistence_tests PRIVATE
        ATLAS_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
    )
    gtest_discover_tests(atlas_persistence_tests)

    add_executable(atlas_application_tests
        tests/atlas/application/command_tests.cpp
    )
    target_link_libraries(atlas_application_tests
        PRIVATE
            atlas_application
            GTest::gtest_main
    )
    gtest_discover_tests(atlas_application_tests)
endif()

# These installation rules make the app usable after a build.
install(TARGETS atlas_cli atlas_persistence RUNTIME DESTINATION bin)
if(ATLAS_BUILD_UI)
    install(TARGETS atlas RUNTIME DESTINATION bin)
endif()
