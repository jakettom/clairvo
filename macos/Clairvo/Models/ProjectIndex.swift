import Foundation

/// A selectable node in the hierarchy (tree, canvas and table share it).
enum NodeID: Hashable {
    case folder(String)
    case clip(String)

    var token: String {
        switch self {
        case .folder(let id): return "f:" + id
        case .clip(let id): return "c:" + id
        }
    }

    init?(token: String) {
        if token.hasPrefix("f:") { self = .folder(String(token.dropFirst(2))) }
        else if token.hasPrefix("c:") { self = .clip(String(token.dropFirst(2))) }
        else { return nil }
    }

    var folderId: String? { if case .folder(let id) = self { return id } else { return nil } }
    var clipId: String? { if case .clip(let id) = self { return id } else { return nil } }
}

/// Lookup tables derived from a snapshot so views can walk the hierarchy.
struct ProjectIndex {
    var rootId = ""
    var folders: [String: FolderDTO] = [:]
    var clips: [String: ClipDTO] = [:]
    var tags: [String: TagDTO] = [:]
    var childFolders: [String: [String]] = [:]
    var folderClips: [String: [String]] = [:]
    var subtreeClipCount: [String: Int] = [:]
    var sortedTags: [TagDTO] = []

    init() {}

    init(_ s: Snapshot) {
        rootId = s.rootFolderId
        for f in s.folders { folders[f.id] = f }
        for c in s.clips { clips[c.id] = c }
        for t in s.tags { tags[t.id] = t }
        sortedTags = s.tags.sorted { $0.name.localizedStandardCompare($1.name) == .orderedAscending }
        for f in s.folders where !f.parentId.isEmpty { childFolders[f.parentId, default: []].append(f.id) }
        for c in s.clips { folderClips[c.folderId, default: []].append(c.id) }
        for key in childFolders.keys {
            childFolders[key]?.sort {
                folders[$0]!.name.localizedStandardCompare(folders[$1]!.name) == .orderedAscending
            }
        }
        for key in folderClips.keys {
            folderClips[key]?.sort { clips[$0]!.name.localizedStandardCompare(clips[$1]!.name) == .orderedAscending }
        }
        _ = countSubtree(rootId)
    }

    private mutating func countSubtree(_ id: String) -> Int {
        var n = folderClips[id]?.count ?? 0
        for child in childFolders[id] ?? [] { n += countSubtree(child) }
        subtreeClipCount[id] = n
        return n
    }

    func intendedPath(_ clip: ClipDTO) -> String {
        let folderPath = folders[clip.folderId]?.path ?? ""
        return folderPath.isEmpty ? clip.name : folderPath + "/" + clip.name
    }

    func hasPendingChange(_ clip: ClipDTO) -> Bool { intendedPath(clip) != clip.filePath }

    func displayPath(folderId: String) -> String {
        guard let f = folders[folderId] else { return "" }
        let rootName = folders[rootId]?.name ?? ""
        return f.isRoot ? rootName : rootName + " / " + f.path.replacingOccurrences(of: "/", with: " / ")
    }

    func isAncestor(_ ancestor: String, of folder: String) -> Bool {
        var current: String? = folder
        while let id = current {
            if id == ancestor { return true }
            current = folders[id].flatMap { $0.parentId.isEmpty ? nil : $0.parentId }
        }
        return false
    }

    func folderOf(_ node: NodeID) -> String? {
        switch node {
        case .folder(let id): return id
        case .clip(let id): return clips[id]?.folderId
        }
    }

    func allFoldersSorted() -> [FolderDTO] {
        folders.values.sorted { $0.path.localizedStandardCompare($1.path) == .orderedAscending }
    }
}
