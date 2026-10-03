add_executable(clairvo_tests
    support/TestSupport.cpp
    support/TestVideoWriter.mm
    core/PathUtilTests.cpp
    core/ModelTests.cpp
    core/CommandTests.cpp
    core/MetadataNormalizerTests.cpp
    organization/NamingTests.cpp
    organization/OrganizationTests.cpp
    organization/ChangeSetTests.cpp
    filesystem/FileSystemTests.cpp
    filesystem/ApplyTests.cpp
    database/ProjectStoreTests.cpp
    media/MediaTests.cpp
    integration/WorkflowTests.cpp
    integration/BridgeTests.cpp
    integration/StressTests.cpp
)
target_include_directories(clairvo_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(clairvo_tests PRIVATE clairvo_app GTest::gtest_main
    "-framework AVFoundation" "-framework CoreMedia" "-framework CoreVideo" "-framework Foundation")
target_compile_options(clairvo_tests PRIVATE -Wall -Wextra -Wno-unused-parameter)
set_source_files_properties(support/TestVideoWriter.mm PROPERTIES COMPILE_OPTIONS "-fobjc-arc")

include(GoogleTest)
gtest_discover_tests(clairvo_tests DISCOVERY_TIMEOUT 60)
