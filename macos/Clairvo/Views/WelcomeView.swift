import SwiftUI

struct WelcomeView: View {
    @EnvironmentObject private var store: ProjectStore

    var body: some View {
        VStack(spacing: 28) {
            VStack(spacing: 10) {
                Image(systemName: "film.stack")
                    .font(.system(size: 64, weight: .light))
                    .foregroundStyle(.tint)
                Text("Clairvo")
                    .font(.system(size: 34, weight: .semibold))
                Text("Organize raw footage without touching it — until you say Apply.")
                    .foregroundStyle(.secondary)
            }
            HStack(spacing: 16) {
                WelcomeButton(title: "New Project…", subtitle: "Choose a footage folder", systemImage: "folder.badge.plus") {
                    store.chooseFolderForNewProject()
                }
                WelcomeButton(title: "Open Project…", subtitle: "Open a .project file", systemImage: "doc.text.magnifyingglass") {
                    store.chooseProjectToOpen()
                }
            }
            VStack(alignment: .leading, spacing: 6) {
                Label("Metadata is read, never modified", systemImage: "lock")
                Label("Changes stay virtual until you review and apply them", systemImage: "eye")
                Label("Files are never deleted or overwritten", systemImage: "checkmark.shield")
            }
            .font(.callout)
            .foregroundStyle(.secondary)
        }
        .padding(48)
        .frame(maxWidth: .infinity, maxHeight: .infinity)
        .background(Color(nsColor: .windowBackgroundColor))
    }
}

private struct WelcomeButton: View {
    let title: String
    let subtitle: String
    let systemImage: String
    let action: () -> Void

    var body: some View {
        Button(action: action) {
            VStack(spacing: 8) {
                Image(systemName: systemImage).font(.system(size: 28))
                Text(title).font(.headline)
                Text(subtitle).font(.caption).foregroundStyle(.secondary)
            }
            .frame(width: 200, height: 120)
            .contentShape(Rectangle())
        }
        .buttonStyle(.plain)
        .background(RoundedRectangle(cornerRadius: 12).fill(Color.primary.opacity(0.06)))
        .overlay(RoundedRectangle(cornerRadius: 12).stroke(Color.primary.opacity(0.12)))
    }
}

struct NewProjectSheet: View {
    @EnvironmentObject private var store: ProjectStore
    @Environment(\.dismiss) private var dismiss
    let root: URL
    @State private var name: String = ""

    var body: some View {
        VStack(alignment: .leading, spacing: 16) {
            Text("New Project").font(.title2.bold())
            LabeledContent("Footage folder") {
                Text(root.path).textSelection(.enabled).lineLimit(2).truncationMode(.middle)
            }
            TextField("Project name", text: $name)
                .textFieldStyle(.roundedBorder)
            Text("Creates “\(fileName)” inside the footage folder. The folder itself is never renamed, and nothing is moved until you Apply.")
                .font(.caption)
                .foregroundStyle(.secondary)
                .fixedSize(horizontal: false, vertical: true)
            HStack {
                Spacer()
                Button("Cancel") { dismiss() }.keyboardShortcut(.cancelAction)
                Button("Create & Scan") {
                    dismiss()
                    store.createProject(root: root, name: name)
                }
                .keyboardShortcut(.defaultAction)
                .disabled(name.trimmingCharacters(in: .whitespaces).isEmpty)
            }
        }
        .padding(24)
        .frame(width: 480)
        .onAppear { name = root.lastPathComponent }
    }

    private var fileName: String { name.trimmingCharacters(in: .whitespaces) + ".project" }
}
