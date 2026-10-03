import SwiftUI

/// "ORGANIZE FOOTAGE" (spec §37): choose and order metadata criteria, pick a
/// naming template, and preview the result in the virtual hierarchy.
struct OrganizeSheet: View {
    @EnvironmentObject private var store: ProjectStore
    @Environment(\.dismiss) private var dismiss
    @State private var stats: [CategoryStat] = []
    @State private var order: [String] = []
    @State private var template = "{folder}_{number}"
    @State private var renameFiles = true
    @State private var preview: NamePreview?
    @State private var confirmReset = false

    var body: some View {
        VStack(alignment: .leading, spacing: 0) {
            HStack {
                Image(systemName: "wand.and.stars").font(.title2).foregroundStyle(.tint)
                VStack(alignment: .leading) {
                    Text("Organize Footage").font(.title2.bold())
                    Text("Builds a proposed folder hierarchy from metadata. Nothing on disk changes until you Apply.")
                        .font(.callout).foregroundStyle(.secondary)
                }
            }
            .padding(20)
            Divider()
            HStack(alignment: .top, spacing: 20) {
                availableColumn
                orderColumn
            }
            .padding(20)
            Divider()
            namingSection.padding(20)
            Divider()
            footer.padding(16)
        }
        .frame(width: 720)
        .onAppear(perform: load)
        .confirmationDialog("Reset organization?", isPresented: $confirmReset) {
            Button("Reset Organization", role: .destructive) {
                store.resetOrganization()
                dismiss()
            }
        } message: {
            Text("Discards all pending virtual changes and manual overrides, returning the hierarchy to the current layout on disk. You can undo this.")
        }
    }

    private var availableColumn: some View {
        VStack(alignment: .leading, spacing: 8) {
            Text("AVAILABLE METADATA").font(.caption.weight(.semibold)).foregroundStyle(.secondary)
            VStack(alignment: .leading, spacing: 6) {
                ForEach(stats) { stat in
                    Toggle(isOn: binding(for: stat.key)) {
                        HStack {
                            Text(stat.name)
                            Spacer()
                            Text(coverage(stat))
                                .font(.caption)
                                .foregroundStyle(stat.clipsWithValue == 0 ? .tertiary : .secondary)
                        }
                    }
                    .toggleStyle(.checkbox)
                }
            }
            .padding(12)
            .background(RoundedRectangle(cornerRadius: 10).fill(Color.primary.opacity(0.05)))
            Text("Clips without a value are grouped under “Unknown …” so nothing disappears.")
                .font(.caption2).foregroundStyle(.tertiary)
        }
        .frame(maxWidth: .infinity)
    }

    private var orderColumn: some View {
        VStack(alignment: .leading, spacing: 8) {
            Text("ORGANIZATION ORDER").font(.caption.weight(.semibold)).foregroundStyle(.secondary)
            VStack(alignment: .leading, spacing: 0) {
                if order.isEmpty {
                    Text("No criteria selected — clips return to the folders they are in on disk.")
                        .font(.callout).foregroundStyle(.secondary).padding(12)
                }
                ForEach(Array(order.enumerated()), id: \.element) { i, key in
                    HStack {
                        Text("\(i + 1).").monospacedDigit().foregroundStyle(.secondary)
                        Text(name(of: key))
                        Spacer()
                        Button { move(i, by: -1) } label: { Image(systemName: "chevron.up") }
                            .disabled(i == 0)
                        Button { move(i, by: 1) } label: { Image(systemName: "chevron.down") }
                            .disabled(i == order.count - 1)
                        Button { order.remove(at: i) } label: { Image(systemName: "xmark.circle.fill") }
                    }
                    .buttonStyle(.borderless)
                    .padding(.horizontal, 12)
                    .padding(.vertical, 7)
                    if i < order.count - 1 { Divider() }
                }
            }
            .background(RoundedRectangle(cornerRadius: 10).fill(Color.primary.opacity(0.05)))
            if !order.isEmpty {
                Text(hierarchyPreview).font(.caption.monospaced()).foregroundStyle(.secondary).padding(.top, 4)
            }
        }
        .frame(maxWidth: .infinity)
    }

