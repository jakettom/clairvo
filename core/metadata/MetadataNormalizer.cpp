#include "core/metadata/MetadataNormalizer.h"

#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#include <map>

#include "core/util/StringUtil.h"

namespace vo {

namespace {

// Lowercased key -> first non-empty value (extractor order is preserved).
using KeyIndex = std::vector<std::pair<std::string, std::string>>;

std::optional<std::string> find(const KeyIndex& idx, std::initializer_list<const char*> suffixes) {
    // Priority follows the order of `suffixes`, not the order of raw entries.
    for (const char* suffix : suffixes) {
        std::string s = toLower(suffix);
        for (const auto& [key, value] : idx) {
            if (value.empty()) continue;
            if (key == s || endsWith(key, s)) return value;
        }
    }
    return std::nullopt;
}

bool parseNumber(const std::string& s, double& out) {
    std::string t = trim(s);
    if (t.empty()) return false;
    char* end = nullptr;
    out = std::strtod(t.c_str(), &end);
    return end && end != t.c_str();
}

std::string formatFixed(double v, int decimals) {
    char buf[64];
    std::snprintf(buf, sizeof buf, "%.*f", decimals, v);
    return buf;
}

std::string combineCamera(const std::optional<std::string>& make, const std::optional<std::string>& model) {
    std::string mk = make ? trim(*make) : "";
    std::string md = model ? trim(*model) : "";
    if (mk.empty()) return md;
    if (md.empty()) return mk;
    if (startsWith(toLower(md), toLower(mk))) return md;
    // "SONY" + "ILCE-7M4" reads better with title-cased make.
    bool allUpper = true;
    for (char c : mk)
        if (std::islower(static_cast<unsigned char>(c))) allUpper = false;
    if (allUpper && mk.size() > 3) {
        for (size_t i = 1; i < mk.size(); ++i) mk[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(mk[i])));
    }
    return mk + " " + md;
}

double parseCoordinateComponent(const std::string& s, int degreeDigits) {
    // s is e.g. "42.3736" / "4223.416" / "422225.0" (no sign)
    auto dot = s.find('.');
    size_t intLen = dot == std::string::npos ? s.size() : dot;
    double v = std::strtod(s.c_str(), nullptr);
    if (intLen <= static_cast<size_t>(degreeDigits)) return v;            // DD.DDD
    if (intLen <= static_cast<size_t>(degreeDigits) + 2) {                // DDMM.MMM
        double deg = std::floor(v / 100.0);
        return deg + (v - deg * 100.0) / 60.0;
    }
    double deg = std::floor(v / 10000.0);                                // DDMMSS.SS
    double rest = v - deg * 10000.0;
    double min = std::floor(rest / 100.0);
    double sec = rest - min * 100.0;
    return deg + min / 60.0 + sec / 3600.0;
}

std::string formatLocation(double lat, double lon) { return formatFixed(lat, 6) + ", " + formatFixed(lon, 6); }

std::optional<double> parseGpsValue(const std::string& raw, const std::optional<std::string>& ref) {
    std::string s = trim(raw);
    if (s.empty()) return std::nullopt;
    double sign = 1;
    // Accept "42.3736", "-71.1", "42 deg 22' 25.00\" N", "42,22.416N"
    std::vector<double> nums;
    std::string cur;
    char hemi = 0;
    for (char c : s) {
        if (std::isdigit(static_cast<unsigned char>(c)) || c == '.') {
            cur += c;
        } else {
            if (!cur.empty()) {
                nums.push_back(std::strtod(cur.c_str(), nullptr));
                cur.clear();
            }
            if (c == '-') sign = -1;
            char u = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            if (u == 'N' || u == 'S' || u == 'E' || u == 'W') hemi = u;
        }
    }
    if (!cur.empty()) nums.push_back(std::strtod(cur.c_str(), nullptr));
    if (nums.empty()) return std::nullopt;
    double v = nums[0];
    if (nums.size() > 1) v += nums[1] / 60.0;
    if (nums.size() > 2) v += nums[2] / 3600.0;
    if (!hemi && ref && !ref->empty()) hemi = static_cast<char>(std::toupper(static_cast<unsigned char>((*ref)[0])));
    if (hemi == 'S' || hemi == 'W') sign = -1;
    return sign * v;
}

} // namespace

