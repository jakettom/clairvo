import SwiftUI

/// Spatial view of the same hierarchy (spec §35). It is not a second model:
/// nodes come from the shared ProjectIndex and edits go through ProjectStore.
struct CanvasView: View {
    @EnvironmentObject private var store: ProjectStore
    @State private var zoom: CGFloat = 1.0
    @GestureState private var pinch: CGFloat = 1.0
    @State private var dropTarget: String?

    static let nodeSize = CGSize(width: 168, height: 46)
    static let hGap: CGFloat = 18
    static let vGap: CGFloat = 54
    static let maxClipsPerFolder = 60

    struct LayoutNode: Identifiable {
        let id: NodeID
        var position: CGPoint
        var parent: NodeID?
        var overflow = 0 // "+N more" pseudo node count
    }

    struct Layout {
        var nodes: [LayoutNode] = []
        var size: CGSize = .zero
        var positions: [NodeID: CGPoint] = [:]
    }

    var body: some View {
        let layout = computeLayout()
        let scale = zoom * pinch
        GeometryReader { geo in
            ScrollView([.horizontal, .vertical]) {
                ZStack(alignment: .topLeading) {
                    Canvas { context, _ in
                        for node in layout.nodes {
                            guard let parent = node.parent, let p = layout.positions[parent] else { continue }
                            var path = Path()
                            let start = CGPoint(x: p.x, y: p.y + Self.nodeSize.height / 2)
                            let end = CGPoint(x: node.position.x, y: node.position.y - Self.nodeSize.height / 2)
                            let midY = (start.y + end.y) / 2
                            path.move(to: start)
                            path.addLine(to: CGPoint(x: start.x, y: midY))
                            path.addLine(to: CGPoint(x: end.x, y: midY))
                            path.addLine(to: end)
                            context.stroke(path, with: .color(.secondary.opacity(0.45)), lineWidth: 1)
                        }
                    }
                    .frame(width: layout.size.width, height: layout.size.height)

                    ForEach(layout.nodes) { node in
                        nodeView(node)
                            .position(node.position)
                    }
                }
                .frame(width: layout.size.width, height: layout.size.height)
                .scaleEffect(scale, anchor: .topLeading)
                .frame(width: layout.size.width * scale, height: layout.size.height * scale, alignment: .topLeading)
                .frame(minWidth: geo.size.width, minHeight: geo.size.height, alignment: .topLeading)
                .contentShape(Rectangle())
                .onTapGesture { store.selection = [] }
            }
        }
        .gesture(MagnificationGesture().updating($pinch) { value, state, _ in state = value }
            .onEnded { value in zoom = min(max(zoom * value, 0.25), 2.5) })
        .overlay(alignment: .bottomTrailing) { zoomControls }
        .background(Color(nsColor: .underPageBackgroundColor))
    }

    private var zoomControls: some View {
        HStack(spacing: 8) {
            Button { zoom = max(zoom / 1.25, 0.25) } label: { Image(systemName: "minus.magnifyingglass") }
            Text("\(Int(zoom * 100))%").monospacedDigit().frame(width: 44)
            Button { zoom = min(zoom * 1.25, 2.5) } label: { Image(systemName: "plus.magnifyingglass") }
            Button { zoom = 1 } label: { Image(systemName: "1.magnifyingglass") }
        }
        .buttonStyle(.borderless)
        .padding(8)
        .background(.regularMaterial, in: RoundedRectangle(cornerRadius: 8))
        .padding(12)
    }

    // MARK: Layout (tidy tree: leaves left-to-right, parents centered above children)

