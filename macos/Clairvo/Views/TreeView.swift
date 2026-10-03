import AppKit
import SwiftUI

extension NSPasteboard.PasteboardType {
    static let clairvoNode = NSPasteboard.PasteboardType("com.clairvo.node")
}

final class TreeNode: NSObject {
    let node: NodeID
    var children: [TreeNode] = []
    init(_ node: NodeID) { self.node = node }
}

/// Outline view with Return-to-rename and Delete-to-remove keyboard handling.
final class ClairvoOutlineView: NSOutlineView {
    var onReturn: (() -> Void)?
    var onDelete: (() -> Void)?

    override func keyDown(with event: NSEvent) {
        switch event.keyCode {
        case 36, 76: onReturn?()          // return / enter
        case 51, 117: onDelete?()         // delete / forward delete
        default: super.keyDown(with: event)
        }
    }
}

/// The authoritative hierarchy editor (spec §34). Backed by NSOutlineView for
/// virtualized rendering of large projects and native drag & drop.
struct TreeView: NSViewRepresentable {
    @EnvironmentObject private var store: ProjectStore

    func makeCoordinator() -> Coordinator { Coordinator(store: store) }

    func makeNSView(context: Context) -> NSScrollView {
        let outline = ClairvoOutlineView()
        let column = NSTableColumn(identifier: .init("name"))
        column.resizingMask = .autoresizingMask
        outline.addTableColumn(column)
        outline.outlineTableColumn = column
        outline.headerView = nil
        outline.style = .sourceList
        outline.rowSizeStyle = .default
        outline.allowsMultipleSelection = true
        outline.autoresizesOutlineColumn = true
        outline.floatsGroupRows = false
        outline.registerForDraggedTypes([.clairvoNode])
        outline.setDraggingSourceOperationMask(.move, forLocal: true)
        outline.draggingDestinationFeedbackStyle = .sourceList
        outline.dataSource = context.coordinator
        outline.delegate = context.coordinator
        outline.target = context.coordinator
        outline.doubleAction = #selector(Coordinator.doubleClicked(_:))
        let menu = NSMenu()
        menu.delegate = context.coordinator
        outline.menu = menu
        outline.onReturn = { [weak coordinator = context.coordinator] in coordinator?.renameSelected() }
        outline.onDelete = { [weak coordinator = context.coordinator] in coordinator?.deleteSelected() }

        let scroll = NSScrollView()
        scroll.documentView = outline
        scroll.hasVerticalScroller = true
        scroll.autohidesScrollers = true
        scroll.drawsBackground = false
        context.coordinator.outline = outline
        context.coordinator.sync()
        return scroll
    }

    func updateNSView(_ nsView: NSScrollView, context: Context) {
        context.coordinator.store = store
        context.coordinator.sync()
    }

    @MainActor
    final class Coordinator: NSObject, NSOutlineViewDataSource, NSOutlineViewDelegate, NSMenuDelegate, NSTextFieldDelegate {
        var store: ProjectStore
        weak var outline: ClairvoOutlineView?
        private var root: TreeNode?
        private var nodes: [NodeID: TreeNode] = [:]
        private var builtRevision = -1
        private var isSyncing = false

        init(store: ProjectStore) { self.store = store }

        // MARK: Sync with store

        func sync() {
            guard let outline else { return }
            if builtRevision != store.revision {
                builtRevision = store.revision
                rebuild()
            } else {
                applySelection()
            }
            if let request = store.renameRequest {
                DispatchQueue.main.async { [weak self] in
                    self?.store.renameRequest = nil
                    self?.beginRename(request)
                }
            }
            _ = outline
        }

        private func rebuild() {
            guard let outline else { return }
            let index = store.index
            nodes = [:]
            guard !index.rootId.isEmpty else {
                root = nil
                outline.reloadData()
                return
            }
            func build(_ folderId: String) -> TreeNode {
                let n = TreeNode(.folder(folderId))
                nodes[n.node] = n
                for child in index.childFolders[folderId] ?? [] where store.isVisible(folder: child) {
                    n.children.append(build(child))
                }
                let folderMatches = store.searchMatches?.folders.contains(folderId) ?? false
                for clip in index.folderClips[folderId] ?? [] where folderMatches || store.isVisible(clip: clip) {
                    let c = TreeNode(.clip(clip))
                    nodes[c.node] = c
                    n.children.append(c)
                }
                return n
            }
            root = build(index.rootId)
            isSyncing = true
            outline.reloadData()
            restoreExpansion(root!)
            isSyncing = false
            applySelection()
        }

