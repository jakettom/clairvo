#ifndef VO_BRIDGE_H
#define VO_BRIDGE_H

// Narrow C boundary between the Swift UI and the C++ application core
// (roadmap §35). Requests and responses are JSON documents; the UI never sees
// C++ types or SQLite.

#ifdef __cplusplus
extern "C" {
#endif

typedef struct vo_session vo_session;

// Called from arbitrary threads with a JSON event {"type": "...", "payload": {...}}.
typedef void (*vo_event_callback)(void* context, const char* event_json);

// Return NULL on failure and store a message in *error_message (free with
// vo_free_string).
vo_session* vo_session_create(const char* root_path, const char* project_name, char** error_message);
vo_session* vo_session_open(const char* project_file, char** error_message);

// Waits for background work (import) to stop, then closes the project.
void vo_session_close(vo_session* session);

void vo_session_set_event_callback(vo_session* session, vo_event_callback callback, void* context);

// Executes a command: {"cmd": "<name>", ...arguments}. Returns
// {"ok": true, "result": ...} or {"ok": false, "error": "..."}; free with
// vo_free_string. Thread-safe.
char* vo_session_call(vo_session* session, const char* request_json);

void vo_free_string(char* s);

#ifdef __cplusplus
}
#endif

#endif
