#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace vo {

// ASCII case folding. APFS/HFS+ volumes are case-insensitive by default, so
// name-uniqueness and collision checks compare folded names.
std::string toLower(std::string_view s);
bool equalsIgnoreCase(std::string_view a, std::string_view b);
bool startsWith(std::string_view s, std::string_view prefix);
bool endsWith(std::string_view s, std::string_view suffix);
bool endsWithIgnoreCase(std::string_view s, std::string_view suffix);
std::string trim(std::string_view s);
std::vector<std::string> split(std::string_view s, char delimiter);
std::string join(const std::vector<std::string>& parts, std::string_view delimiter);
std::string replaceAll(std::string s, std::string_view from, std::string_view to);

// Natural ordering used for deterministic, human-friendly sorting
// ("C2" < "C10"), with a case-insensitive comparison and a stable tiebreak.
bool naturalLess(std::string_view a, std::string_view b);

} // namespace vo
