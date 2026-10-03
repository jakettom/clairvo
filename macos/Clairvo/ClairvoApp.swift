import AppKit
import SwiftUI

final class AppDelegate: NSObject, NSApplicationDelegate {
    func applicationDidFinishLaunching(_ notification: Notification) {
        // Launched from a terminal/bundle without Xcode: make sure we are a regular foreground app.
        NSApp.setActivationPolicy(.regular)
        NSApp.activate(ignoringOtherApps: true)
    }

    func application(_ application: NSApplication, open urls: [URL]) {
        guard let url = urls.first(where: { $0.pathExtension == "project" }) else { return }
        Task { @MainActor in ProjectStore.shared.openProject(url) }
    }

    func applicationShouldTerminateAfterLastWindowClosed(_ sender: NSApplication) -> Bool { true }
}

@main
struct ClairvoApp: App {
    @NSApplicationDelegateAdaptor(AppDelegate.self) private var appDelegate
    @StateObject private var store = ProjectStore.shared

    var body: some Scene {
        WindowGroup("Clairvo") {
            RootView()
                .environmentObject(store)
                .preferredColorScheme(.dark)
                .frame(minWidth: 1000, minHeight: 620)
        }
        .windowToolbarStyle(.unified)
        .commands {
            CommandGroup(replacing: .newItem) {
                Button("New Project…") { store.chooseFolderForNewProject() }
                    .keyboardShortcut("n", modifiers: [.command, .shift])
                Button("Open Project…") { store.chooseProjectToOpen() }
                    .keyboardShortcut("o")
                Divider()
                Button("Close Project") { store.closeProject() }
                    .keyboardShortcut("w", modifiers: [.command, .shift])
                    .disabled(!store.hasProject)
            }
            CommandGroup(replacing: .undoRedo) {
                Button(undoTitle) { store.undo() }
                    .keyboardShortcut("z")
                    .disabled(!(store.snapshot?.undo.canUndo ?? false))
                Button(redoTitle) { store.redo() }
                    .keyboardShortcut("z", modifiers: [.command, .shift])
                    .disabled(!(store.snapshot?.undo.canRedo ?? false))
            }
            CommandMenu("Project") {
                Group {
                    Button("Import Footage…") { store.chooseFolderToImport() }
                        .keyboardShortcut("i", modifiers: [.command, .shift])
                    Button("Check Files on Disk") { store.refreshFromDisk() }
                        .keyboardShortcut("r")
                    Divider()
                    Button("New Folder") { store.createFolder() }
                        .keyboardShortcut("n")
                    Divider()
                    Button("Organize Footage…") { store.sheet = .organize }
                        .keyboardShortcut("o", modifiers: [.command, .option])
                    Button("Review Changes…") { store.sheet = .review }
                        .keyboardShortcut("r", modifiers: [.command, .shift])
                }
                .disabled(!store.hasProject)
            }
        }
    }

    private var undoTitle: String {
        let d = store.snapshot?.undo.undoDescription ?? ""
        return d.isEmpty ? "Undo" : "Undo \(d)"
    }

    private var redoTitle: String {
        let d = store.snapshot?.undo.redoDescription ?? ""
        return d.isEmpty ? "Redo" : "Redo \(d)"
    }
}
