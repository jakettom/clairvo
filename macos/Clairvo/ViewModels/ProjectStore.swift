import AppKit
import Foundation
import SwiftUI

struct ImportProgress: Equatable {
    var phase: String
    var completed: Int
    var total: Int
}

struct AlertItem: Identifiable {
    let id = UUID()
    let title: String
    let message: String
}

struct SearchMatches {
    var clips: Set<String> = []
    var folders: Set<String> = []
    /// Folders that must stay visible to reveal a match (ancestors included).
    var visibleFolders: Set<String> = []
}

enum CenterMode: String, CaseIterable, Identifiable {
    case canvas = "Canvas"
    case clips = "Clips"
    var id: String { rawValue }
}

enum ActiveSheet: Identifiable {
    case newProject(URL)
    case organize
    case review
    case applyResult(ApplyResultDTO)
    case recovery([ApplyOperationDTO])

    var id: String {
        switch self {
        case .newProject: return "new"
        case .organize: return "organize"
        case .review: return "review"
        case .applyResult: return "result"
        case .recovery: return "recovery"
        }
    }
}

/// UI state for the open project. All mutations go through the C++ core via
/// CoreSession commands; this object only mirrors the core's snapshot.
@MainActor
final class ProjectStore: ObservableObject {
    static let shared = ProjectStore()

    @Published private(set) var session: CoreSession?
    @Published private(set) var snapshot: Snapshot?
    @Published private(set) var index = ProjectIndex()
    @Published var selection: Set<NodeID> = []
    @Published var expanded: Set<String> = []
    @Published var searchText = "" { didSet { updateSearch() } }
    @Published private(set) var searchMatches: SearchMatches?
    @Published var importProgress: ImportProgress?
    @Published var alert: AlertItem?
    @Published var notice: String?
    @Published var sheet: ActiveSheet?
    @Published var deleteRequest: FolderDTO?
    @Published var centerMode: CenterMode = .canvas
    @Published private(set) var isApplying = false
    @Published var renameRequest: NodeID?
    /// Bumped whenever the hierarchy or search filter changes (AppKit views rebuild on it).
    @Published private(set) var revision = 0

    private var needsReload = false

    // MARK: - Project lifecycle

    var hasProject: Bool { session != nil }
    var projectName: String { snapshot?.project.name ?? "Clairvo" }

    func chooseFolderForNewProject() {
        let panel = NSOpenPanel()
        panel.canChooseDirectories = true
        panel.canChooseFiles = false
        panel.allowsMultipleSelection = false
        panel.prompt = "Choose Footage Folder"
        panel.message = "Choose the root folder of your footage. The project file will be created inside it."
        if panel.runModal() == .OK, let url = panel.url { sheet = .newProject(url) }
    }

    func chooseProjectToOpen() {
        let panel = NSOpenPanel()
        panel.canChooseFiles = true
        panel.canChooseDirectories = false
        panel.allowsMultipleSelection = false
        panel.allowedContentTypes = [.init(filenameExtension: "project") ?? .data]
        panel.prompt = "Open Project"
        if panel.runModal() == .OK, let url = panel.url { openProject(url) }
    }

    func createProject(root: URL, name: String) {
        do {
            let s = try CoreSession.create(root: root, name: name)
            attach(s)
            startImport(nil) // initial scan of the footage root
        } catch {
            show(error, title: "Could Not Create Project")
        }
    }

    func openProject(_ url: URL) {
        do {
            let s = try CoreSession.open(projectFile: url)
            attach(s)
            NSDocumentController.shared.noteNewRecentDocumentURL(url)
            if let report = try? s.call("recoveryReport", as: RecoveryReportDTO.self), !report.operations.isEmpty {
                sheet = .recovery(report.operations)
            }
        } catch {
            show(error, title: "Could Not Open Project")
        }
    }

    func closeProject() {
        session?.setEventHandler(nil)
        session = nil
        snapshot = nil
        index = ProjectIndex()
        selection = []
        expanded = []
        searchText = ""
        importProgress = nil
    }

    private func attach(_ s: CoreSession) {
        closeProject()
        session = s
        s.setEventHandler { [weak self] event in
            DispatchQueue.main.async { self?.handle(event) }
        }
        needsReload = true
        reloadIfNeeded()
        expanded = [index.rootId]
        selection = []
        if let url = snapshot.map({ URL(fileURLWithPath: $0.project.projectFile) }) {
            NSDocumentController.shared.noteNewRecentDocumentURL(url)
        }
    }

    // MARK: - Events / reloading