std::optional<std::string> MetadataNormalizer::parseISO6709(const std::string& value) {
    // e.g. "+42.3736-071.1097+012.000/", "+4223.416-07106.582/"
    std::vector<std::pair<int, std::string>> parts; // sign, digits
    size_t i = 0;
    while (i < value.size() && parts.size() < 3) {
        char c = value[i];
        if (c == '+' || c == '-') {
            int sign = c == '-' ? -1 : 1;
            size_t j = i + 1;
            while (j < value.size() && (std::isdigit(static_cast<unsigned char>(value[j])) || value[j] == '.')) ++j;
            parts.emplace_back(sign, value.substr(i + 1, j - i - 1));
            i = j;
        } else if (c == '/') {
            break;
        } else {
            ++i;
        }
    }
    if (parts.size() < 2 || parts[0].second.empty() || parts[1].second.empty()) return std::nullopt;
    double lat = parts[0].first * parseCoordinateComponent(parts[0].second, 2);
    double lon = parts[1].first * parseCoordinateComponent(parts[1].second, 3);
    if (std::fabs(lat) > 90 || std::fabs(lon) > 180) return std::nullopt;
    return formatLocation(lat, lon);
}

std::optional<std::string> MetadataNormalizer::normalizeTime(const std::string& value) {
    std::string s = trim(value);
    // Collect leading numeric fields: Y M D h m s
    std::vector<std::string> fields;
    size_t i = 0;
    while (i < s.size() && fields.size() < 6) {
        if (std::isdigit(static_cast<unsigned char>(s[i]))) {
            size_t j = i;
            while (j < s.size() && std::isdigit(static_cast<unsigned char>(s[j]))) ++j;
            fields.push_back(s.substr(i, j - i));
            i = j;
            // Skip fractional seconds.
            if (fields.size() == 6 && i < s.size() && s[i] == '.') {
                ++i;
                while (i < s.size() && std::isdigit(static_cast<unsigned char>(s[i]))) ++i;
            }
            continue;
        }
        char c = s[i];
        if (fields.size() >= 3 && (c == '+' || c == 'Z' || (c == '-' && fields.size() >= 5))) break;
        if (c == '-' || c == ':' || c == 'T' || c == ' ' || c == '/') {
            ++i;
            continue;
        }
        break;
    }
    if (fields.size() < 3 || fields[0].size() != 4) return std::nullopt;
    int y = std::atoi(fields[0].c_str());
    int mo = std::atoi(fields[1].c_str());
    int d = std::atoi(fields[2].c_str());
    if (y < 1900 || mo < 1 || mo > 12 || d < 1 || d > 31) return std::nullopt;
    char buf[64];
    if (fields.size() < 5) {
        std::snprintf(buf, sizeof buf, "%04d-%02d-%02d", y, mo, d);
        return std::string(buf);
    }
    int h = std::atoi(fields[3].c_str());
    int mi = std::atoi(fields[4].c_str());
    int se = fields.size() > 5 ? std::atoi(fields[5].c_str()) : 0;
    std::snprintf(buf, sizeof buf, "%04d-%02d-%02dT%02d:%02d:%02d", y, mo, d, h, mi, se);
    std::string out = buf;
    // Timezone
    std::string rest = s.substr(i);
    if (!rest.empty()) {
        if (rest[0] == 'Z') {
            out += "Z";
        } else if (rest[0] == '+' || rest[0] == '-') {
            std::string digits;
            for (size_t k = 1; k < rest.size() && digits.size() < 4; ++k) {
                if (std::isdigit(static_cast<unsigned char>(rest[k]))) digits += rest[k];
                else if (rest[k] != ':') break;
            }
            if (digits.size() == 4) out += std::string(1, rest[0]) + digits.substr(0, 2) + ":" + digits.substr(2, 2);
            else if (digits.size() == 2) out += std::string(1, rest[0]) + digits + ":00";
        }
    }
    return out;
}

std::string MetadataNormalizer::codecName(const std::string& fourcc) {
    static const std::map<std::string, std::string> names = {
        {"avc1", "H.264"},       {"avc3", "H.264"},
        {"hvc1", "HEVC"},        {"hev1", "HEVC"},
        {"dvh1", "HEVC (Dolby Vision)"}, {"dvhe", "HEVC (Dolby Vision)"},
        {"apch", "Apple ProRes 422 HQ"}, {"apcn", "Apple ProRes 422"},
        {"apcs", "Apple ProRes 422 LT"}, {"apco", "Apple ProRes 422 Proxy"},
        {"ap4h", "Apple ProRes 4444"},   {"ap4x", "Apple ProRes 4444 XQ"},
        {"aprn", "Apple ProRes RAW"},    {"aprh", "Apple ProRes RAW HQ"},
        {"mp4v", "MPEG-4"},      {"jpeg", "Motion JPEG"}, {"mjpa", "Motion JPEG"},
        {"av01", "AV1"},         {"vp09", "VP9"},
        {"dvc ", "DV"},          {"dvcp", "DV"},          {"dv5n", "DVCPRO50"},
        {"xd5c", "XDCAM HD422"}, {"xdvc", "XDCAM HD"},
    };
    auto it = names.find(fourcc);
    if (it != names.end()) return it->second;
    auto lower = names.find(toLower(fourcc));
    if (lower != names.end()) return lower->second;
    return trim(fourcc);
}