        private func restoreExpansion(_ node: TreeNode) {
            guard let outline, let id = node.node.folderId, store.expanded.contains(id) else { return }
            outline.expandItem(node)
            for child in node.children where child.node.folderId != nil { restoreExpansion(child) }
        }

        private func applySelection() {
            guard let outline else { return }
            let current = Set(outline.selectedRowIndexes.compactMap { (outline.item(atRow: $0) as? TreeNode)?.node })
            guard current != store.selection else { return }
            isSyncing = true
            // Reveal selected items by expanding their ancestors.
            for node in store.selection {
                var folder = store.index.folderOf(node)
                if case .folder(let id) = node { folder = store.index.folders[id]?.parentId }
                var chain: [String] = []
                while let f = folder, !f.isEmpty {
                    chain.append(f)
                    folder = store.index.folders[f]?.parentId
                }
                for f in chain.reversed() { if let n = nodes[.folder(f)] { outline.expandItem(n) } }
            }
            let rows = IndexSet(store.selection.compactMap { nodes[$0] }.map { outline.row(forItem: $0) }.filter { $0 >= 0 })
            outline.selectRowIndexes(rows, byExtendingSelection: false)
            if let first = rows.first { outline.scrollRowToVisible(first) }
            isSyncing = false
        }

        // MARK: Data source

        func outlineView(_ outlineView: NSOutlineView, numberOfChildrenOfItem item: Any?) -> Int {
            guard let node = item as? TreeNode else { return root == nil ? 0 : 1 }
            return node.children.count
        }

        func outlineView(_ outlineView: NSOutlineView, child index: Int, ofItem item: Any?) -> Any {
            guard let node = item as? TreeNode else { return root! }
            return node.children[index]
        }

        func outlineView(_ outlineView: NSOutlineView, isItemExpandable item: Any) -> Bool {
            guard let node = item as? TreeNode else { return false }
            return node.node.folderId != nil && !node.children.isEmpty
        }

        // MARK: Cells

        func outlineView(_ outlineView: NSOutlineView, viewFor tableColumn: NSTableColumn?, item: Any) -> NSView? {
            guard let node = item as? TreeNode else { return nil }
            let cell = (outlineView.makeView(withIdentifier: TreeCell.identifier, owner: nil) as? TreeCell) ?? TreeCell()
            cell.configure(node.node, index: store.index)
            cell.textField?.delegate = self
            return cell
        }

        func outlineView(_ outlineView: NSOutlineView, heightOfRowByItem item: Any) -> CGFloat { 24 }

        func outlineViewSelectionDidChange(_ notification: Notification) {
            guard !isSyncing, let outline else { return }
            let selected = Set(outline.selectedRowIndexes.compactMap { (outline.item(atRow: $0) as? TreeNode)?.node })
            if selected != store.selection { store.selection = selected }
        }

        func outlineViewItemDidExpand(_ notification: Notification) {
            guard !isSyncing, let node = notification.userInfo?["NSObject"] as? TreeNode, let id = node.node.folderId else { return }
            store.expanded.insert(id)
        }

        func outlineViewItemDidCollapse(_ notification: Notification) {
            guard !isSyncing, let node = notification.userInfo?["NSObject"] as? TreeNode, let id = node.node.folderId else { return }
            store.expanded.remove(id)
        }

        @objc func doubleClicked(_ sender: Any?) {
            guard let outline, outline.clickedRow >= 0, let node = outline.item(atRow: outline.clickedRow) as? TreeNode else { return }
            switch node.node {
            case .folder:
                if outline.isItemExpanded(node) { outline.collapseItem(node) } else { outline.expandItem(node) }
            case .clip:
                beginRename(node.node)
            }
        }

        // MARK: Rename

        func renameSelected() {
            guard let first = store.selection.first, store.selection.count == 1 else { return }
            beginRename(first)
        }

        func beginRename(_ node: NodeID) {
            guard let outline, let item = nodes[node] else { return }
            if case .folder(let id) = node, id == store.index.rootId { return }
            let row = outline.row(forItem: item)
            guard row >= 0, let cell = outline.view(atColumn: 0, row: row, makeIfNecessary: true) as? TreeCell,
                  let field = cell.textField else { return }
            outline.scrollRowToVisible(row)
            field.isEditable = true
            outline.window?.makeFirstResponder(field)
            if case .clip(let id) = node, let clip = store.index.clips[id], let editor = field.currentEditor() {
                editor.selectedRange = NSRange(location: 0, length: (clip.title as NSString).length)
            }
        }

