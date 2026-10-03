import ClairvoBridge
import Foundation

struct CoreError: LocalizedError {
    let message: String
    var errorDescription: String? { message }
}

/// Decodes `{}` results of commands that return nothing.
struct Empty: Decodable {}

struct CoreEvent {
    let type: String
    let payload: [String: Any]
}

/// Thin Swift wrapper around the C bridge (`vo_bridge.h`). All requests are
/// JSON; the UI never touches C++ types or SQLite directly.
final class CoreSession: @unchecked Sendable {
    private let handle: OpaquePointer
    private let eventLock = NSLock()
    private var eventHandler: ((CoreEvent) -> Void)?

    private init(handle: OpaquePointer) {
        self.handle = handle
        let context = Unmanaged.passUnretained(self).toOpaque()
        vo_session_set_event_callback(handle, { context, json in
            guard let context, let json else { return }
            let session = Unmanaged<CoreSession>.fromOpaque(context).takeUnretainedValue()
            session.dispatchEvent(String(cString: json))
        }, context)
    }

    deinit {
        vo_session_set_event_callback(handle, nil, nil)
        vo_session_close(handle)
    }

    static func create(root: URL, name: String) throws -> CoreSession {
        var error: UnsafeMutablePointer<CChar>?
        guard let h = vo_session_create(root.path, name, &error) else { throw takeError(error) }
        return CoreSession(handle: h)
    }

    static func open(projectFile: URL) throws -> CoreSession {
        var error: UnsafeMutablePointer<CChar>?
        guard let h = vo_session_open(projectFile.path, &error) else { throw takeError(error) }
        return CoreSession(handle: h)
    }

    private static func takeError(_ error: UnsafeMutablePointer<CChar>?) -> CoreError {
        guard let error else { return CoreError(message: "Unknown error") }
        defer { vo_free_string(error) }
        return CoreError(message: String(cString: error))
    }

    func setEventHandler(_ handler: ((CoreEvent) -> Void)?) {
        eventLock.lock()
        eventHandler = handler
        eventLock.unlock()
    }

    private func dispatchEvent(_ json: String) {
        guard let data = json.data(using: .utf8),
              let obj = try? JSONSerialization.jsonObject(with: data) as? [String: Any],
              let type = obj["type"] as? String else { return }
        eventLock.lock()
        let handler = eventHandler
        eventLock.unlock()
        handler?(CoreEvent(type: type, payload: obj["payload"] as? [String: Any] ?? [:]))
    }

    /// Executes a command and returns the raw `result` JSON data.
    func callRaw(_ command: String, _ args: [String: Any] = [:]) throws -> Data {
        var request = args
        request["cmd"] = command
        let body = try JSONSerialization.data(withJSONObject: request)
        guard let text = String(data: body, encoding: .utf8) else { throw CoreError(message: "Encoding failed") }
        guard let out = vo_session_call(handle, text) else { throw CoreError(message: "No response from core") }
        defer { vo_free_string(out) }
        let response = Data(bytes: out, count: strlen(out))
        guard let obj = try JSONSerialization.jsonObject(with: response) as? [String: Any] else {
            throw CoreError(message: "Malformed response")
        }
        if (obj["ok"] as? Bool) != true {
            throw CoreError(message: obj["error"] as? String ?? "Unknown error")
        }
        return try JSONSerialization.data(withJSONObject: obj["result"] ?? [:], options: [.fragmentsAllowed])
    }

    func call<T: Decodable>(_ command: String, _ args: [String: Any] = [:], as type: T.Type = T.self) throws -> T {
        let data = try callRaw(command, args)
        return try JSONDecoder().decode(T.self, from: data)
    }

    @discardableResult
    func run(_ command: String, _ args: [String: Any] = [:]) throws -> Empty {
        _ = try callRaw(command, args)
        return Empty()
    }
}
