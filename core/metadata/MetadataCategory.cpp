#include "core/metadata/MetadataCategory.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "core/util/PathUtil.h"

namespace vo {

namespace {

struct CategoryInfo {
    MetadataCategory category;
    const char* key;
    const char* displayName;
};

constexpr CategoryInfo kCategories[] = {
    {MetadataCategory::Time, "time", "Time"},
    {MetadataCategory::Location, "location", "Location"},
    {MetadataCategory::Camera, "camera", "Camera"},
    {MetadataCategory::Lens, "lens", "Lens"},
    {MetadataCategory::Resolution, "resolution", "Resolution"},
    {MetadataCategory::FrameRate, "frame_rate", "Frame Rate"},
    {MetadataCategory::Codec, "codec", "Codec"},
    {MetadataCategory::Author, "author", "Author"},
    {MetadataCategory::Duration, "duration", "Duration"},
    {MetadataCategory::Orientation, "orientation", "Orientation"},
};

const CategoryInfo& info(MetadataCategory c) {
    for (const auto& i : kCategories)
        if (i.category == c) return i;
    return kCategories[0];
}

std::string formatTrimmed(double v, int decimals) {
    char buf[64];
    std::snprintf(buf, sizeof buf, "%.*f", decimals, v);
    std::string s = buf;
    if (s.find('.') != std::string::npos) {
        while (!s.empty() && s.back() == '0') s.pop_back();
        if (!s.empty() && s.back() == '.') s.pop_back();
    }
    return s;
}

bool parseDouble(const std::string& s, double& out) {
    if (s.empty()) return false;
    char* end = nullptr;
    out = std::strtod(s.c_str(), &end);
    return end && end != s.c_str();
}

bool parseLocation(const std::string& s, double& lat, double& lon) {
    auto comma = s.find(',');
    if (comma == std::string::npos) return false;
    return parseDouble(s.substr(0, comma), lat) && parseDouble(s.substr(comma + 1), lon);
}

} // namespace

const std::vector<MetadataCategory>& allMetadataCategories() {
    static const std::vector<MetadataCategory> all = [] {
        std::vector<MetadataCategory> v;
        for (const auto& i : kCategories) v.push_back(i.category);
        return v;
    }();
    return all;
}

std::string categoryKey(MetadataCategory c) { return info(c).key; }
std::string categoryDisplayName(MetadataCategory c) { return info(c).displayName; }

std::optional<MetadataCategory> categoryFromKey(std::string_view key) {
    for (const auto& i : kCategories)
        if (key == i.key) return i.category;
    return std::nullopt;
}

std::optional<std::string> datePart(const std::string& t) {
    if (t.size() >= 10 && t[4] == '-' && t[7] == '-') return t.substr(0, 10);
    return std::nullopt;
}

std::string displayValue(MetadataCategory c, const std::string& value) {
    switch (c) {
    case MetadataCategory::FrameRate: {
        double v;
        if (parseDouble(value, v)) return formatTrimmed(v, 3) + " fps";
        return value;
    }
    case MetadataCategory::Duration: {
        double v;
        if (!parseDouble(value, v)) return value;
        long total = std::lround(std::floor(v));
        char buf[32];
        std::snprintf(buf, sizeof buf, "%02ld:%02ld:%02ld", total / 3600, (total / 60) % 60, total % 60);
        return buf;
    }
    case MetadataCategory::Resolution: {
        auto x = value.find('x');
        if (x == std::string::npos) return value;
        return value.substr(0, x) + " × " + value.substr(x + 1);
    }
    case MetadataCategory::Location: {
        double lat, lon;
        if (!parseLocation(value, lat, lon)) return value;
        return formatTrimmed(lat, 4) + ", " + formatTrimmed(lon, 4);
    }
    case MetadataCategory::Time: {
        // "2026-10-03T14:22:05-04:00" -> "2026-10-03 14:22:05 -04:00"
        std::string s = value;
        if (s.size() >= 19 && s[10] == 'T') {
            std::string out = s.substr(0, 10) + " " + s.substr(11, 8);
            if (s.size() > 19) out += " " + s.substr(19);
            return out;
        }
        return s;
    }
    default:
        return value;
    }
}

std::string groupValue(MetadataCategory c, const std::optional<std::string>& value) {
    if (!value || value->empty()) return "Unknown " + categoryDisplayName(c);
    const std::string& v = *value;
    std::string out;
    switch (c) {
    case MetadataCategory::FrameRate: {
        double d;
        out = parseDouble(v, d) ? formatTrimmed(d, 2) + " fps" : v;
        break;
    }
    case MetadataCategory::Duration: {
        double d;
        if (!parseDouble(v, d)) {
            out = v;
        } else if (d < 60) {
            out = "Under 1 min";
        } else if (d < 300) {
            out = "1-5 min";
        } else if (d < 900) {
            out = "5-15 min";
        } else {
            out = "Over 15 min";
        }
        break;
    }
    case MetadataCategory::Time:
        out = datePart(v).value_or(v);
        break;
    case MetadataCategory::Location: {
        // ~100 m buckets; coordinates stay raw (no geocoding, spec §13).
        double lat, lon;
        out = parseLocation(v, lat, lon) ? formatTrimmed(lat, 3) + ", " + formatTrimmed(lon, 3) : v;
        break;
    }
    default:
        out = v;
    }
    return path::sanitizeName(out, "Unknown " + categoryDisplayName(c));
}

} // namespace vo