    private func computeLayout() -> Layout {
        let index = store.index
        guard !index.rootId.isEmpty else { return Layout() }
        var layout = Layout()
        var nextX: CGFloat = Self.hGap
        var maxDepth = 0

        func place(_ node: NodeID, depth: Int, parent: NodeID?) -> CGFloat {
            maxDepth = max(maxDepth, depth)
            let y = CGFloat(depth) * (Self.nodeSize.height + Self.vGap) + Self.nodeSize.height / 2 + 20
            var childXs: [CGFloat] = []
            var overflow = 0
            if case .folder(let id) = node, store.expanded.contains(id) {
                for child in index.childFolders[id] ?? [] where store.isVisible(folder: child) {
                    childXs.append(place(.folder(child), depth: depth + 1, parent: node))
                }
                let clips = (index.folderClips[id] ?? []).filter { store.isVisible(clip: $0) }
                for clip in clips.prefix(Self.maxClipsPerFolder) {
                    childXs.append(place(.clip(clip), depth: depth + 1, parent: node))
                }
                overflow = max(0, clips.count - Self.maxClipsPerFolder)
            }
            let x: CGFloat
            if childXs.isEmpty {
                x = nextX + Self.nodeSize.width / 2
                nextX += Self.nodeSize.width + Self.hGap
            } else {
                // The root sits above its first child so it is visible at the scroll origin;
                // other folders are centered over their children.
                x = parent == nil ? childXs.first! : (childXs.first! + childXs.last!) / 2
            }
            let pos = CGPoint(x: x, y: y)
            layout.nodes.append(LayoutNode(id: node, position: pos, parent: parent, overflow: overflow))
            layout.positions[node] = pos
            return x
        }

        _ = place(.folder(index.rootId), depth: 0, parent: nil)
        layout.size = CGSize(width: max(nextX, 400),
                             height: CGFloat(maxDepth + 1) * (Self.nodeSize.height + Self.vGap) + 60)
        return layout
    }

    // MARK: Nodes

    @ViewBuilder
    private func nodeView(_ node: LayoutNode) -> some View {
        switch node.id {
        case .folder(let id):
            if let folder = store.index.folders[id] {
                folderNode(folder, overflow: node.overflow)
            }
        case .clip(let id):
            if let clip = store.index.clips[id] {
                clipNode(clip)
            }
        }
    }

    private func folderNode(_ folder: FolderDTO, overflow: Int) -> some View {
        let selected = store.selection.contains(.folder(folder.id))
        let expanded = store.expanded.contains(folder.id)
        let count = store.index.subtreeClipCount[folder.id] ?? 0
        return HStack(spacing: 8) {
            Image(systemName: folder.isRoot ? "film.stack" : (expanded ? "folder.fill" : "folder"))
                .foregroundStyle(folder.origin == "AUTOMATIC" ? Color.teal : Color.accentColor)
            VStack(alignment: .leading, spacing: 1) {
                Text(folder.name).font(.callout.weight(.semibold)).lineLimit(1)
                HStack(spacing: 4) {
                    Text("\(count) clip\(count == 1 ? "" : "s")")
                    if overflow > 0 { Text("· +\(overflow) more") }
                    if !folder.isRoot && !folder.existsOnDisk { Text("· new").foregroundStyle(.green) }
                }
                .font(.caption2)
                .foregroundStyle(.secondary)
            }
            Spacer(minLength: 0)
            if (store.index.childFolders[folder.id]?.isEmpty == false) || (store.index.folderClips[folder.id]?.isEmpty == false) {
                Image(systemName: expanded ? "chevron.up" : "chevron.down").font(.caption2).foregroundStyle(.secondary)
            }
        }
        .padding(.horizontal, 10)
        .frame(width: Self.nodeSize.width, height: Self.nodeSize.height)
        .background(RoundedRectangle(cornerRadius: 9).fill(Color(nsColor: .controlBackgroundColor)))
        .overlay(RoundedRectangle(cornerRadius: 9)
            .stroke(dropTarget == folder.id ? Color.green : (selected ? Color.accentColor : Color.primary.opacity(0.15)),
                    style: StrokeStyle(lineWidth: selected || dropTarget == folder.id ? 2 : 1,
                                       dash: folder.existsOnDisk || folder.isRoot ? [] : [4, 3])))
        .contentShape(Rectangle())
        .onTapGesture(count: 2) { toggle(folder.id) }
        .onTapGesture { select(.folder(folder.id)) }
        .modifier(DraggableIf(enabled: !folder.isRoot, token: NodeID.folder(folder.id).token))
        .dropDestination(for: String.self) { tokens, _ in
            let nodes = tokens.compactMap(NodeID.init(token:))
            guard store.canMove(nodes, to: folder.id) else { return false }
            store.move(nodes, to: folder.id)
            return true
        } isTargeted: { targeted in
            dropTarget = targeted ? folder.id : (dropTarget == folder.id ? nil : dropTarget)
        }
        .contextMenu {
            Button("New Folder") { store.createFolder(in: folder.id) }
            if !folder.isRoot {
                Button("Rename…") {
                    if let name = promptForText(title: "Rename Folder", message: "New name:", initial: folder.name) {
                        store.rename(.folder(folder.id), to: name)
                    }
                }
                Button("Delete…") { store.requestDelete(folderId: folder.id) }
            }
            Divider()
            Button(expanded ? "Collapse" : "Expand") { toggle(folder.id) }
            Button("Reveal in Finder") { store.revealInFinder(.folder(folder.id)) }
        }
        .help(folder.isRoot ? folder.name : folder.path)
    }

