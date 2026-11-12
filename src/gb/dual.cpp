#include "gb/dual.h"

#include <algorithm>
#include <cmath>
#include <iterator>

namespace gb {

void Dual::start(int rowA, int rowB) {
    lim_.reset();
    follow_ = -1;
    a_.start(rowA);
    b_.start(rowB);
}

void Dual::startOne(int console, int row) {
    lim_.reset();
    follow_ = -1;
    (console ? b_ : a_).start(row);
    (console ? a_ : b_).stop();
}

void Dual::startLoop(int console, int ch, int phrase) {
    lim_.reset();
    follow_ = -1;
    (console ? b_ : a_).start(0, ch, phrase);
    (console ? a_ : b_).stop();
}

void Dual::startSolo(int console, int ch, int phrase, int chain) {
    lim_.reset();
    follow_ = -1;
    (console ? b_ : a_).start(0, ch, phrase, false, chain);
    (console ? a_ : b_).stop();
}

void Dual::startOverdub(int console, int ch, int phrase, int rowA, int rowB, int master) {
    lim_.reset();
    Player& me = console ? b_ : a_;
    Player& other = console ? a_ : b_;
    if (master >= 0) other.start(console ? rowA : rowB); else other.stop();
    me.start(console ? rowB : rowA, ch, phrase, true);
    follow_ = master;
    if (master >= 0) (master ? a_ : b_).setTempo(player(master).tempo());
}

void Dual::startLinked(int master, int rowA, int rowB) {
    start(rowA, rowB);
    follow_ = master;
    (master ? a_ : b_).setTempo(player(master).tempo());
}

void Dual::stop() {
    lim_.reset();
    a_.stop();
    b_.stop();
}

void OutputLimiter::Settings::clamp() {
    driveHalfDb = std::clamp(driveHalfDb, 0, 36);
    ceilHalfDb = std::clamp(ceilHalfDb, -24, -1);
    releaseMs = std::clamp((releaseMs + 12) / 25 * 25, 25, 500);
}

void OutputLimiter::set(const Settings& in) {
    s_ = in;
    s_.clamp();
    tgt_ = s_.on ? std::pow(10.f, float(s_.driveHalfDb) * 0.5f / 20.f) : 1.f;
    ceil_ = s_.on ? std::pow(10.f, float(s_.ceilHalfDb) * 0.5f / 20.f) : 1e9f;
    rel_ = std::max<uint32_t>(1u, uint32_t(65536.f / (float(s_.releaseMs) * 44.1f) + 0.5f));      // 125 ms -> 12 a frame
}

void OutputLimiter::reset() {
    gain_ = tgt_;
    std::fill(std::begin(dl_), std::end(dl_), 0.f);
    std::fill(std::begin(dr_), std::end(dr_), 0.f);
    std::fill(std::begin(qv_), std::end(qv_), 65536u);
    std::fill(std::begin(qi_), std::end(qi_), 0u);
    std::fill(std::begin(mbuf_), std::end(mbuf_), 65536u);
    n_ = head_ = tail_ = 0;
    m_ = 65536;
    sum_ = 65536u * kLook;
}

void OutputLimiter::process(float& l, float& r) {
    gain_ += (tgt_ - gain_) * 0.003f;                                                       // about 7 ms to settle on a new drive
    l *= gain_;
    r *= gain_;
    const float peak = std::max(std::fabs(l), std::fabs(r));
    const uint32_t need = peak > ceil_ ? uint32_t(ceil_ / peak * 65536.f) : 65536u;         // rounded down: never needs more than it needs
    const uint32_t n = n_++;
    while (tail_ != head_ && qv_[(head_ - 1) % kLook] >= need) --head_;                      // the sliding minimum of `need` over the last kLook frames
    qv_[head_ % kLook] = need;
    qi_[head_ % kLook] = n;
    ++head_;
    if (n - qi_[tail_ % kLook] >= uint32_t(kLook)) ++tail_;
    uint32_t m = qv_[tail_ % kLook];
    if (m_ + rel_ < m) m = m_ + rel_;                                                       // it comes back up slowly
    m_ = m;
    sum_ += m - mbuf_[n % kLook];                                                           // averaged over kLook frames, the fall is smooth and has arrived by the peak
    mbuf_[n % kLook] = m;
    dl_[n % kLook] = l;
    dr_[n % kLook] = r;
    const float g = gain();
    l = dl_[(n + 1) % kLook] * g;
    r = dr_[(n + 1) % kLook] * g;
}

void Dual::render(int16_t* out, int n, int rate) {
    int16_t ta[2 * 512], tb[2 * 512];
    while (n > 0) {
        if (follow_ >= 0) (follow_ ? a_ : b_).setTempo(player(follow_).tempo());
        const int k = std::min(n, 512);
        a_.render(ta, k, rate);
        b_.render(tb, k, rate);
        for (int i = 0; i < k; ++i) {
            float l = float((int(ta[2 * i]) * vol_[0] + int(tb[2 * i]) * vol_[1]) * 7 / 150) * (1.f / 32768.f);
            float r = float((int(ta[2 * i + 1]) * vol_[0] + int(tb[2 * i + 1]) * vol_[1]) * 7 / 150) * (1.f / 32768.f);
            lim_.process(l, r);
            out[2 * i] = int16_t(std::clamp(l * 32768.f, -32767.f, 32767.f));
            out[2 * i + 1] = int16_t(std::clamp(r * 32768.f, -32767.f, 32767.f));
        }
        out += 2 * k;
        n -= k;
    }
}

}  // namespace gb
