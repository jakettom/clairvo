#pragma once

#include <string>
#include <string_view>

namespace vo::path {

// All project paths are stored relative to the project root using '/'
// separators. The root itself is the empty string.
std::string join(std::string_view a, std::string_view b);
std::string parent(std::string_view relPath);   // "A/B/c.mov" -> "A/B", "c.mov" -> ""
std::string filename(std::string_view relPath); // "A/B/c.mov" -> "c.mov"
std::string stem(std::string_view filename);    // "c.mov" -> "c"
std::string extension(std::string_view filename); // "c.mov" -> ".mov" (original case)
bool isWithin(std::string_view path, std::string_view ancestor); // equal or descendant
// Replaces a leading `from` component prefix with `to` (only on component boundaries).
std::string rebase(std::string_view path, std::string_view from, std::string_view to);

std::string absolute(std::string_view root, std::string_view relPath);
// Returns the relative path of `abs` under `root`, or empty optional-like "" with ok=false.
bool relativeTo(std::string_view root, std::string_view abs, std::string& out);
std::string normalizeAbsolute(std::string_view abs); // strips trailing '/', collapses "//"

// Filesystem name validation for macOS volumes.
bool isValidName(std::string_view name, std::string* reason = nullptr);
// Produces a valid name: replaces '/', ':' and control characters, trims
// whitespace and leading dots, truncates to the 255-byte limit.
std::string sanitizeName(std::string_view name, std::string_view fallback = "Untitled");

constexpr size_t kMaxNameBytes = 255;

} // namespace vo::path
