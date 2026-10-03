import SwiftUI

/// "REVIEW CHANGES" (spec §25, FR-REVIEW-*): every filesystem operation Apply
/// would perform, plus preflight validation, before anything touches disk.
struct ReviewSheet: View {
    @EnvironmentObject private var store: ProjectStore
    @Environment(\.dismiss) private var dismiss
    @State private var changes: ChangeSetDTO?
    @State private var preflight: PreflightDTO?
    @State private var loading = true
    @State private var confirmApply = false
    @State private var errorText: String?

    var body: some View {
        VStack(spacing: 0) {
            header.padding(20)
            Divider()
            if loading {
                ProgressView("Validating changes…").frame(maxWidth: .infinity, maxHeight: .infinity)
            } else if let changes {
                List {
                    if let preflight { preflightSection(preflight) }
                    if changes.isEmpty && changes.skippedMissing.isEmpty {
                        Section {
                            Label("Nothing to apply — the project matches the files on disk.", systemImage: "checkmark.circle")
                                .foregroundStyle(.secondary)
                        }
                    }
                    folderSection(changes.folders)
                    moveSection(changes.clips.filter(\.moves))
                    renameSection(changes.clips.filter(\.renames))
                    if !changes.skippedMissing.isEmpty {
                        Section("Skipped — missing files (\(changes.skippedMissing.count))") {
                            ForEach(changes.skippedMissing) { s in
                                Label(s.path, systemImage: "exclamationmark.triangle.fill").foregroundStyle(.orange)
                            }
                        }
                    }
                }
                .listStyle(.inset(alternatesRowBackgrounds: true))
            } else if let errorText {
                Text(errorText).foregroundStyle(.red).frame(maxWidth: .infinity, maxHeight: .infinity)
            }
            Divider()
            footer.padding(16)
        }
        .frame(width: 760, height: 620)
        .task { await load() }
        .alert("Apply changes to disk?", isPresented: $confirmApply) {
            Button("Cancel", role: .cancel) {}
            Button("Apply") {
                dismiss()
                Task { await store.apply() }
            }
        } message: {
            Text(applySummary + "\n\nFiles are moved and renamed, never deleted or overwritten. Applied changes cannot be undone with Undo.")
        }
    }

    private var header: some View {
        HStack(alignment: .top) {
            Image(systemName: "checklist").font(.title2).foregroundStyle(.tint)
            VStack(alignment: .leading, spacing: 4) {
                Text("Review Changes").font(.title2.bold())
                if let c = changes {
                    Text(summary(c)).font(.callout).foregroundStyle(.secondary)
                }
            }
            Spacer()
        }
    }

    private var footer: some View {
        HStack {
            Button("Undo Last Edit") {
                store.undo()
                Task { await load() }
            }
            .disabled(!(store.snapshot?.undo.canUndo ?? false))
            Spacer()
            if let p = preflight, !p.ok {
                Label("Resolve \(p.errorCount) error\(p.errorCount == 1 ? "" : "s") to apply", systemImage: "xmark.octagon.fill")
                    .foregroundStyle(.red)
                    .font(.callout)
            }
            Button("Close") { dismiss() }.keyboardShortcut(.cancelAction)
            Button("Apply…") { confirmApply = true }
                .keyboardShortcut(.defaultAction)
                .disabled(loading || !(preflight?.ok ?? false) || (changes?.isEmpty ?? true) || store.isApplying)
        }
    }

    private func preflightSection(_ p: PreflightDTO) -> some View {
        Section("Preflight") {
            ForEach(p.checks) { check in
                Label(check.description, systemImage: check.passed ? "checkmark.circle.fill" : "xmark.circle.fill")
                    .foregroundStyle(check.passed ? Color.green : Color.red)
            }
            ForEach(p.issues) { issue in
                VStack(alignment: .leading, spacing: 2) {
                    Label(issue.message, systemImage: icon(issue.severity))
                        .foregroundStyle(color(issue.severity))
                    if !issue.path.isEmpty { Text(issue.path).font(.caption.monospaced()).foregroundStyle(.secondary) }
                }
            }
        }
    }

    @ViewBuilder
    private func folderSection(_ folders: [ChangeDTO]) -> some View {
        if !folders.isEmpty {
            Section("Folders (\(folders.count))") {
                ForEach(folders) { c in
                    if c.type == "CREATE_FOLDER" {
                        ChangeRow(symbol: "plus", color: .green, primary: "Create \(c.destination)/", secondary: nil, origin: c.origin)
                    } else {
                        ChangeRow(symbol: "arrow.right", color: .blue,
                                  primary: (c.type == "MOVE_FOLDER" ? "Move " : "Rename ") + c.source + "/",
                                  secondary: "→ " + c.destination + "/", origin: c.origin)
                    }
                }
            }
        }
    }

    @ViewBuilder
    private func moveSection(_ moves: [ChangeDTO]) -> some View {
        if !moves.isEmpty {
            Section("Moves (\(moves.count))") {
                ForEach(moves) { c in
                    ChangeRow(symbol: "arrow.turn.down.right", color: .blue, primary: c.oldName,
                              secondary: "/" + dir(c.source) + " → /" + dir(c.destination), origin: c.origin)
                }
            }
        }
    }

    @ViewBuilder
    private func renameSection(_ renames: [ChangeDTO]) -> some View {
        if !renames.isEmpty {
            Section("Renames (\(renames.count))") {
                ForEach(renames) { c in
                    ChangeRow(symbol: "pencil", color: .purple, primary: c.oldName + " → " + c.newName, secondary: nil, origin: c.origin)
                }
            }
        }
    }

    private func load() async {
        loading = true
        do {
            let p = try await store.preflight()
            let c = try await store.pendingChanges()
            preflight = p
            changes = c
        } catch {
            errorText = error.localizedDescription
        }
        loading = false
    }

    private func dir(_ path: String) -> String {
        guard let slash = path.lastIndex(of: "/") else { return "" }
        return String(path[..<slash])
    }

    private func summary(_ c: ChangeSetDTO) -> String {
        "\(c.counts.folderCreates) new folders · \(c.counts.folderMoves) folder renames/moves · \(c.counts.clipMoves) clip moves · \(c.counts.clipRenames) clip renames"
    }

    private var applySummary: String { changes.map(summary) ?? "" }

    private func icon(_ severity: String) -> String {
        switch severity {
        case "error": return "xmark.octagon.fill"
        case "warning": return "exclamationmark.triangle.fill"
        default: return "info.circle.fill"
        }
    }

    private func color(_ severity: String) -> Color {
        switch severity {
        case "error": return .red
        case "warning": return .orange
        default: return .secondary
        }
    }
}

private struct ChangeRow: View {
    let symbol: String
    let color: Color
    let primary: String
    let secondary: String?
    let origin: String

    var body: some View {
        HStack(alignment: .firstTextBaseline, spacing: 8) {
            Image(systemName: symbol).foregroundStyle(color).frame(width: 16)
            VStack(alignment: .leading, spacing: 2) {
                Text(primary).textSelection(.enabled)
                if let secondary { Text(secondary).font(.caption.monospaced()).foregroundStyle(.secondary) }
            }
            Spacer()
            Text(origin == "MANUAL" ? "Manual" : "Automatic")
                .font(.caption2)
                .padding(.horizontal, 6)
                .padding(.vertical, 2)
                .background(Capsule().fill((origin == "MANUAL" ? Color.purple : Color.teal).opacity(0.18)))
        }
    }
}
