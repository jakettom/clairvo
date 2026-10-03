import SwiftUI

/// Properties of the selection (spec §36). Editable organizational data is
/// kept visually separate from read-only camera/file metadata.
struct InspectorView: View {
    @EnvironmentObject private var store: ProjectStore

    var body: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 18) {
                let selection = store.selection
                if selection.isEmpty {
                    ProjectSummary()
                } else if selection.count == 1, let node = selection.first {
                    switch node {
                    case .clip(let id):
                        if let clip = store.index.clips[id] { ClipInspector(clip: clip).id(id) }
                    case .folder(let id):
                        if let folder = store.index.folders[id] { FolderInspector(folder: folder).id(id) }
                    }
                } else {
                    MultiInspector()
                }
            }
            .padding(16)
            .frame(maxWidth: .infinity, alignment: .leading)
        }
        .background(Color(nsColor: .windowBackgroundColor))
    }
}

private struct ProjectSummary: View {
    @EnvironmentObject private var store: ProjectStore

    var body: some View {
        if let s = store.snapshot {
            InspectorSection("Project") {
                PropertyRow("Name", s.project.name)
                PropertyRow("Root folder", s.project.rootPath)
                PropertyRow("Project file", URL(fileURLWithPath: s.project.projectFile).lastPathComponent)
            }
            InspectorSection("Status") {
                PropertyRow("Clips", "\(s.clips.count)")
                PropertyRow("Folders", "\(max(s.folders.count - 1, 0))")
                PropertyRow("Tags", "\(s.tags.count)")
                PropertyRow("Pending changes", "\(s.pendingChangeCount)")
                PropertyRow("Missing clips", "\(s.missingCount)")
            }
            if !s.config.criteria.isEmpty {
                InspectorSection("Organization") {
                    PropertyRow("Criteria", s.config.criteria.compactMap { key in s.categories.first { $0.key == key }?.name }
                        .joined(separator: " → "))
                    PropertyRow("Naming", s.config.namingTemplate.isEmpty ? "Keep names" : s.config.namingTemplate)
                }
            }
            Text("Select a clip or folder to see its details.")
                .font(.callout)
                .foregroundStyle(.secondary)
        }
    }
}

private struct ClipInspector: View {
    @EnvironmentObject private var store: ProjectStore
    let clip: ClipDTO
    @State private var title = ""
    @State private var newTag = ""
    @State private var details: ClipDetails?
    @State private var showRaw = false

    var body: some View {
        InspectorSection("Clip", systemImage: "film") {
            VStack(alignment: .leading, spacing: 4) {
                Text("Title").font(.caption).foregroundStyle(.secondary)
                HStack(spacing: 4) {
                    TextField("Title", text: $title)
                        .textFieldStyle(.roundedBorder)
                        .onSubmit { commitTitle() }
                    Text(clip.extension).foregroundStyle(.secondary)
                }
            }
            PropertyRow("Original filename", clip.originalFilename)
            PropertyRow("Folder", store.index.displayPath(folderId: clip.folderId))
            HStack {
                Text("Status").foregroundStyle(.secondary)
                Spacer()
                StatusPill(missing: clip.isMissing)
            }
            .font(.callout)
            HStack {
                Text("Organization").foregroundStyle(.secondary)
                Spacer()
                if clip.isManual {
                    Label("Manual", systemImage: "hand.raised.fill").foregroundStyle(.purple)
                } else {
                    Label("Automatic", systemImage: "wand.and.stars").foregroundStyle(.teal)
                }
            }
            .font(.callout)
            if clip.isManual {
                Button("Reset to Automatic Organization") { store.resetToAutomatic([clip.id]) }
                    .controlSize(.small)
            }
        }

        InspectorSection("Tags", systemImage: "tag") {
            FlowLayout(spacing: 6) {
                ForEach(clip.tagIds.compactMap { store.index.tags[$0] }.sorted { $0.name < $1.name }) { tag in
                    TagChip(name: tag.name) { store.removeTag(tag.id, from: [clip.id]) }
                }
            }
            TagEntryField(text: $newTag) { name in store.addTag(named: name, to: [clip.id]) }
        }

        InspectorSection("Metadata", systemImage: "lock", subtitle: "Read-only — extracted from the file") {
            ForEach(store.snapshot?.categories ?? []) { cat in
                PropertyRow(cat.name, clip.display[cat.key] ?? "—", dimmed: clip.display[cat.key] == nil)
            }
            DisclosureGroup("Raw metadata", isExpanded: $showRaw) {
                if let details {
                    if details.raw.isEmpty {
                        Text("No metadata could be read from this file.").font(.caption).foregroundStyle(.secondary)
                    }
                    ForEach(details.raw) { r in
                        VStack(alignment: .leading, spacing: 1) {
                            Text(r.key).font(.caption2.monospaced()).foregroundStyle(.secondary)
                            Text(r.value).font(.caption).textSelection(.enabled)
                        }
                        .padding(.vertical, 1)
                    }
                }
            }
            .font(.callout)
            .onChange(of: showRaw) { _, open in if open && details == nil { details = store.clipDetails(clip.id) } }
        }

        InspectorSection("File", systemImage: "doc") {
            PropertyRow("On disk", clip.filePath)
            if store.index.hasPendingChange(clip) {
                PropertyRow("After Apply", store.index.intendedPath(clip))
            }
            PropertyRow("Size", ByteCountFormatter.string(fromByteCount: clip.fileSize, countStyle: .file))
            Button("Reveal in Finder") { store.revealInFinder(.clip(clip.id)) }
                .controlSize(.small)
                .disabled(clip.isMissing)
        }
        .onAppear { title = clip.title }
        .onChange(of: clip.title) { _, newValue in title = newValue }
    }

