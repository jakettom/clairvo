#include "core/util/PathUtil.h"

#include "core/util/StringUtil.h"

namespace vo::path {

std::string join(std::string_view a, std::string_view b) {
    if (a.empty()) return std::string(b);
    if (b.empty()) return std::string(a);
    std::string out(a);
    if (out.back() != '/') out += '/';
    out += b;
    return out;
}

std::string parent(std::string_view relPath) {
    auto pos = relPath.rfind('/');
    if (pos == std::string_view::npos) return "";
    return std::string(relPath.substr(0, pos));
}

std::string filename(std::string_view relPath) {
    auto pos = relPath.rfind('/');
    if (pos == std::string_view::npos) return std::string(relPath);
    return std::string(relPath.substr(pos + 1));
}

std::string stem(std::string_view name) {
    auto pos = name.rfind('.');
    if (pos == std::string_view::npos || pos == 0) return std::string(name);
    return std::string(name.substr(0, pos));
}

std::string extension(std::string_view name) {
    auto pos = name.rfind('.');
    if (pos == std::string_view::npos || pos == 0) return "";
    return std::string(name.substr(pos));
}

bool isWithin(std::string_view p, std::string_view ancestor) {
    if (ancestor.empty()) return true;
    if (p == ancestor) return true;
    return p.size() > ancestor.size() && p.substr(0, ancestor.size()) == ancestor && p[ancestor.size()] == '/';
}

std::string rebase(std::string_view p, std::string_view from, std::string_view to) {
    if (!isWithin(p, from)) return std::string(p);
    if (from.empty()) return join(to, p);
    std::string rest(p.substr(from.size()));
    if (!rest.empty() && rest.front() == '/') rest.erase(0, 1);
    return join(to, rest);
}

std::string absolute(std::string_view root, std::string_view relPath) {
    if (relPath.empty()) return std::string(root);
    std::string out(root);
    if (out.empty() || out.back() != '/') out += '/';
    out += relPath;
    return out;
}

std::string normalizeAbsolute(std::string_view abs) {
    std::string out;
    out.reserve(abs.size());
    for (char c : abs) {
        if (c == '/' && !out.empty() && out.back() == '/') continue;
        out += c;
    }
    while (out.size() > 1 && out.back() == '/') out.pop_back();
    return out;
}

bool relativeTo(std::string_view root, std::string_view abs, std::string& out) {
    std::string r = normalizeAbsolute(root);
    std::string a = normalizeAbsolute(abs);
    if (a == r) {
        out.clear();
        return true;
    }
    if (a.size() > r.size() && a.compare(0, r.size(), r) == 0 && (a[r.size()] == '/' || r == "/")) {
        out = a.substr(r == "/" ? 1 : r.size() + 1);
        return true;
    }
    return false;
}

bool isValidName(std::string_view name, std::string* reason) {
    auto fail = [&](const char* why) {
        if (reason) *reason = why;
        return false;
    };
    if (name.empty()) return fail("Name is empty");
    if (name == "." || name == "..") return fail("Name cannot be '.' or '..'");
    if (name.size() > kMaxNameBytes) return fail("Name is longer than 255 bytes");
    for (char c : name) {
        unsigned char u = static_cast<unsigned char>(c);
        if (c == '/') return fail("Name cannot contain '/'");
        if (c == ':') return fail("Name cannot contain ':'");
        if (u < 0x20 || u == 0x7f) return fail("Name cannot contain control characters");
    }
    if (name.front() == '.') return fail("Name cannot start with '.' (hidden file)");
    if (trim(name) != name) return fail("Name cannot start or end with whitespace");
    return true;
}

std::string sanitizeName(std::string_view name, std::string_view fallback) {
    std::string out;
    out.reserve(name.size());
    for (char c : name) {
        unsigned char u = static_cast<unsigned char>(c);
        if (c == '/' || c == ':') out += '-';
        else if (u < 0x20 || u == 0x7f) out += ' ';
        else out += c;
    }
    out = trim(out);
    while (!out.empty() && out.front() == '.') out.erase(0, 1);
    out = trim(out);
    if (out.size() > kMaxNameBytes - 16) {
        // Leave room for an extension and a collision suffix; avoid splitting
        // a UTF-8 sequence.
        size_t cut = kMaxNameBytes - 16;
        while (cut > 0 && (static_cast<unsigned char>(out[cut]) & 0xC0) == 0x80) --cut;
        out.resize(cut);
        out = trim(out);
    }
    if (out.empty()) out = std::string(fallback);
    return out;
}

} // namespace vo::path