        func controlTextDidEndEditing(_ obj: Notification) {
            guard let field = obj.object as? NSTextField, let outline else { return }
            field.isEditable = false
            let row = outline.row(for: field)
            guard row >= 0, let node = outline.item(atRow: row) as? TreeNode else { return }
            let newName = field.stringValue.trimmingCharacters(in: .whitespaces)
            if newName.isEmpty {
                outline.reloadItem(node)
                return
            }
            DispatchQueue.main.async { [store] in
                store.rename(node.node, to: newName)
                // Restore the displayed name if the rename was rejected.
                self.outline?.reloadItem(node)
            }
        }

        func deleteSelected() {
            let folders = store.selectedFolderIds.filter { $0 != store.index.rootId }
            if folders.count == 1 && store.selectedClipIds.isEmpty {
                store.requestDelete(folderId: folders[0])
            } else if folders.isEmpty && !store.selectedClipIds.isEmpty {
                store.removeFromProject(store.selectedClipIds)
            }
        }

        // MARK: Drag & drop

        func outlineView(_ outlineView: NSOutlineView, pasteboardWriterForItem item: Any) -> NSPasteboardWriting? {
            guard let node = item as? TreeNode else { return nil }
            if case .folder(let id) = node.node, id == store.index.rootId { return nil }
            let pb = NSPasteboardItem()
            pb.setString(node.node.token, forType: .clairvoNode)
            return pb
        }

        private func draggedNodes(_ info: NSDraggingInfo) -> [NodeID] {
            (info.draggingPasteboard.pasteboardItems ?? []).compactMap { $0.string(forType: .clairvoNode) }.compactMap(NodeID.init(token:))
        }

        func outlineView(_ outlineView: NSOutlineView, validateDrop info: NSDraggingInfo, proposedItem item: Any?,
                         proposedChildIndex index: Int) -> NSDragOperation {
            guard let root else { return [] }
            var target = (item as? TreeNode) ?? root
            if case .clip(let id) = target.node, let parent = store.index.clips[id]?.folderId, let p = nodes[.folder(parent)] {
                target = p
            }
            guard let folderId = target.node.folderId else { return [] }
            outlineView.setDropItem(target, dropChildIndex: NSOutlineViewDropOnItemIndex)
            let dragged = draggedNodes(info)
            return !dragged.isEmpty && store.canMove(dragged, to: folderId) ? .move : []
        }

        func outlineView(_ outlineView: NSOutlineView, acceptDrop info: NSDraggingInfo, item: Any?, childIndex index: Int) -> Bool {
            guard let target = (item as? TreeNode) ?? root, let folderId = target.node.folderId else { return false }
            let dragged = draggedNodes(info)
            DispatchQueue.main.async { [store] in store.move(dragged, to: folderId) }
            return true
        }

        // MARK: Context menu (spec §40)

        func menuNeedsUpdate(_ menu: NSMenu) {
            menu.removeAllItems()
            guard let outline, outline.clickedRow >= 0, let clicked = outline.item(atRow: outline.clickedRow) as? TreeNode else { return }
            if !store.selection.contains(clicked.node) {
                store.selection = [clicked.node]
                applySelection()
            }
            ContextMenuBuilder(store: store, onRename: { [weak self] node in self?.beginRename(node) }).populate(menu, for: store.selection)
        }
    }
}

/// Shared context-menu construction for the tree and the clip table.
@MainActor
struct ContextMenuBuilder {
    let store: ProjectStore
    let onRename: (NodeID) -> Void