    private func commitTitle() {
        let t = title.trimmingCharacters(in: .whitespaces)
        if t.isEmpty || t == clip.title { title = clip.title; return }
        store.rename(.clip(clip.id), to: t)
        title = store.index.clips[clip.id]?.title ?? clip.title
    }
}

private struct FolderInspector: View {
    @EnvironmentObject private var store: ProjectStore
    let folder: FolderDTO
    @State private var name = ""

    var body: some View {
        InspectorSection(folder.isRoot ? "Project Root" : "Folder", systemImage: folder.isRoot ? "film.stack" : "folder") {
            if folder.isRoot {
                PropertyRow("Name", folder.name)
                Text("The project root is never renamed or moved.").font(.caption).foregroundStyle(.secondary)
            } else {
                VStack(alignment: .leading, spacing: 4) {
                    Text("Name").font(.caption).foregroundStyle(.secondary)
                    TextField("Name", text: $name)
                        .textFieldStyle(.roundedBorder)
                        .onSubmit {
                            let n = name.trimmingCharacters(in: .whitespaces)
                            if n.isEmpty || n == folder.name { name = folder.name } else { store.rename(.folder(folder.id), to: n) }
                        }
                }
                PropertyRow("Path", folder.path)
                PropertyRow("Origin", folder.origin == "AUTOMATIC" ? "Automatic organization" :
                                folder.origin == "IMPORTED" ? "Imported from disk" : "Created manually")
            }
            PropertyRow("Clips (including subfolders)", "\(store.index.subtreeClipCount[folder.id] ?? 0)")
            PropertyRow("Subfolders", "\(store.index.childFolders[folder.id]?.count ?? 0)")
        }
        InspectorSection("On Disk", systemImage: "internaldrive") {
            if folder.isRoot {
                PropertyRow("Location", store.snapshot?.project.rootPath ?? "")
            } else if let physical = folder.physicalPath {
                PropertyRow("Directory", physical)
                if folder.hasPendingRename { PropertyRow("After Apply", folder.path) }
            } else {
                Label("Will be created when you Apply", systemImage: "plus.circle.fill").foregroundStyle(.green).font(.callout)
            }
        }
        HStack {
            Button("New Subfolder") { store.createFolder(in: folder.id) }
            if !folder.isRoot { Button("Delete…", role: .destructive) { store.requestDelete(folderId: folder.id) } }
        }
        .controlSize(.small)
        .onAppear { name = folder.name }
        .onChange(of: folder.name) { _, newValue in name = newValue }
    }
}

private struct MultiInspector: View {
    @EnvironmentObject private var store: ProjectStore
    @State private var newTag = ""

    var body: some View {
        let clips = store.selectedClipIds
        let folders = store.selectedFolderIds
        InspectorSection("Selection", systemImage: "square.stack.3d.up") {
            PropertyRow("Clips", "\(clips.count)")
            PropertyRow("Folders", "\(folders.count)")
        }
        if !clips.isEmpty {
            InspectorSection("Bulk Tagging", systemImage: "tag") {
                let shared = sharedTags(clips)
                if !shared.isEmpty {
                    FlowLayout(spacing: 6) {
                        ForEach(shared) { tag in TagChip(name: tag.name) { store.removeTag(tag.id, from: clips) } }
                    }
                }
                TagEntryField(text: $newTag) { name in store.addTag(named: name, to: clips) }
            }
        }
        InspectorSection("Actions", systemImage: "bolt") {
            Menu("Move to…") {
                ForEach(store.index.allFoldersSorted().filter { store.canMove(Array(store.selection), to: $0.id) }) { folder in
                    Button(folder.isRoot ? folder.name : folder.path) { store.move(Array(store.selection), to: folder.id) }
                }
            }
            if !clips.isEmpty {
                Button("Reset to Automatic Organization") { store.resetToAutomatic(clips) }
                Button("Remove \(clips.count) Clip\(clips.count == 1 ? "" : "s") from Project") { store.removeFromProject(clips) }
            }
        }
    }

    private func sharedTags(_ clips: [String]) -> [TagDTO] {
        guard let first = clips.first, var shared = store.index.clips[first].map({ Set($0.tagIds) }) else { return [] }
        for id in clips.dropFirst() { shared.formIntersection(store.index.clips[id]?.tagIds ?? []) }
        return store.index.sortedTags.filter { shared.contains($0.id) }
    }
}

private struct TagEntryField: View {
    @EnvironmentObject private var store: ProjectStore
    @Binding var text: String
    let onAdd: (String) -> Void

    var body: some View {
        HStack(spacing: 6) {
            TextField("Add tag", text: $text)
                .textFieldStyle(.roundedBorder)
                .onSubmit(add)
            Menu {
                ForEach(store.index.sortedTags) { tag in Button(tag.name) { onAdd(tag.name) } }
                if !store.index.sortedTags.isEmpty {
                    Divider()
                    Menu("Delete Tag from Project") {
                        ForEach(store.index.sortedTags) { tag in
                            Button("\(tag.name) (\(tag.count))") { store.deleteTag(tag.id) }
                        }
                    }
                }
            } label: {
                Image(systemName: "tag")
            }
            .menuStyle(.borderlessButton)
            .fixedSize()
            .disabled(store.index.sortedTags.isEmpty)
        }
    }

    private func add() {
        let t = text.trimmingCharacters(in: .whitespaces)
        guard !t.isEmpty else { return }
        onAdd(t)
        text = ""
    }
}