    private func handle(_ event: CoreEvent) {
        switch event.type {
        case "modelChanged":
            needsReload = true
            DispatchQueue.main.async { [weak self] in self?.reloadIfNeeded() }
        case "importProgress":
            importProgress = ImportProgress(phase: event.payload["phase"] as? String ?? "",
                                            completed: event.payload["completed"] as? Int ?? 0,
                                            total: event.payload["total"] as? Int ?? 0)
        case "importFinished":
            importProgress = nil
            needsReload = true
            reloadIfNeeded()
            if let error = event.payload["error"] as? String {
                alert = AlertItem(title: "Import Failed", message: error)
            } else if (event.payload["cancelled"] as? Bool) == true {
                notice = "Import cancelled."
            } else {
                let added = event.payload["added"] as? Int ?? 0
                let skipped = event.payload["skippedUnsupported"] as? Int ?? 0
                var text = "Imported \(added) clip\(added == 1 ? "" : "s")."
                if skipped > 0 { text += " \(skipped) unsupported file\(skipped == 1 ? " was" : "s were") skipped." }
                if let errors = event.payload["errors"] as? [String], !errors.isEmpty {
                    text += " " + errors.joined(separator: " ")
                }
                notice = text
            }
        default:
            break
        }
    }

    func reloadIfNeeded() {
        guard needsReload, let session else { return }
        needsReload = false
        do {
            let snap = try session.call("snapshot", as: Snapshot.self)
            snapshot = snap
            index = ProjectIndex(snap)
            selection = selection.filter { node in
                switch node {
                case .folder(let id): return index.folders[id] != nil
                case .clip(let id): return index.clips[id] != nil
                }
            }
            expanded = expanded.filter { index.folders[$0] != nil }
            if expanded.isEmpty { expanded = [index.rootId] }
            updateSearch()
            revision += 1
        } catch {
            show(error)
        }
    }

    private func forceReload() {
        needsReload = true
        reloadIfNeeded()
    }

    // MARK: - Command helpers

    @discardableResult
    private func perform(_ command: String, _ args: [String: Any] = [:]) -> Bool {
        guard let session else { return false }
        do {
            try session.run(command, args)
            forceReload()
            return true
        } catch {
            show(error)
            return false
        }
    }

    func show(_ error: Error, title: String = "Clairvo") {
        alert = AlertItem(title: title, message: error.localizedDescription)
    }

    var selectedClipIds: [String] { selection.compactMap(\.clipId).sorted() }
    var selectedFolderIds: [String] { selection.compactMap(\.folderId).sorted() }

    /// Folder that new items go into: the selected folder, or the folder of the selected clip.
    var targetFolderId: String {
        if let first = selection.first, let folder = index.folderOf(first) { return folder }
        return index.rootId
    }

    // MARK: - Editing commands

    func createFolder(in parentId: String? = nil) {
        guard let session else { return }
        let parent = parentId ?? targetFolderId
        do {
            struct R: Decodable { let folderId: String }
            let r = try session.call("createFolder", ["parentId": parent, "name": "untitled folder"], as: R.self)
            forceReload()
            expanded.insert(parent)
            selection = [.folder(r.folderId)]
            renameRequest = .folder(r.folderId)
        } catch {
            show(error)
        }
    }

    func rename(_ node: NodeID, to name: String) {
        switch node {
        case .folder(let id):
            guard index.folders[id]?.name != name else { return }
            perform("renameFolder", ["folderId": id, "name": name])
        case .clip(let id):
            guard let clip = index.clips[id], clip.name != name, clip.title != name else { return }
            perform("renameClip", ["clipId": id, "title": name])
        }
    }

    func move(_ nodes: [NodeID], to folderId: String) {
        let clips = nodes.compactMap(\.clipId)
        let folders = nodes.compactMap(\.folderId).filter { $0 != folderId && !index.isAncestor($0, of: folderId) }
        let movingClips = clips.filter { index.clips[$0]?.folderId != folderId }
        guard !movingClips.isEmpty || !folders.isEmpty else { return }
        if perform("moveItems", ["clipIds": movingClips, "folderIds": folders, "destinationId": folderId]) {
            expanded.insert(folderId)
        }
    }

    func canMove(_ nodes: [NodeID], to folderId: String) -> Bool {
        for node in nodes {
            if case .folder(let id) = node, index.isAncestor(id, of: folderId) { return false }
        }
        return index.folders[folderId] != nil
    }

    func requestDelete(folderId: String) {
        guard let folder = index.folders[folderId], !folder.isRoot else { return }
        if (index.subtreeClipCount[folderId] ?? 0) == 0 {
            deleteFolder(folderId, removeClips: false)
        } else {
            deleteRequest = folder
        }
    }

    func deleteFolder(_ id: String, removeClips: Bool) {
        perform("deleteFolder", ["folderId": id, "behavior": removeClips ? "removeClips" : "moveToParent"])
    }

    func removeFromProject(_ clipIds: [String]) {
        guard !clipIds.isEmpty else { return }
        perform("removeClips", ["clipIds": clipIds])
    }

    func addTag(named name: String, to clipIds: [String]) {
        let trimmed = name.trimmingCharacters(in: .whitespaces)
        guard !trimmed.isEmpty, !clipIds.isEmpty else { return }
        perform("addTag", ["clipIds": clipIds, "name": trimmed])
    }

    func removeTag(_ tagId: String, from clipIds: [String]) {
        perform("removeTag", ["clipIds": clipIds, "tagId": tagId])
    }

    func deleteTag(_ tagId: String) { perform("deleteTag", ["tagId": tagId]) }

