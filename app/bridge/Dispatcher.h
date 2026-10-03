#pragma once

#include <atomic>
#include <memory>
#include <mutex>
#include <thread>

#include <nlohmann/json.hpp>

#include "app/application/ProjectSession.h"

namespace vo::bridge {

// Owns a session plus its background import worker.
struct SessionHandle {
    std::unique_ptr<ProjectSession> session;
    std::thread importThread;
    std::atomic<bool> cancelImport{false};
    std::mutex threadMutex;

    ~SessionHandle();
    void joinImport();
};

// Executes one JSON command against the session. Throws on failure.
nlohmann::json dispatch(SessionHandle& handle, const nlohmann::json& request);

} // namespace vo::bridge
