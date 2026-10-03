import SwiftUI

/// Apply Results (spec §27): partial execution with a detailed report.
struct ApplyResultView: View {
    @Environment(\.dismiss) private var dismiss
    let result: ApplyResultDTO
    @State private var showOnlyFailures = false

    var body: some View {
        VStack(alignment: .leading, spacing: 0) {
            HStack(spacing: 12) {
                Image(systemName: icon).font(.system(size: 30)).foregroundStyle(color)
                VStack(alignment: .leading, spacing: 4) {
                    Text(title).font(.title2.bold())
                    Text("\(result.succeeded) operation\(result.succeeded == 1 ? "" : "s") succeeded · \(result.failed) failed")
                        .foregroundStyle(.secondary)
                }
                Spacer()
                if result.failed > 0 { Toggle("Failures only", isOn: $showOnlyFailures).toggleStyle(.switch) }
            }
            .padding(20)
            Divider()
            List {
                if result.state == "BLOCKED" {
                    Section("Preflight errors") {
                        ForEach(result.preflight.issues.filter { $0.severity == "error" }) { issue in
                            VStack(alignment: .leading) {
                                Label(issue.message, systemImage: "xmark.octagon.fill").foregroundStyle(.red)
                                Text(issue.path).font(.caption.monospaced()).foregroundStyle(.secondary)
                            }
                        }
                    }
                }
                Section("Operations") {
                    ForEach(result.operations.filter { !showOnlyFailures || !$0.success }) { op in
                        HStack(alignment: .firstTextBaseline) {
                            Image(systemName: op.success ? "checkmark" : "xmark")
                                .foregroundStyle(op.success ? Color.green : Color.red)
                                .frame(width: 16)
                            VStack(alignment: .leading, spacing: 2) {
                                Text(op.description).textSelection(.enabled)
                                if !op.success { Text("Reason: \(op.error)").font(.caption).foregroundStyle(.red) }
                            }
                        }
                    }
                }
                if !result.removedDirectories.isEmpty {
                    Section("Removed empty folders") {
                        ForEach(result.removedDirectories, id: \.self) { Text($0 + "/").font(.callout.monospaced()) }
                    }
                }
            }
            Divider()
            HStack {
                if result.failed > 0 {
                    Text("Failed operations remain pending in Review so you can resolve them and apply again.")
                        .font(.caption).foregroundStyle(.secondary)
                }
                Spacer()
                Button("Done") { dismiss() }.keyboardShortcut(.defaultAction)
            }
            .padding(16)
        }
        .frame(width: 680, height: 540)
    }

    private var title: String {
        switch result.state {
        case "APPLIED": return "Changes Applied"
        case "PARTIALLY_APPLIED": return "Partially Applied"
        case "BLOCKED": return "Apply Blocked by Preflight"
        case "NOTHING_TO_APPLY": return "Nothing to Apply"
        default: return "Apply Failed"
        }
    }

    private var icon: String {
        switch result.state {
        case "APPLIED", "NOTHING_TO_APPLY": return "checkmark.seal.fill"
        case "PARTIALLY_APPLIED": return "exclamationmark.triangle.fill"
        default: return "xmark.octagon.fill"
        }
    }

    private var color: Color {
        switch result.state {
        case "APPLIED", "NOTHING_TO_APPLY": return .green
        case "PARTIALLY_APPLIED": return .orange
        default: return .red
        }
    }
}

/// Shown after opening a project whose previous Apply was interrupted.
struct RecoveryReportView: View {
    @Environment(\.dismiss) private var dismiss
    let operations: [ApplyOperationDTO]

    var body: some View {
        VStack(alignment: .leading, spacing: 12) {
            Label("An earlier Apply was interrupted", systemImage: "bandage.fill").font(.title3.bold())
            Text("Clairvo compared its operation journal with the files on disk. The project now reflects what actually happened:")
                .foregroundStyle(.secondary)
            List(operations) { op in
                Label(op.description, systemImage: op.success ? "checkmark.circle.fill" : "arrow.uturn.backward.circle")
                    .foregroundStyle(op.success ? Color.green : Color.orange)
            }
            .frame(minHeight: 200)
            HStack {
                Spacer()
                Button("OK") { dismiss() }.keyboardShortcut(.defaultAction)
            }
        }
        .padding(20)
        .frame(width: 600, height: 420)
    }
}
