import Foundation

// JSON documents produced by app/application/JsonCodec.cpp.

struct Snapshot: Decodable {
    struct Project: Decodable {
        let id: String
        let name: String
        let rootPath: String
        let projectFile: String
    }
    struct UndoState: Decodable {
        let canUndo: Bool
        let canRedo: Bool
        let undoDescription: String
        let redoDescription: String
    }
    let project: Project
    let rootFolderId: String
    let folders: [FolderDTO]
    let clips: [ClipDTO]
    let tags: [TagDTO]
    let config: OrganizationConfigDTO
    let undo: UndoState
    let pendingChangeCount: Int
    let missingCount: Int
    let categories: [CategoryDTO]
    let namingVariables: [String]
}

struct FolderDTO: Decodable, Identifiable {
    let id: String
    let parentId: String
    let name: String
    let origin: String
    let path: String
    let physicalPath: String?

    var isRoot: Bool { parentId.isEmpty }
    var existsOnDisk: Bool { physicalPath != nil }
    var hasPendingRename: Bool { physicalPath.map { $0 != path } ?? false }
}

struct ClipDTO: Decodable, Identifiable {
    let id: String
    let folderId: String
    let title: String
    let `extension`: String
    let originalFilename: String
    let filePath: String
    let fileSize: Int64
    let status: String
    let placementOverride: Bool
    let titleOverride: Bool
    let tagIds: [String]
    let metadata: [String: String]
    let display: [String: String]

    var name: String { title + self.extension }
    var isMissing: Bool { status == "MISSING" }
    var isManual: Bool { placementOverride || titleOverride }
}

struct TagDTO: Decodable, Identifiable, Hashable {
    let id: String
    let name: String
    let count: Int
}

struct OrganizationConfigDTO: Decodable {
    let criteria: [String]
    let namingTemplate: String
}

struct CategoryDTO: Decodable, Identifiable, Hashable {
    let key: String
    let name: String
    var id: String { key }
}

struct CategoryStat: Decodable, Identifiable {
    let key: String
    let name: String
    let clipsWithValue: Int
    let distinctValues: Int
    var id: String { key }
}

struct ClipDetails: Decodable {
    struct Raw: Decodable, Identifiable {
        let key: String
        let value: String
        let source: String
        var id: String { key + "|" + source + "|" + value }
    }
    let id: String
    let raw: [Raw]
    let fileHash: String
    let absolutePath: String
    let intendedPath: String
    let folderPath: String
}

struct ChangeDTO: Decodable, Identifiable {
    let type: String
    let origin: String
    let clipId: String
    let folderId: String
    let source: String
    let destination: String
    let oldName: String
    let newName: String
    let moves: Bool
    let renames: Bool
    var id: String { type + clipId + folderId + destination }
}

struct ChangeSetDTO: Decodable {
    struct Skipped: Decodable, Identifiable {
        let clipId: String
        let path: String
        let intendedPath: String
        var id: String { clipId }
    }
    struct Counts: Decodable {
        let folderCreates: Int
        let folderMoves: Int
        let clipMoves: Int
        let clipRenames: Int
    }
    let folders: [ChangeDTO]
    let clips: [ChangeDTO]
    let skippedMissing: [Skipped]
    let counts: Counts
    var isEmpty: Bool { folders.isEmpty && clips.isEmpty }
}

struct PreflightDTO: Decodable {
    struct Check: Decodable, Identifiable {
        let description: String
        let passed: Bool
        var id: String { description }
    }
    struct Issue: Decodable, Identifiable {
        let severity: String
        let message: String
        let path: String
        let clipId: String
        let folderId: String
        var id: String { severity + message + path + clipId + folderId }
    }
    let ok: Bool
    let checks: [Check]
    let issues: [Issue]
    let errorCount: Int
    let warningCount: Int
    let counts: ChangeSetDTO.Counts
}

struct ApplyOperationDTO: Decodable, Identifiable {
    let type: String
    let clipId: String
    let folderId: String
    let source: String
    let destination: String
    let success: Bool
    let error: String
    let description: String
    var id: String { type + source + destination + description }
}

struct ApplyResultDTO: Decodable {
    let state: String
    let succeeded: Int
    let failed: Int
    let operations: [ApplyOperationDTO]
    let removedDirectories: [String]
    let preflight: PreflightDTO
}

struct NamePreview: Decodable {
    struct Example: Decodable, Identifiable {
        let original: String
        let name: String
        var id: String { original }
    }
    let examples: [Example]
    let error: String?
}

struct SearchResultDTO: Decodable {
    let clipIds: [String]
    let folderIds: [String]
}

struct RecoveryReportDTO: Decodable {
    let operations: [ApplyOperationDTO]
}