    func populate(_ menu: NSMenu, for selection: Set<NodeID>) {
        let clips = selection.compactMap(\.clipId)
        let folders = selection.compactMap(\.folderId)
        let index = store.index

        if selection.count == 1, let node = selection.first {
            let isRoot = node.folderId == index.rootId
            if !isRoot { menu.addItem(ActionItem("Rename", "pencil") { onRename(node) }) }
        }
        if folders.count == 1, clips.isEmpty {
            let folderId = folders[0]
            menu.addItem(ActionItem("New Folder", "folder.badge.plus") { store.createFolder(in: folderId) })
        }
        let movable = selection.filter { $0.folderId != index.rootId }
        if !movable.isEmpty {
            let moveItem = NSMenuItem(title: "Move to", action: nil, keyEquivalent: "")
            moveItem.image = NSImage(systemSymbolName: "arrow.right.doc.on.clipboard", accessibilityDescription: nil)
            let sub = NSMenu()
            for folder in index.allFoldersSorted() where store.canMove(Array(movable), to: folder.id) {
                let depth = folder.path.isEmpty ? 0 : folder.path.split(separator: "/").count
                let item = ActionItem(folder.isRoot ? folder.name : folder.name, folder.isRoot ? "house" : "folder") {
                    store.move(Array(movable), to: folder.id)
                }
                item.indentationLevel = min(depth, 15)
                sub.addItem(item)
            }
            moveItem.submenu = sub
            menu.addItem(moveItem)
        }
        if !clips.isEmpty {
            menu.addItem(.separator())
            let addTag = NSMenuItem(title: "Add Tag", action: nil, keyEquivalent: "")
            addTag.image = NSImage(systemSymbolName: "tag", accessibilityDescription: nil)
            let tagMenu = NSMenu()
            for tag in index.sortedTags {
                tagMenu.addItem(ActionItem(tag.name, nil) { store.addTag(named: tag.name, to: clips) })
            }
            if !index.sortedTags.isEmpty { tagMenu.addItem(.separator()) }
            tagMenu.addItem(ActionItem("New Tag…", "plus") {
                if let name = promptForText(title: "New Tag", message: "Tag \(clips.count) clip\(clips.count == 1 ? "" : "s") with:") {
                    store.addTag(named: name, to: clips)
                }
            })
            addTag.submenu = tagMenu
            menu.addItem(addTag)

            let assigned = Set(clips.flatMap { index.clips[$0]?.tagIds ?? [] })
            if !assigned.isEmpty {
                let removeTag = NSMenuItem(title: "Remove Tag", action: nil, keyEquivalent: "")
                removeTag.image = NSImage(systemSymbolName: "tag.slash", accessibilityDescription: nil)
                let sub = NSMenu()
                for tag in index.sortedTags where assigned.contains(tag.id) {
                    sub.addItem(ActionItem(tag.name, nil) { store.removeTag(tag.id, from: clips) })
                }
                removeTag.submenu = sub
                menu.addItem(removeTag)
            }
            menu.addItem(.separator())
            let reset = ActionItem("Reset to Automatic Organization", "wand.and.stars") { store.resetToAutomatic(clips) }
            reset.isEnabled = clips.contains { index.clips[$0]?.isManual ?? false }
            menu.addItem(reset)
            menu.addItem(ActionItem("Remove from Project", "minus.circle") { store.removeFromProject(clips) })
        }
        if folders.count == 1, clips.isEmpty, folders[0] != index.rootId {
            menu.addItem(.separator())
            menu.addItem(ActionItem("Delete Folder…", "trash") { store.requestDelete(folderId: folders[0]) })
        }
        if selection.count == 1, let node = selection.first {
            menu.addItem(.separator())
            menu.addItem(ActionItem("Reveal in Finder", "magnifyingglass") { store.revealInFinder(node) })
        }
    }
}

/// NSMenuItem that runs a closure.
final class ActionItem: NSMenuItem {
    private let handler: () -> Void

    init(_ title: String, _ symbol: String?, handler: @escaping () -> Void) {
        self.handler = handler
        super.init(title: title, action: #selector(run), keyEquivalent: "")
        target = self
        if let symbol { image = NSImage(systemSymbolName: symbol, accessibilityDescription: nil) }
    }

    @available(*, unavailable)
    required init(coder: NSCoder) { fatalError() }

    @objc private func run() { handler() }
}

@MainActor
func promptForText(title: String, message: String, initial: String = "") -> String? {
    let alert = NSAlert()
    alert.messageText = title
    alert.informativeText = message
    let field = NSTextField(frame: NSRect(x: 0, y: 0, width: 260, height: 24))
    field.stringValue = initial
    alert.accessoryView = field
    alert.addButton(withTitle: "OK")
    alert.addButton(withTitle: "Cancel")
    alert.window.initialFirstResponder = field
    guard alert.runModal() == .alertFirstButtonReturn else { return nil }
    let value = field.stringValue.trimmingCharacters(in: .whitespaces)
    return value.isEmpty ? nil : value
}

/// Row view: icon, name, and status badges (missing, manual override, pending change).
final class TreeCell: NSTableCellView {
    static let identifier = NSUserInterfaceItemIdentifier("TreeCell")
    private let icon = NSImageView()
    private let label = NSTextField(labelWithString: "")
    private let detail = NSTextField(labelWithString: "")
    private let badges = NSStackView()

