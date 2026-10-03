set(CLAIRVO_SWIFT_SOURCES
    Clairvo/ClairvoApp.swift
    Clairvo/Bridge/CoreSession.swift
    Clairvo/Models/Snapshot.swift
    Clairvo/Models/ProjectIndex.swift
    Clairvo/ViewModels/ProjectStore.swift
    Clairvo/Views/RootView.swift
    Clairvo/Views/WelcomeView.swift
    Clairvo/Views/MainView.swift
    Clairvo/Views/TreeView.swift
    Clairvo/Views/ClipTableView.swift
    Clairvo/Views/CanvasView.swift
    Clairvo/Views/InspectorView.swift
    Clairvo/Views/OrganizeSheet.swift
    Clairvo/Views/ReviewSheet.swift
    Clairvo/Views/ApplyResultView.swift
    Clairvo/Views/ImportProgressView.swift
    Clairvo/Views/Components.swift
)

add_executable(Clairvo MACOSX_BUNDLE ${CLAIRVO_SWIFT_SOURCES} Clairvo/Resources/AppIcon.icns)

set_source_files_properties(Clairvo/Resources/AppIcon.icns PROPERTIES MACOSX_PACKAGE_LOCATION Resources)

set_target_properties(Clairvo PROPERTIES
    OUTPUT_NAME "Clairvo"
    MACOSX_BUNDLE_INFO_PLIST ${CMAKE_CURRENT_SOURCE_DIR}/Clairvo/Resources/Info.plist.in
    MACOSX_BUNDLE_GUI_IDENTIFIER "com.clairvo.Clairvo"
    MACOSX_BUNDLE_BUNDLE_NAME "Clairvo"
    MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
    MACOSX_BUNDLE_ICON_FILE "AppIcon"
    XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER "com.clairvo.Clairvo"
    XCODE_ATTRIBUTE_SWIFT_VERSION "5.0"
    XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY "-"
    XCODE_ATTRIBUTE_ENABLE_HARDENED_RUNTIME YES
)

target_compile_options(Clairvo PRIVATE $<$<COMPILE_LANGUAGE:Swift>:-parse-as-library>)
target_link_libraries(Clairvo PRIVATE clairvo_app
    "-framework AppKit" "-framework SwiftUI" "-framework UniformTypeIdentifiers")
