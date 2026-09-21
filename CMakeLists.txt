cmake_minimum_required(VERSION 3.25)

project(Atlas VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
    add_compile_options(/EHsc)
endif()

option(ATLAS_BUILD_UI "Build the Qt Widgets desktop shell" ON)
option(ATLAS_BUILD_TESTS "Build the GoogleTest suite" ON)

find_package(nlohmann_json CONFIG REQUIRED)

add_library(atlas_domain STATIC
    src/atlas/domain/project.cpp
)
target_include_directories(atlas_domain
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_link_libraries(atlas_domain PUBLIC nlohmann_json::nlohmann_json)
target_compile_features(atlas_domain PUBLIC cxx_std_23)

add_executable(atlas_cli src/atlas/cli/main.cpp)
target_link_libraries(atlas_cli PRIVATE atlas_domain)

if(ATLAS_BUILD_UI)
    find_package(Qt6 CONFIG REQUIRED COMPONENTS Widgets)

    add_executable(atlas WIN32 src/atlas/ui/main.cpp)
    target_link_libraries(atlas PRIVATE atlas_domain Qt6::Widgets)
    target_compile_definitions(atlas PRIVATE QT_NO_KEYWORDS)
endif()

if(ATLAS_BUILD_TESTS)
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
endif()

install(TARGETS atlas_cli RUNTIME DESTINATION bin)
if(ATLAS_BUILD_UI)
    install(TARGETS atlas RUNTIME DESTINATION bin)
endif()
