#include "media/hashing/FileHasher.h"

#include <CommonCrypto/CommonDigest.h>

#include <cstdint>
#include <cstdio>
#include <vector>

namespace vo::media {

std::optional<std::string> fingerprintFile(const std::string& absolutePath) {
    FILE* f = std::fopen(absolutePath.c_str(), "rb");
    if (!f) return std::nullopt;
    if (std::fseek(f, 0, SEEK_END) != 0) {
        std::fclose(f);
        return std::nullopt;
    }
    long long size = ftello(f);
    std::fseek(f, 0, SEEK_SET);

    CC_SHA256_CTX ctx;
    CC_SHA256_Init(&ctx);
    uint64_t sizeLE = static_cast<uint64_t>(size);
    CC_SHA256_Update(&ctx, &sizeLE, sizeof sizeLE);

    std::vector<unsigned char> buf(kFingerprintChunkBytes);
    auto hashRange = [&](long long offset, size_t length) {
        if (fseeko(f, offset, SEEK_SET) != 0) return false;
        size_t read = std::fread(buf.data(), 1, length, f);
        CC_SHA256_Update(&ctx, buf.data(), static_cast<CC_LONG>(read));
        return read == length;
    };

    bool ok;
    if (size <= static_cast<long long>(2 * kFingerprintChunkBytes)) {
        ok = hashRange(0, static_cast<size_t>(size));
    } else {
        ok = hashRange(0, kFingerprintChunkBytes) &&
             hashRange(size - static_cast<long long>(kFingerprintChunkBytes), kFingerprintChunkBytes);
    }
    std::fclose(f);
    if (!ok) return std::nullopt;

    unsigned char digest[CC_SHA256_DIGEST_LENGTH];
    CC_SHA256_Final(digest, &ctx);
    static const char* hex = "0123456789abcdef";
    std::string out;
    out.reserve(2 * CC_SHA256_DIGEST_LENGTH);
    for (unsigned char b : digest) {
        out += hex[b >> 4];
        out += hex[b & 0xF];
    }
    return out;
}

} // namespace vo::media