    private var namingSection: some View {
        VStack(alignment: .leading, spacing: 8) {
            Toggle("Rename clips with a naming template", isOn: $renameFiles)
            HStack {
                TextField("Template", text: $template)
                    .textFieldStyle(.roundedBorder)
                    .font(.body.monospaced())
                    .disabled(!renameFiles)
                    .onChange(of: template) { _, _ in updatePreview() }
                Menu("Insert") {
                    ForEach(store.snapshot?.namingVariables ?? [], id: \.self) { v in
                        Button(v) { template += v }
                    }
                }
                .fixedSize()
                .disabled(!renameFiles)
            }
            if renameFiles, let preview {
                if let error = preview.error {
                    Label(error, systemImage: "exclamationmark.triangle.fill").foregroundStyle(.orange).font(.caption)
                } else {
                    HStack(spacing: 14) {
                        ForEach(preview.examples) { ex in
                            HStack(spacing: 4) {
                                Text(ex.original).foregroundStyle(.secondary)
                                Image(systemName: "arrow.right").font(.caption2)
                                Text(ex.name)
                            }
                            .font(.caption)
                        }
                    }
                }
            }
            Text("{folder} uses the clip's immediate folder; {number} counts per folder (skipping names already taken). Manually renamed clips keep their names; extensions are always preserved.")
                .font(.caption2).foregroundStyle(.tertiary)
        }
    }

    private var footer: some View {
        HStack {
            Button("Reset Organization…") { confirmReset = true }
            Button("Apply Naming Only") {
                if store.applyNaming(template: renameFiles ? template : "") { dismiss() }
            }
            .disabled(!renameFiles || preview?.error != nil)
            Spacer()
            Button("Cancel") { dismiss() }.keyboardShortcut(.cancelAction)
            Button("Preview Organization") {
                if store.organize(criteria: order, template: renameFiles ? template : "") {
                    store.centerMode = .canvas
                    dismiss()
                }
            }
            .keyboardShortcut(.defaultAction)
            .disabled(renameFiles && preview?.error != nil)
        }
    }

    // MARK: Helpers

    private func load() {
        stats = store.categoryStats()
        if let cfg = store.snapshot?.config {
            order = cfg.criteria
            if !cfg.namingTemplate.isEmpty { template = cfg.namingTemplate }
            renameFiles = cfg.criteria.isEmpty || !cfg.namingTemplate.isEmpty
        }
        if order.isEmpty {
            // Sensible default: camera then resolution, when present.
            order = ["camera", "resolution"].filter { key in stats.first { $0.key == key }?.clipsWithValue ?? 0 > 0 }
        }
        updatePreview()
    }

    private func updatePreview() { preview = store.previewNames(template) }

    private func binding(for key: String) -> Binding<Bool> {
        Binding(get: { order.contains(key) }, set: { on in
            if on { if !order.contains(key) { order.append(key) } } else { order.removeAll { $0 == key } }
        })
    }

    private func move(_ i: Int, by delta: Int) {
        let j = i + delta
        guard order.indices.contains(j) else { return }
        order.swapAt(i, j)
    }

    private func name(of key: String) -> String { stats.first { $0.key == key }?.name ?? key }

    private func coverage(_ s: CategoryStat) -> String {
        let total = store.snapshot?.clips.count ?? 0
        if s.clipsWithValue == 0 { return "not available" }
        return "\(s.clipsWithValue)/\(total) clips · \(s.distinctValues) group\(s.distinctValues == 1 ? "" : "s")"
    }

    private var hierarchyPreview: String {
        var lines: [String] = []
        for (i, key) in order.enumerated() {
            lines.append(String(repeating: "   ", count: i) + (i == 0 ? "" : "└─ ") + name(of: key))
        }
        lines.append(String(repeating: "   ", count: order.count) + "└─ Clips")
        return lines.joined(separator: "\n")
    }
}
