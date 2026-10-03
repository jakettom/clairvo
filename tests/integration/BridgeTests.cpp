#include <gtest/gtest.h>

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>

#include <nlohmann/json.hpp>

#include "support/TestSupport.h"
#include "vo_bridge.h"

using namespace vo::test;
using nlohmann::json;

namespace {

json call(vo_session* s, const json& req) {
    char* out = vo_session_call(s, req.dump().c_str());
    json j = json::parse(out);
    vo_free_string(out);
    return j;
}

struct Events {
    std::mutex mu;
    std::condition_variable cv;
    std::vector<json> events;
    static void callback(void* ctx, const char* event) {
        auto* self = static_cast<Events*>(ctx);
        std::lock_guard lock(self->mu);
        self->events.push_back(json::parse(event));
        self->cv.notify_all();
    }
    bool waitFor(const std::string& type) {
        std::unique_lock lock(mu);
        return cv.wait_for(lock, std::chrono::seconds(30), [&] {
            for (const auto& e : events)
                if (e["type"] == type) return true;
            return false;
        });
    }
};

} // namespace

TEST(Bridge, EndToEndThroughCApi) {
    TempDir dir;
    ASSERT_TRUE(writeTestMovie(dir.file("C001.MOV"), 320, 240, "Sony", "ILCE-7M4", "", ""));
    char* err = nullptr;
    vo_session* s = vo_session_create(dir.path().c_str(), "Bridge Test", &err);
    ASSERT_TRUE(s) << (err ? err : "");
    Events events;
    vo_session_set_event_callback(s, &Events::callback, &events);

    EXPECT_TRUE(call(s, {{"cmd", "startImport"}})["ok"]);
    ASSERT_TRUE(events.waitFor("importFinished"));
    bool sawProgress = false;
    for (const auto& e : events.events) sawProgress = sawProgress || e["type"] == "importProgress";
    EXPECT_TRUE(sawProgress);

    json snap = call(s, {{"cmd", "snapshot"}});
    ASSERT_TRUE(snap["ok"]) << snap.dump();
    ASSERT_EQ(snap["result"]["clips"].size(), 1u);
    json clip = snap["result"]["clips"][0];
    EXPECT_EQ(clip["metadata"]["camera"], "Sony ILCE-7M4");
    EXPECT_EQ(clip["display"]["resolution"], "320 × 240");

    json org = call(s, {{"cmd", "organize"}, {"criteria", {"camera"}}, {"namingTemplate", "{folder}_{number}"}});
    ASSERT_TRUE(org["ok"]) << org.dump();
    json changes = call(s, {{"cmd", "pendingChanges"}});
    EXPECT_EQ(changes["result"]["folders"].size(), 1u);
    EXPECT_EQ(changes["result"]["clips"][0]["destination"], "Sony ILCE-7M4/Sony ILCE-7M4_001.MOV");

    json pre = call(s, {{"cmd", "preflight"}});
    EXPECT_TRUE(pre["result"]["ok"]);
    json applied = call(s, {{"cmd", "apply"}});
    EXPECT_EQ(applied["result"]["state"], "APPLIED");
    EXPECT_TRUE(dir.exists("Sony ILCE-7M4/Sony ILCE-7M4_001.MOV"));

    json bad = call(s, {{"cmd", "renameFolder"}, {"folderId", "nope"}, {"name", "x"}});
    EXPECT_FALSE(bad["ok"]);
    EXPECT_EQ(bad["error"], "Folder not found");
    EXPECT_FALSE(call(s, {{"cmd", "doesNotExist"}})["ok"]);

    vo_session_close(s);

    vo_session* reopened = vo_session_open(dir.file("Bridge Test.project").c_str(), &err);
    ASSERT_TRUE(reopened);
    json snap2 = call(reopened, {{"cmd", "snapshot"}});
    EXPECT_EQ(snap2["result"]["clips"][0]["filePath"], "Sony ILCE-7M4/Sony ILCE-7M4_001.MOV");
    EXPECT_EQ(snap2["result"]["pendingChangeCount"], 0);
    vo_session_close(reopened);
}

TEST(Bridge, OpenMissingProjectReportsError) {
    char* err = nullptr;
    EXPECT_FALSE(vo_session_open("/nonexistent/x.project", &err));
    ASSERT_TRUE(err);
    EXPECT_NE(std::string(err).find("not found"), std::string::npos);
    vo_free_string(err);
}