std::string MetadataNormalizer::formatFrameRate(double fps) {
    std::string s = formatFixed(std::round(fps * 100.0) / 100.0, 2);
    while (!s.empty() && s.back() == '0') s.pop_back();
    if (!s.empty() && s.back() == '.') s.pop_back();
    return s;
}

NormalizedMetadata MetadataNormalizer::normalize(const std::vector<RawMetadataEntry>& raw) {
    KeyIndex idx;
    idx.reserve(raw.size());
    for (const auto& e : raw) idx.emplace_back(toLower(e.key), trim(e.value));

    NormalizedMetadata out;

    // Camera
    auto make = find(idx, {"quicktime.make", "/%a9mak", "/\xC2\xA9" "mak", "common/make", "tiff:make", "make"});
    auto model = find(idx, {"quicktime.model", "/%a9mod", "/\xC2\xA9" "mod", "common/model", "tiff:model", "model"});
    if (std::string cam = combineCamera(make, model); !cam.empty()) out[MetadataCategory::Camera] = cam;

    // Lens
    if (auto lens = find(idx, {"quicktime.camera.lens_model", "exif:lensmodel", "aux:lens", "lensmodel"}))
        out[MetadataCategory::Lens] = *lens;

    // Author / user
    if (auto author = find(idx, {"quicktime.author", "common/author", "/%a9aut", "/\xC2\xA9" "aut", "common/creator",
                                 "common/artist", "/%a9art", "/\xC2\xA9" "art", "dc:creator"}))
        out[MetadataCategory::Author] = *author;

    // Time
    for (const char* key : {"quicktime.creationdate", "common/creationdate", "/%a9day", "/\xC2\xA9" "day",
                            "datetimeoriginal", "xmp:createdate", "asset.creationdate"}) {
        if (auto v = find(idx, {key})) {
            if (auto t = normalizeTime(*v)) {
                out[MetadataCategory::Time] = *t;
                break;
            }
        }
    }

    // Location
    for (const char* key : {"quicktime.location.iso6709", "/%a9xyz", "/\xC2\xA9" "xyz", "common/location"}) {
        if (auto v = find(idx, {key})) {
            if (auto loc = parseISO6709(*v)) {
                out[MetadataCategory::Location] = *loc;
                break;
            }
        }
    }
    if (!out.count(MetadataCategory::Location)) {
        auto lat = find(idx, {"gpslatitude"});
        auto lon = find(idx, {"gpslongitude"});
        if (lat && lon) {
            auto la = parseGpsValue(*lat, find(idx, {"gpslatituderef"}));
            auto lo = parseGpsValue(*lon, find(idx, {"gpslongituderef"}));
            if (la && lo && std::fabs(*la) <= 90 && std::fabs(*lo) <= 180)
                out[MetadataCategory::Location] = formatLocation(*la, *lo);
        }
    }

    // Resolution / orientation
    double w = 0, h = 0, rot = 0;
    auto ws = find(idx, {"track.video.width", "imagewidth"});
    auto hs = find(idx, {"track.video.height", "imageheight"});
    if (ws && hs && parseNumber(*ws, w) && parseNumber(*hs, h) && w > 0 && h > 0) {
        out[MetadataCategory::Resolution] =
            std::to_string(static_cast<long>(std::lround(w))) + "x" + std::to_string(static_cast<long>(std::lround(h)));
        if (auto r = find(idx, {"track.video.rotation", "rotation"})) parseNumber(*r, rot);
        long q = (static_cast<long>(std::lround(rot)) % 360 + 360) % 360;
        bool rotated = q == 90 || q == 270;
        bool landscape = rotated ? h >= w : w >= h;
        out[MetadataCategory::Orientation] = landscape ? "Landscape" : "Portrait";
    }

    // Frame rate
    if (auto f = find(idx, {"track.video.framerate", "videoframerate", "framerate"})) {
        double fps;
        if (parseNumber(*f, fps) && fps > 0) out[MetadataCategory::FrameRate] = formatFrameRate(fps);
    }

    // Codec
    if (auto c = find(idx, {"track.video.codec", "videocodec", "compressorid"})) {
        out[MetadataCategory::Codec] = codecName(*c);
    }

    // Duration
    if (auto d = find(idx, {"asset.duration", "duration"})) {
        double secs;
        if (parseNumber(*d, secs) && secs >= 0 && std::isfinite(secs)) out[MetadataCategory::Duration] = formatFixed(secs, 3);
    }

    return out;
}

} // namespace vo
