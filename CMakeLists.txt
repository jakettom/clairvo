cmake_minimum_required(VERSION 3.26)

set(CMAKE_OSX_DEPLOYMENT_TARGET "14.0" CACHE STRING "Minimum macOS version")

project(Clairvo VERSION 0.1.0 LANGUAGES C CXX OBJCXX)

option(CLAIRVO_BUILD_APP "Build the macOS SwiftUI application" ON)
option(CLAIRVO_BUILD_TESTS "Build the C++ test suite" ON)

if(CLAIRVO_BUILD_APP)
    enable_language(Swift)
    set(CMAKE_Swift_LANGUAGE_VERSION 5)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_OBJCXX_STANDARD 20)
set(CMAKE_OBJCXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

include(cmake/Dependencies.cmake)

set(CLAIRVO_WARNINGS -Wall -Wextra -Wpedantic -Wno-unused-parameter)

# ---------------------------------------------------------------------------
# Core engine: data model, commands/undo, organization, changes, filesystem.
# Pure C++ with no UI, database or media dependencies.
# ---------------------------------------------------------------------------
add_library(clairvo_core STATIC
    core/util/PathUtil.cpp
    core/util/StringUtil.cpp
    core/model/Ids.cpp
    core/model/ProjectModel.cpp
    core/metadata/MetadataCategory.cpp
    core/metadata/MetadataNormalizer.cpp
    core/undo/UndoManager.cpp
    core/commands/EditCommands.cpp
    core/organization/NamingEngine.cpp
    core/organization/OrganizationEngine.cpp
    core/changes/ChangeSet.cpp
    core/filesystem/PosixFileSystem.cpp
    core/filesystem/PreflightValidator.cpp
    core/filesystem/ApplyEngine.cpp
    core/filesystem/MissingFileDetector.cpp
)
target_include_directories(clairvo_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_options(clairvo_core PRIVATE ${CLAIRVO_WARNINGS})

# ---------------------------------------------------------------------------
# Persistence: SQLite-backed project file, schema migrations, apply journal.
# ---------------------------------------------------------------------------
add_library(clairvo_database STATIC
    database/Sqlite.cpp
    database/Schema.cpp
    database/ProjectStore.cpp
)
target_link_libraries(clairvo_database PUBLIC clairvo_core SQLite::SQLite3)
target_compile_options(clairvo_database PRIVATE ${CLAIRVO_WARNINGS})

# ---------------------------------------------------------------------------
# Media: scanning, supported formats, fingerprinting, AVFoundation metadata.
# ---------------------------------------------------------------------------
add_library(clairvo_media STATIC
    media/formats/SupportedFormats.cpp
    media/scanning/DirectoryScanner.cpp
    media/hashing/FileHasher.cpp
    media/extraction/AVFoundationExtractor.mm
)
target_link_libraries(clairvo_media PUBLIC clairvo_core
    "-framework Foundation" "-framework AVFoundation" "-framework CoreMedia" "-framework CoreGraphics")
target_compile_options(clairvo_media PRIVATE ${CLAIRVO_WARNINGS})
set_source_files_properties(media/extraction/AVFoundationExtractor.mm
    PROPERTIES COMPILE_OPTIONS "-fobjc-arc")

# ---------------------------------------------------------------------------
# Application layer + C bridge consumed by the Swift UI.
# ---------------------------------------------------------------------------
add_library(clairvo_app STATIC
    app/application/ProjectSession.cpp
    app/application/ImportService.cpp
    app/application/SearchService.cpp
    app/application/JsonCodec.cpp
    app/bridge/Bridge.cpp
)
target_include_directories(clairvo_app PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/app/bridge/include)
target_link_libraries(clairvo_app PUBLIC clairvo_core clairvo_database clairvo_media nlohmann_json::nlohmann_json)
target_compile_options(clairvo_app PRIVATE ${CLAIRVO_WARNINGS})

add_executable(clairvo-cli tools/cli/main.cpp)
target_link_libraries(clairvo-cli PRIVATE clairvo_app)
target_compile_options(clairvo-cli PRIVATE ${CLAIRVO_WARNINGS})

if(CLAIRVO_BUILD_APP)
    add_subdirectory(macos)
endif()

if(CLAIRVO_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
