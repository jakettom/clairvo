import SwiftUI

/// Tabular list of the clips in the selected folder (or all search matches).
struct ClipTableView: View {
    @EnvironmentObject private var store: ProjectStore
    @State private var sortOrder = [KeyPathComparator(\ClipRow.name, comparator: .localizedStandard)]

    struct ClipRow: Identifiable {
        let id: String
        let name: String
        let original: String
        let camera: String
        let resolution: String
        let frameRate: String
        let duration: String
        let codec: String
        let status: String
        let tags: String
        let manual: Bool
        let pending: Bool
    }

    private var scopeFolder: String {
        if let first = store.selection.first, let folder = store.index.folderOf(first) { return folder }
        return store.index.rootId
    }

    private var rows: [ClipRow] {
        let index = store.index
        let ids: [String]
        if let matches = store.searchMatches {
            ids = Array(matches.clips)
        } else {
            ids = index.folderClips[scopeFolder] ?? []
        }
        return ids.compactMap { id in
            guard let c = index.clips[id] else { return nil }
            return ClipRow(id: id, name: c.name, original: c.originalFilename,
                           camera: c.display["camera"] ?? "—", resolution: c.display["resolution"] ?? "—",
                           frameRate: c.display["frame_rate"] ?? "—", duration: c.display["duration"] ?? "—",
                           codec: c.display["codec"] ?? "—", status: c.isMissing ? "Missing" : "Available",
                           tags: c.tagIds.compactMap { index.tags[$0]?.name }.sorted().joined(separator: ", "),
                           manual: c.isManual, pending: !c.isMissing && index.hasPendingChange(c))
        }.sorted(using: sortOrder)
    }

    private var tableSelection: Binding<Set<String>> {
        Binding(
            get: { Set(store.selection.compactMap(\.clipId)) },
            set: { ids in
                if ids.isEmpty { return }
                store.selection = Set(ids.map { NodeID.clip($0) })
            })
    }

    var body: some View {
        VStack(spacing: 0) {
            HStack {
                Image(systemName: store.searchMatches == nil ? "folder" : "magnifyingglass")
                Text(store.searchMatches == nil ? store.index.displayPath(folderId: scopeFolder) : "Search results")
                    .lineLimit(1)
                    .truncationMode(.middle)
                Spacer()
                Text("\(rows.count) clips").foregroundStyle(.secondary)
            }
            .font(.callout)
            .padding(.horizontal, 12)
            .padding(.vertical, 6)
            Table(rows, selection: tableSelection, sortOrder: $sortOrder) {
                TableColumn("Name", value: \.name, comparator: .localizedStandard) { row in
                    HStack(spacing: 4) {
                        Image(systemName: row.status == "Missing" ? "exclamationmark.triangle.fill" : "film")
                            .foregroundStyle(row.status == "Missing" ? .orange : .secondary)
                        Text(row.name)
                        if row.manual { Image(systemName: "hand.raised.fill").foregroundStyle(.purple).font(.caption2) }
                        if row.pending { Image(systemName: "arrow.right.circle.fill").foregroundStyle(.blue).font(.caption2) }
                    }
                    .draggable(NodeID.clip(row.id).token)
                }
                .width(min: 160, ideal: 220)
                TableColumn("Original", value: \.original, comparator: .localizedStandard)
                TableColumn("Camera", value: \.camera, comparator: .localizedStandard)
                TableColumn("Resolution", value: \.resolution, comparator: .localizedStandard)
                TableColumn("Frame Rate", value: \.frameRate, comparator: .localizedStandard)
                TableColumn("Duration", value: \.duration, comparator: .localizedStandard)
                TableColumn("Codec", value: \.codec, comparator: .localizedStandard)
                TableColumn("Tags", value: \.tags, comparator: .localizedStandard)
            }
            .contextMenu(forSelectionType: String.self) { ids in
                ClipContextMenu(clipIds: Array(ids))
            }
        }
    }
}

/// SwiftUI context menu used by the table and canvas (mirrors the tree menu).
struct ClipContextMenu: View {
    @EnvironmentObject private var store: ProjectStore
    let clipIds: [String]

    var body: some View {
        if !clipIds.isEmpty {
            if clipIds.count == 1 {
                Button("Rename…") {
                    if let clip = store.index.clips[clipIds[0]],
                       let name = promptForText(title: "Rename Clip", message: "New name for \(clip.name):", initial: clip.title) {
                        store.rename(.clip(clipIds[0]), to: name)
                    }
                }
            }
            Menu("Move to") {
                ForEach(store.index.allFoldersSorted()) { folder in
                    Button(folder.isRoot ? folder.name : folder.path) {
                        store.move(clipIds.map { .clip($0) }, to: folder.id)
                    }
                }
            }
            Menu("Add Tag") {
                ForEach(store.index.sortedTags) { tag in
                    Button(tag.name) { store.addTag(named: tag.name, to: clipIds) }
                }
                Divider()
                Button("New Tag…") {
                    if let name = promptForText(title: "New Tag", message: "Tag \(clipIds.count) clip(s) with:") {
                        store.addTag(named: name, to: clipIds)
                    }
                }
            }
            let assigned = Set(clipIds.flatMap { store.index.clips[$0]?.tagIds ?? [] })
            if !assigned.isEmpty {
                Menu("Remove Tag") {
                    ForEach(store.index.sortedTags.filter { assigned.contains($0.id) }) { tag in
                        Button(tag.name) { store.removeTag(tag.id, from: clipIds) }
                    }
                }
            }
            Divider()
            Button("Reset to Automatic Organization") { store.resetToAutomatic(clipIds) }
                .disabled(!clipIds.contains { store.index.clips[$0]?.isManual ?? false })
            Button("Remove from Project") { store.removeFromProject(clipIds) }
            if clipIds.count == 1 {
                Divider()
                Button("Reveal in Finder") { store.revealInFinder(.clip(clipIds[0])) }
            }
        }
    }
}
