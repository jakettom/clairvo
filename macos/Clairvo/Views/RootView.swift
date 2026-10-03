import SwiftUI

struct RootView: View {
    @EnvironmentObject private var store: ProjectStore

    var body: some View {
        Group {
            if store.hasProject {
                MainView()
            } else {
                WelcomeView()
            }
        }
        .sheet(item: $store.sheet) { sheet in
            switch sheet {
            case .newProject(let url):
                NewProjectSheet(root: url)
            case .organize:
                OrganizeSheet()
            case .review:
                ReviewSheet()
            case .applyResult(let result):
                ApplyResultView(result: result)
            case .recovery(let operations):
                RecoveryReportView(operations: operations)
            }
        }
        .alert(item: $store.alert) { item in
            Alert(title: Text(item.title), message: Text(item.message), dismissButton: .default(Text("OK")))
        }
    }
}
