// clairvo-cli: command-line access to the same application core the UI uses.
//
//   clairvo-cli create <footage-root> <project-name>   create a project and import the root
//   clairvo-cli <file.project> import [folder]          import new footage (synchronously)
//   clairvo-cli <file.project> '<json command>'         run any bridge command, e.g.
//       '{"cmd":"organize","criteria":["camera","resolution"],"namingTemplate":"{folder}_{number}"}'
//       '{"cmd":"pendingChanges"}'  '{"cmd":"preflight"}'  '{"cmd":"apply"}'
#include <iostream>

#include <nlohmann/json.hpp>

#include "app/application/ImportService.h"
#include "app/application/ProjectSession.h"
#include "app/bridge/Dispatcher.h"

using nlohmann::json;

namespace {

int usage() {
    std::cerr << "usage:\n"
                 "  clairvo-cli create <footage-root> <project-name>\n"
                 "  clairvo-cli <file.project> import [folder]\n"
                 "  clairvo-cli <file.project> '<json command>'\n";
    return 2;
}

void printImport(const vo::ImportSummary& s) {
    std::cout << json{{"added", s.added},
                      {"scanned", s.scanned},
                      {"alreadyInProject", s.alreadyInProject},
                      {"skippedUnsupported", s.skippedUnsupported},
                      {"errors", s.errors}}
                     .dump(2)
              << "\n";
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 3) return usage();
    try {
        std::string first = argv[1];
        if (first == "create") {
            if (argc < 4) return usage();
            auto session = vo::ProjectSession::create(argv[2], argv[3], {});
            std::cout << "Created " << session->model().info.projectFile << "\n";
            printImport(vo::ImportService::run(*session, ""));
            return 0;
        }
        vo::bridge::SessionHandle handle;
        handle.session = vo::ProjectSession::open(first, {});
        std::string command = argv[2];
        if (command == "import") {
            printImport(vo::ImportService::run(*handle.session, argc > 3 ? argv[3] : ""));
            return 0;
        }
        json request = json::parse(command);
        if (request.value("cmd", "") == "startImport") {
            std::cerr << "use 'import' for synchronous import from the CLI\n";
            return 2;
        }
        std::cout << vo::bridge::dispatch(handle, request).dump(2) << "\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << "\n";
        return 1;
    }
}