    func resetToAutomatic(_ clipIds: [String]) {
        guard !clipIds.isEmpty else { return }
        perform("resetToAutomatic", ["clipIds": clipIds])
    }

    func organize(criteria: [String], template: String) -> Bool {
        perform("organize", ["criteria": criteria, "namingTemplate": template])
    }

    func applyNaming(template: String) -> Bool { perform("applyNaming", ["namingTemplate": template]) }

    func resetOrganization() { perform("resetOrganization") }

    func undo() { perform("undo") }
    func redo() { perform("redo") }

    func refreshFromDisk() {
        guard let session else { return }
        do {
            struct R: Decodable { let newlyMissing: [String]; let recovered: [String] }
            let r = try session.call("refresh", as: R.self)
            forceReload()
            notice = "Checked files on disk: \(r.newlyMissing.count) newly missing, \(r.recovered.count) found again."
        } catch {
            show(error)
        }
    }

    func chooseFolderToImport() {
        guard let snapshot else { return }
        let panel = NSOpenPanel()
        panel.canChooseDirectories = true
        panel.canChooseFiles = false
        panel.directoryURL = URL(fileURLWithPath: snapshot.project.rootPath)
        panel.prompt = "Import"
        panel.message = "Choose a folder inside the project root to scan for new footage."
        if panel.runModal() == .OK, let url = panel.url { startImport(url) }
    }

    func startImport(_ url: URL?) {
        guard let session else { return }
        do {
            var args: [String: Any] = [:]
            if let url { args["path"] = url.path }
            try session.run("startImport", args)
            importProgress = ImportProgress(phase: "scanning", completed: 0, total: 0)
        } catch {
            show(error, title: "Could Not Import")
        }
    }

    func cancelImport() { _ = try? session?.run("cancelImport") }

    // MARK: - Queries

    func categoryStats() -> [CategoryStat] { (try? session?.call("categoryStats", as: [CategoryStat].self)) ?? [] }

    func previewNames(_ template: String) -> NamePreview? {
        try? session?.call("previewNames", ["namingTemplate": template], as: NamePreview.self)
    }

    func clipDetails(_ id: String) -> ClipDetails? { try? session?.call("clipDetails", ["clipId": id], as: ClipDetails.self) }

    func pendingChanges() async throws -> ChangeSetDTO {
        guard let session else { throw CoreError(message: "No project open") }
        return try await Task.detached { try session.call("pendingChanges", as: ChangeSetDTO.self) }.value
    }

    func preflight() async throws -> PreflightDTO {
        guard let session else { throw CoreError(message: "No project open") }
        let result = try await Task.detached { try session.call("preflight", as: PreflightDTO.self) }.value
        forceReload()
        return result
    }

    func apply() async {
        guard let session, !isApplying else { return }
        isApplying = true
        defer { isApplying = false }
        do {
            let result = try await Task.detached { try session.call("apply", as: ApplyResultDTO.self) }.value
            forceReload()
            sheet = .applyResult(result)
        } catch {
            forceReload()
            show(error, title: "Apply Failed")
        }
    }

    func revealInFinder(_ node: NodeID) {
        guard let session else { return }
        struct R: Decodable { let path: String; let exists: Bool }
        var args: [String: Any] = [:]
        switch node {
        case .clip(let id): args["clipId"] = id
        case .folder(let id): args["folderId"] = id
        }
        guard let r = try? session.call("absolutePath", args, as: R.self) else { return }
        guard r.exists else {
            alert = AlertItem(title: "Not on Disk Yet",
                              message: "This item does not exist on disk (it is missing or will be created when you Apply).")
            return
        }
        NSWorkspace.shared.activateFileViewerSelecting([URL(fileURLWithPath: r.path)])
    }

    // MARK: - Search

    private func updateSearch() {
        let query = searchText.trimmingCharacters(in: .whitespaces)
        guard !query.isEmpty, let session else {
            if searchMatches != nil {
                searchMatches = nil
                revision += 1
            }
            return
        }
        guard let r = try? session.call("search", ["query": query], as: SearchResultDTO.self) else { return }
        var m = SearchMatches(clips: Set(r.clipIds), folders: Set(r.folderIds))
        func reveal(_ folder: String?) {
            var current = folder
            while let id = current, !m.visibleFolders.contains(id) {
                m.visibleFolders.insert(id)
                current = index.folders[id].flatMap { $0.parentId.isEmpty ? nil : $0.parentId }
            }
        }
        for id in r.clipIds { reveal(index.clips[id]?.folderId) }
        for id in r.folderIds { reveal(id) }
        m.visibleFolders.insert(index.rootId)
        searchMatches = m
        expanded.formUnion(m.visibleFolders)
        revision += 1
    }

    func isVisible(folder id: String) -> Bool { searchMatches?.visibleFolders.contains(id) ?? true }
    func isVisible(clip id: String) -> Bool {
        guard let m = searchMatches else { return true }
        return m.clips.contains(id) || index.clips[id].map { m.folders.contains($0.folderId) } ?? false
    }
}
