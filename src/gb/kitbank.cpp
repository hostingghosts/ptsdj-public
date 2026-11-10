#include "gb/kitbank.h"

#include <algorithm>
#include <cstring>

namespace gb {

namespace {
constexpr int kNames = 0x22, kKitName = 0x52;
int offsetOf(const uint8_t* b, int i) { return (b[i * 2] | b[i * 2 + 1] << 8) - 0x4000; }
void setOffset(uint8_t* b, int i, int off) { const int v = off ? off + 0x4000 : 0; b[i * 2] = uint8_t(v); b[i * 2 + 1] = uint8_t(v >> 8); }
bool padding(const uint8_t* p) {                 // a chunk of nothing: silence (8), zeros or an erased flash
    for (int k = 1; k < 16; ++k) if (p[k] != p[0]) return false;
    return p[0] == 0x88 || p[0] == 0x00 || p[0] == 0xFF;
}
}  // namespace

void KitBank::clear(uint8_t* b) {
    std::memset(b, 0x88, kKitBytes);
    std::memset(b, 0, kKitHeader);
    for (int i = 0; i < kKitMaxSamples; ++i) { b[kNames + i * 3] = 0; b[kNames + i * 3 + 1] = '-'; b[kNames + i * 3 + 2] = '-'; }
}

int KitBank::entries(const uint8_t* b) {
    int n = 0;
    while (n < kKitMaxSamples + 1 && (b[n * 2] | b[n * 2 + 1] << 8) != 0) ++n;
    return n;
}

int KitBank::dataEnd(const uint8_t* b) {
    int end = kKitBytes;
    while (end - 16 >= kKitHeader && padding(b + end - 16)) end -= 16;
    return end;
}

// The end entry points at or past the last chunk with sound; the start of a real sample never does.
bool KitBank::hasEnd(const uint8_t* b) {
    const int n = entries(b);
    return n > 0 && offsetOf(b, n - 1) >= dataEnd(b);
}

int KitBank::count(const uint8_t* b) {
    const int n = entries(b);
    return n - (hasEnd(b) ? 1 : 0);
}

namespace {
// Sample starts stay, the entry after the last sample points at `end`, the rest are empty.
void seal(uint8_t* b, int count, int end) {
    setOffset(b, count, end);
    for (int k = count + 1; k <= kKitMaxSamples; ++k) setOffset(b, k, 0);
}
}  // namespace

void KitBank::normalize(uint8_t* b) {
    if (!hasSamples(b) || hasEnd(b)) return;
    const int n = entries(b);
    if (n > kKitMaxSamples) return;
    seal(b, n, used(b));
}

int KitBank::length(const uint8_t* b, int i) {
    const int n = count(b);
    if (i < 0 || i >= n) return 0;
    const int end = i + 1 < entries(b) ? offsetOf(b, i + 1) : used(b);
    return end - offsetOf(b, i);
}

const uint8_t* KitBank::data(const uint8_t* b, int i) { return i >= 0 && i < count(b) ? b + offsetOf(b, i) : nullptr; }

void KitBank::name(const uint8_t* b, int i, char out[4]) {
    for (int k = 0; k < 3; ++k) { const uint8_t c = b[kNames + i * 3 + k]; out[k] = c >= 32 && c < 127 ? char(c) : ' '; }
    out[3] = 0;
}

void KitBank::setName(uint8_t* b, int i, const char* nm) {
    bool end = !nm;
    for (int k = 0; k < 3; ++k) { end = end || !nm[k]; b[kNames + i * 3 + k] = end ? ' ' : uint8_t(nm[k]); }
}

void KitBank::kitName(const uint8_t* b, char out[9]) {
    for (int k = 0; k < 8; ++k) { const uint8_t c = b[kKitName + k]; out[k] = c >= 32 && c < 127 ? char(c) : ' '; }
    out[8] = 0;
}

void KitBank::setKitName(uint8_t* b, const char* nm) {
    bool end = !nm;
    for (int k = 0; k < 8; ++k) { end = end || !nm[k]; b[kKitName + k] = end ? ' ' : uint8_t(nm[k]); }
}

int KitBank::used(const uint8_t* b) {
    const int n = count(b);
    if (!n) return kKitHeader;
    const int floor = offsetOf(b, n - 1) + 16;                       // the last sample keeps at least one chunk
    const int end = std::max(dataEnd(b), floor);
    return end + 16 <= kKitBytes ? end + 16 : kKitBytes;             // plus the silent chunk that ends it (the last chunk of a sample is not played)
}

// LSDJ adds two layered samples nibble by nibble (x + y - 8, clipped), so loud samples distort together; a lower level leaves room for the second one.
void KitBank::copyLevel(uint8_t* dst, const uint8_t* src, int n, int pct) {
    if (pct >= 100) { std::memcpy(dst, src, size_t(n)); return; }
    auto f = [&](int v) { const int d = v - 8; const int s = d * pct / 100; return std::min(15, std::max(0, 8 + s)); };   // truncated toward the centre, so two 50% samples never add past the top
    for (int i = 0; i < n; ++i) dst[i] = uint8_t(f(src[i] >> 4) << 4 | f(src[i] & 15));
}

bool KitBank::add(uint8_t* b, const char* nm, const uint8_t* bytes, int n, int levelPct) {
    const int c = count(b);
    if (c >= kKitMaxSamples || n <= 0) return false;
    const int size = paddedSize(n);
    const int start = c ? used(b) : kKitHeader;
    if (start + size > kKitBytes) return false;
    copyLevel(b + start, bytes, n, levelPct);
    std::memset(b + start + n, 0x88, size_t(size - n));
    setOffset(b, c, start);
    setName(b, c, nm);
    seal(b, c + 1, start + size);
    return true;
}

bool KitBank::replace(uint8_t* b, int i, const char* nm, const uint8_t* bytes, int n, int levelPct) {
    const int c = count(b);
    if (i < 0 || i >= c || n <= 0) return false;
    const int u = used(b);
    const int from = offsetOf(b, i), to = i + 1 < c ? offsetOf(b, i + 1) : u;
    const int size = paddedSize(n), delta = size - (to - from);
    if (u + delta > kKitBytes) return false;
    std::memmove(b + to + delta, b + to, size_t(u - to));
    copyLevel(b + from, bytes, n, levelPct);
    std::memset(b + from + n, 0x88, size_t(size - n));
    if (delta < 0) std::memset(b + u + delta, 0x88, size_t(-delta));            // the end of the kit is silence again
    for (int k = i + 1; k < c; ++k) setOffset(b, k, offsetOf(b, k) + delta);
    setName(b, i, nm);
    seal(b, c, u + delta);
    return true;
}

bool KitBank::remove(uint8_t* b, int i) {
    const int c = count(b);
    if (i < 0 || i >= c) return false;
    const int u = used(b);
    const int from = offsetOf(b, i), to = i + 1 < c ? offsetOf(b, i + 1) : u, len = to - from;
    std::memmove(b + from, b + to, size_t(u - to));
    std::memset(b + u - len, 0x88, size_t(len));
    for (int k = i; k + 1 < c; ++k) {
        setOffset(b, k, offsetOf(b, k + 1) - len);
        std::memcpy(b + kNames + k * 3, b + kNames + (k + 1) * 3, 3);
    }
    seal(b, c - 1, c > 1 ? u - len : 0);
    b[kNames + (c - 1) * 3] = 0; b[kNames + (c - 1) * 3 + 1] = '-'; b[kNames + (c - 1) * 3 + 2] = '-';
    return true;
}

}  // namespace gb