    init() {
        super.init(frame: .zero)
        identifier = Self.identifier
        imageView = icon
        textField = label
        label.lineBreakMode = .byTruncatingMiddle
        label.isEditable = false
        label.isBordered = false
        label.drawsBackground = false
        label.focusRingType = .none
        label.setContentCompressionResistancePriority(.defaultLow, for: .horizontal)
        detail.textColor = .tertiaryLabelColor
        detail.font = .systemFont(ofSize: NSFont.smallSystemFontSize)
        detail.setContentHuggingPriority(.required, for: .horizontal)
        badges.orientation = .horizontal
        badges.spacing = 3
        badges.setContentHuggingPriority(.required, for: .horizontal)
        for v in [icon, label, badges, detail] as [NSView] {
            v.translatesAutoresizingMaskIntoConstraints = false
            addSubview(v)
        }
        NSLayoutConstraint.activate([
            icon.leadingAnchor.constraint(equalTo: leadingAnchor, constant: 2),
            icon.centerYAnchor.constraint(equalTo: centerYAnchor),
            icon.widthAnchor.constraint(equalToConstant: 16),
            icon.heightAnchor.constraint(equalToConstant: 16),
            label.leadingAnchor.constraint(equalTo: icon.trailingAnchor, constant: 6),
            label.centerYAnchor.constraint(equalTo: centerYAnchor),
            badges.leadingAnchor.constraint(greaterThanOrEqualTo: label.trailingAnchor, constant: 4),
            badges.centerYAnchor.constraint(equalTo: centerYAnchor),
            detail.leadingAnchor.constraint(equalTo: badges.trailingAnchor, constant: 4),
            detail.trailingAnchor.constraint(equalTo: trailingAnchor, constant: -4),
            detail.centerYAnchor.constraint(equalTo: centerYAnchor),
        ])
    }

    @available(*, unavailable)
    required init?(coder: NSCoder) { fatalError() }

    private func badge(_ symbol: String, _ color: NSColor, _ tip: String) -> NSImageView {
        let v = NSImageView(image: NSImage(systemSymbolName: symbol, accessibilityDescription: tip) ?? NSImage())
        v.contentTintColor = color
        v.symbolConfiguration = .init(pointSize: 10, weight: .semibold)
        v.toolTip = tip
        return v
    }

    func configure(_ node: NodeID, index: ProjectIndex) {
        badges.arrangedSubviews.forEach { $0.removeFromSuperview() }
        label.isEditable = false
        switch node {
        case .folder(let id):
            guard let f = index.folders[id] else { return }
            label.stringValue = f.name
            label.font = f.isRoot ? .boldSystemFont(ofSize: NSFont.systemFontSize) : .systemFont(ofSize: NSFont.systemFontSize)
            icon.image = NSImage(systemSymbolName: f.isRoot ? "film.stack" : "folder.fill", accessibilityDescription: nil)
            icon.contentTintColor = f.isRoot ? .controlAccentColor : (f.origin == "AUTOMATIC" ? .systemTeal : .systemBlue)
            detail.stringValue = "\(index.subtreeClipCount[id] ?? 0)"
            if !f.isRoot && !f.existsOnDisk {
                badges.addArrangedSubview(badge("plus.circle.fill", .systemGreen, "Will be created on Apply"))
            } else if f.hasPendingRename {
                badges.addArrangedSubview(badge("arrow.right.circle.fill", .systemBlue, "Will be renamed/moved on Apply (currently \(f.physicalPath ?? ""))"))
            }
            toolTip = f.isRoot ? "Project root" : (f.origin == "AUTOMATIC" ? "Automatic folder" : f.origin == "IMPORTED" ? "Imported folder" : "Folder")
        case .clip(let id):
            guard let c = index.clips[id] else { return }
            label.stringValue = c.name
            label.font = .systemFont(ofSize: NSFont.systemFontSize)
            icon.image = NSImage(systemSymbolName: c.isMissing ? "film.circle" : "film", accessibilityDescription: nil)
            icon.contentTintColor = c.isMissing ? .systemOrange : .secondaryLabelColor
            detail.stringValue = ""
            if c.isMissing { badges.addArrangedSubview(badge("exclamationmark.triangle.fill", .systemOrange, "Missing: file not found at \(c.filePath)")) }
            if c.placementOverride || c.titleOverride {
                badges.addArrangedSubview(badge("hand.raised.fill", .systemPurple, "Manually organized (overrides automatic organization)"))
            }
            if !c.isMissing && index.hasPendingChange(c) {
                badges.addArrangedSubview(badge("arrow.right.circle.fill", .systemBlue, "Pending: \(c.filePath) → \(index.intendedPath(c))"))
            }
            if !c.tagIds.isEmpty {
                let names = c.tagIds.compactMap { index.tags[$0]?.name }.joined(separator: ", ")
                badges.addArrangedSubview(badge("tag.fill", .systemYellow, names))
            }
            toolTip = c.originalFilename == c.name ? c.filePath : "\(c.filePath) (originally \(c.originalFilename))"
        }
    }
}