    private func clipNode(_ clip: ClipDTO) -> some View {
        let selected = store.selection.contains(.clip(clip.id))
        let pending = !clip.isMissing && store.index.hasPendingChange(clip)
        return HStack(spacing: 6) {
            Image(systemName: clip.isMissing ? "exclamationmark.triangle.fill" : "film")
                .foregroundStyle(clip.isMissing ? Color.orange : Color.secondary)
            VStack(alignment: .leading, spacing: 1) {
                Text(clip.name).font(.caption.weight(.medium)).lineLimit(1).truncationMode(.middle)
                Text(clip.display["camera"] ?? clip.originalFilename).font(.caption2).foregroundStyle(.secondary).lineLimit(1)
            }
            Spacer(minLength: 0)
            if clip.isManual { Image(systemName: "hand.raised.fill").font(.caption2).foregroundStyle(.purple) }
            if pending { Image(systemName: "arrow.right.circle.fill").font(.caption2).foregroundStyle(.blue) }
        }
        .padding(.horizontal, 8)
        .frame(width: Self.nodeSize.width, height: Self.nodeSize.height - 8)
        .background(RoundedRectangle(cornerRadius: 7).fill(Color(nsColor: .textBackgroundColor).opacity(0.6)))
        .overlay(RoundedRectangle(cornerRadius: 7).stroke(selected ? Color.accentColor : Color.primary.opacity(0.1),
                                                          lineWidth: selected ? 2 : 1))
        .contentShape(Rectangle())
        .onTapGesture { select(.clip(clip.id)) }
        .draggable(dragToken(for: .clip(clip.id)))
        .contextMenu { ClipContextMenu(clipIds: selectedClips(including: clip.id)) }
        .help(clip.filePath)
    }

    private func toggle(_ folderId: String) {
        if store.expanded.contains(folderId) { store.expanded.remove(folderId) } else { store.expanded.insert(folderId) }
    }

    private func select(_ node: NodeID) {
        if NSEvent.modifierFlags.contains(.command) {
            if store.selection.contains(node) { store.selection.remove(node) } else { store.selection.insert(node) }
        } else {
            store.selection = [node]
        }
    }

    private func selectedClips(including id: String) -> [String] {
        let ids = store.selectedClipIds
        return ids.contains(id) ? ids : [id]
    }

    /// Dragging a selected node drags only that node; multi-node drag is done via the tree.
    private func dragToken(for node: NodeID) -> String { node.token }
}

private struct DraggableIf: ViewModifier {
    let enabled: Bool
    let token: String
    func body(content: Content) -> some View {
        if enabled { content.draggable(token) } else { content }
    }
}
