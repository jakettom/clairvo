import SwiftUI

/// Import progress with distinct phases (spec §57).
struct ImportProgressView: View {
    let progress: ImportProgress
    let onCancel: () -> Void

    private let phases = [("scanning", "Scanning"), ("hashing", "Hashing"), ("metadata", "Metadata extraction"),
                          ("database", "Database insertion")]

    var body: some View {
        ZStack {
            Color.black.opacity(0.35).ignoresSafeArea()
            VStack(alignment: .leading, spacing: 14) {
                Text("Importing footage…").font(.title3.bold())
                ForEach(phases, id: \.0) { key, label in
                    HStack(spacing: 8) {
                        Image(systemName: icon(for: key))
                            .foregroundStyle(state(of: key) == 0 ? Color.secondary : (state(of: key) == 1 ? Color.accentColor : Color.green))
                            .frame(width: 16)
                        Text(label).foregroundStyle(state(of: key) == 0 ? .secondary : .primary)
                        Spacer()
                        if key == progress.phase { Text(countText).monospacedDigit().foregroundStyle(.secondary) }
                    }
                }
                if progress.total > 0 {
                    ProgressView(value: Double(progress.completed), total: Double(max(progress.total, 1)))
                } else {
                    ProgressView().progressViewStyle(.linear)
                }
                HStack {
                    Text("Files on disk are not moved or renamed during import.").font(.caption).foregroundStyle(.secondary)
                    Spacer()
                    Button("Cancel", action: onCancel)
                }
            }
            .padding(24)
            .frame(width: 420)
            .background(.regularMaterial, in: RoundedRectangle(cornerRadius: 14))
            .shadow(radius: 20)
        }
    }

    private var currentIndex: Int { phases.firstIndex { $0.0 == progress.phase } ?? 0 }

    /// 0 = pending, 1 = active, 2 = done
    private func state(of key: String) -> Int {
        let i = phases.firstIndex { $0.0 == key } ?? 0
        return i < currentIndex ? 2 : (i == currentIndex ? 1 : 0)
    }

    private func icon(for key: String) -> String {
        switch state(of: key) {
        case 2: return "checkmark.circle.fill"
        case 1: return "arrow.triangle.2.circlepath.circle.fill"
        default: return "circle"
        }
    }

    private var countText: String {
        progress.total > 0 ? "\(progress.completed.formatted()) / \(progress.total.formatted()) files"
                           : "\(progress.completed.formatted()) files found"
    }
}
