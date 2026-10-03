import SwiftUI

struct MainView: View {
    @EnvironmentObject private var store: ProjectStore
    @State private var columnVisibility = NavigationSplitViewVisibility.all

    var body: some View {
        NavigationSplitView(columnVisibility: $columnVisibility) {
            TreeSidebar()
                .navigationSplitViewColumnWidth(min: 220, ideal: 280, max: 420)
        } content: {
            CenterView()
                .navigationSplitViewColumnWidth(min: 380, ideal: 640)
        } detail: {
            InspectorView()
                .navigationSplitViewColumnWidth(min: 260, ideal: 320, max: 460)
        }
        .navigationTitle(store.projectName)
        .navigationSubtitle(subtitle)
        .searchable(text: $store.searchText, placement: .toolbar, prompt: "Search titles, tags, metadata")
        .toolbar { toolbarContent }
        .overlay {
            if let progress = store.importProgress {
                ImportProgressView(progress: progress) { store.cancelImport() }
            }
        }
        .overlay(alignment: .bottom) {
            if let notice = store.notice {
                NoticeBanner(text: notice) { store.notice = nil }
                    .padding(.bottom, 16)
                    .transition(.move(edge: .bottom).combined(with: .opacity))
            }
        }
        .animation(.easeInOut(duration: 0.2), value: store.notice)
        .confirmationDialog(deleteTitle, isPresented: deleteBinding, titleVisibility: .visible, presenting: store.deleteRequest) { folder in
            Button("Move Clips to Parent") { store.deleteFolder(folder.id, removeClips: false) }
            Button("Remove Clips from Project") { store.deleteFolder(folder.id, removeClips: true) }
            Button("Cancel", role: .cancel) {}
        } message: { folder in
            let n = store.index.subtreeClipCount[folder.id] ?? 0
            Text("This folder contains \(n) clip\(n == 1 ? "" : "s"). Removing clips from the project never deletes the video files on disk.")
        }
        .disabled(store.importProgress != nil)
    }

    private var subtitle: String {
        guard let s = store.snapshot else { return "" }
        var parts = ["\(s.clips.count) clips"]
        if s.pendingChangeCount > 0 { parts.append("\(s.pendingChangeCount) pending changes") }
        if s.missingCount > 0 { parts.append("\(s.missingCount) missing") }
        return parts.joined(separator: " · ")
    }

    private var deleteTitle: String { "Delete folder “\(store.deleteRequest?.name ?? "")”?" }

    private var deleteBinding: Binding<Bool> {
        Binding(get: { store.deleteRequest != nil }, set: { if !$0 { store.deleteRequest = nil } })
    }

    @ToolbarContentBuilder
    private var toolbarContent: some ToolbarContent {
        ToolbarItemGroup(placement: .navigation) {
            Menu {
                Button("New Project…") { store.chooseFolderForNewProject() }
                Button("Open Project…") { store.chooseProjectToOpen() }
                Divider()
                Button("Close Project") { store.closeProject() }
            } label: {
                Label("Project", systemImage: "folder")
            }
            .help("Project")
        }
        ToolbarItemGroup(placement: .primaryAction) {
            Button { store.chooseFolderToImport() } label: { Label("Import", systemImage: "square.and.arrow.down") }
                .help("Scan a folder inside the project root for new footage")
            Button { store.createFolder() } label: { Label("New Folder", systemImage: "folder.badge.plus") }
                .help("New folder")
            Button { store.sheet = .organize } label: { Label("Organize", systemImage: "wand.and.stars") }
                .help("Organize footage by metadata")
            Button { store.undo() } label: { Label("Undo", systemImage: "arrow.uturn.backward") }
                .help(store.snapshot?.undo.undoDescription.isEmpty == false ? "Undo \(store.snapshot!.undo.undoDescription)" : "Undo")
                .disabled(!(store.snapshot?.undo.canUndo ?? false))
            Button { store.refreshFromDisk() } label: { Label("Check Files", systemImage: "arrow.clockwise") }
                .help("Check files on disk and mark missing clips")
            Button { store.sheet = .review } label: {
                Label("Review", systemImage: "checklist")
                    .overlay(alignment: .topTrailing) {
                        if let n = store.snapshot?.pendingChangeCount, n > 0 { CountBadge(count: n).offset(x: 10, y: -8) }
                    }
            }
            .help("Review pending changes and apply them to disk")
        }
    }
}

struct TreeSidebar: View {
    @EnvironmentObject private var store: ProjectStore

    var body: some View {
        TreeView()
            .safeAreaInset(edge: .bottom) {
                HStack(spacing: 12) {
                    Button { store.createFolder() } label: { Image(systemName: "plus") }
                        .help("New folder")
                    Button {
                        if let id = store.selectedFolderIds.first { store.requestDelete(folderId: id) }
                    } label: { Image(systemName: "minus") }
                        .help("Delete folder")
                        .disabled(store.selectedFolderIds.first.map { $0 == store.index.rootId } ?? true)
                    Spacer()
                    if let s = store.snapshot, s.missingCount > 0 {
                        Label("\(s.missingCount)", systemImage: "exclamationmark.triangle.fill")
                            .foregroundStyle(.orange)
                            .help("\(s.missingCount) missing clips")
                    }
                }
                .buttonStyle(.borderless)
                .padding(.horizontal, 10)
                .padding(.vertical, 6)
                .background(.bar)
            }
    }
}

struct CenterView: View {
    @EnvironmentObject private var store: ProjectStore

    var body: some View {
        VStack(spacing: 0) {
            HStack {
                Picker("View", selection: $store.centerMode) {
                    ForEach(CenterMode.allCases) { mode in Text(mode.rawValue).tag(mode) }
                }
                .pickerStyle(.segmented)
                .labelsHidden()
                .frame(width: 180)
                Spacer()
                if store.searchMatches != nil {
                    Text("\(store.searchMatches?.clips.count ?? 0) matching clips")
                        .font(.caption)
                        .foregroundStyle(.secondary)
                }
            }
            .padding(.horizontal, 12)
            .padding(.vertical, 8)
            Divider()
            switch store.centerMode {
            case .canvas: CanvasView()
            case .clips: ClipTableView()
            }
        }
    }
}
