#include "atw/audio.h"

#include <cmath>
#include <cstring>

#include "atari/fastpokey.h"

namespace atw {

void AudioEngine::init(Song* song, uint32_t sampleRateMilliHz) {
    song_ = song;
    fs_ = sampleRateMilliHz ? sampleRateMilliHz : 44100000;
    atari::FastPokey::buildTables();
    seq_.init(song);
    rebuild();
}

void AudioEngine::rebuild() {
    engine_.init(song_->chip, song_->region, fs_ / 1000.0, song_->releaseFrames, song_->pokey, kInstrumentCount, song_->tia, kInstrumentCount, song_->tiaTune, song_->engineSpeed);
    engine_.setFrameHook(&AudioEngine::frameHookThunk, this);
    countIn_.store(false);
    lastBeat_ = seq_.beatCount();
    pcm_.init(engine_.fullScale(), masterGain_);
    seq_.stop();
    appliedMute_ = 0;
}

// The engine's frame hook: during a count-in the clicks come from here (one per beat, an accent on the first), and when the bar is
// over the song starts on this very frame; otherwise the sequencer runs.
void AudioEngine::onFrame(atari::Engine& e) {
    if (countIn_.load(std::memory_order_relaxed)) {
        const int rowFrames = countSpeed_ < 1 ? 1 : countSpeed_;
        if (countFrame_ % rowFrames == 0) {
            const int row = countFrame_ / rowFrames;
            if (row % 4 == 0) startClick(row == 0);
        }
        if (++countFrame_ <= 16 * rowFrames) return;
        countIn_.store(false, std::memory_order_relaxed);
        if (countKind_ == 0) seq_.playSong(countArg_, countRow_);
        else seq_.playPattern(countArg_, countRow_);
    }
    seq_.onFrame(e.bank());
}

void AudioEngine::addClick(int16_t* out, uint32_t frames) {
    const float gain = metroVol_.load(std::memory_order_relaxed) / 100.0f * 12000.0f;
    const float step = (clickAccent_ ? 1800.0f : 1200.0f) * 6.2831853f / (fs_ / 1000.0f);
    for (uint32_t i = 0; i < frames && clickLeft_ > 0; ++i, --clickLeft_) {
        const float env = float(clickLeft_) / kClickLen;
        const int v = int(std::sin(clickPhase_) * env * env * gain);
        clickPhase_ += step;
        for (int c = 0; c < 2; ++c) {
            int s = out[2 * i + c] + v;
            out[2 * i + c] = int16_t(s > 32767 ? 32767 : (s < -32767 ? -32767 : s));
        }
    }
}

void AudioEngine::hold() {
    hold_.store(true, std::memory_order_seq_cst);
    while (inFill_.load(std::memory_order_seq_cst)) {
    }
}
void AudioEngine::release() { hold_.store(false, std::memory_order_seq_cst); }

void AudioEngine::push(uint8_t type, uint8_t a, uint8_t b, uint8_t c, uint8_t d) {
    const uint32_t h = head_.load(std::memory_order_relaxed);
    if (h - tail_.load(std::memory_order_acquire) >= kRing) return;    // full: drop
    ring_[h % kRing] = Cmd{type, a, b, c, d};
    head_.store(h + 1, std::memory_order_release);
}

void AudioEngine::setMuted(int track, bool on) {
    if (track < 0 || track > 3) return;
    uint16_t m = muteMask_.load();
    m = on ? (uint16_t)(m | (1u << track)) : (uint16_t)(m & ~(1u << track));
    muteMask_.store(m);
}

void AudioEngine::setSolo(int track, bool on) {
    if (track < 0 || track > 3) return;
    uint16_t m = soloMask_.load();
    m = on ? (uint16_t)(m | (1u << track)) : (uint16_t)(m & ~(1u << track));
    soloMask_.store(m);
}

void AudioEngine::setMasterVolume(uint8_t percent) {
    masterGain_ = (percent > 100 ? 100 : percent) / 100.0f;
    pcm_.setGain(masterGain_);
}

void AudioEngine::drain() {
    uint32_t t = tail_.load(std::memory_order_relaxed);
    const uint32_t h = head_.load(std::memory_order_acquire);
    for (; t != h; ++t) {
        const Cmd& c = ring_[t % kRing];
        switch (c.type) {
            case kCmdPlaySong: seq_.playSong(c.a, c.b); break;
            case kCmdPlayPattern: seq_.playPattern(c.a, c.b); break;
            case kCmdPreview: seq_.playPreview(c.a, c.b, c.c, c.d); break;
            case kCmdStop:
                seq_.stop();
                countIn_.store(false);
                break;
            case kCmdCountIn:
                countKind_ = c.a;
                countArg_ = c.b;
                countRow_ = c.c;
                countSpeed_ = song_->speed;
                countFrame_ = 0;
                countIn_.store(true);
                break;
            case kCmdNoteOn: {
                const int track = song_->trackSlot(c.a & 3);        // pattern track -> chip voice (TIA: two of the four)
                if (track < 0) break;
                atari::Step s;
                s.note = note::isNote(c.b) ? c.b - 1 : -1;
                s.instrument = c.c ? c.c - 1 : -1;
                s.vol = ((c.a >> 2) & 15) * mix_[c.a & 3].load(std::memory_order_relaxed) / 100;
                if (s.note >= 0) engine_.bank().step(track, s);
                break;
            }
            case kCmdNoteOff: {
                const int slot = song_->trackSlot(c.a & 3);
                if (slot < 0) break;
                atari::Step s;
                s.note = atari::kNoteOff;
                engine_.bank().step(slot, s);
                break;
            }
            case kCmdReset: engine_.resetVoices(); break;
            case kCmdVolume: {
                const int slot = song_->trackSlot(c.a & 3);
                if (slot < 0) break;
                atari::Step s;
                s.vol = c.b & 15;
                engine_.bank().step(slot, s);
                break;
            }
            case kCmdPanic:
                seq_.stop();
                countIn_.store(false);
                engine_.resetVoices();
                break;
        }
    }
    tail_.store(t, std::memory_order_release);
}

void AudioEngine::fill(int16_t* out, uint32_t frames) {
    inFill_.store(true, std::memory_order_seq_cst);
    if (hold_.load(std::memory_order_seq_cst) || !song_) {
        std::memset(out, 0, frames * 2 * sizeof(int16_t));
        inFill_.store(false, std::memory_order_seq_cst);
        return;
    }
    if (frames > kBlock) frames = kBlock;
    drain();
    const uint16_t solo = soloMask_.load();
    const uint16_t mute = solo ? uint16_t(~solo & 15) : muteMask_.load();
    if (mute != appliedMute_) {
        for (int t = 0; t < 4; t++) {
            const int slot = song_->trackSlot(t);
            if (slot < 0) continue;
            if (song_->chip == atari::Chip::Pokey) engine_.bank().pokey.mute(slot, (mute >> t) & 1);
            else engine_.bank().tia.mute(slot, (mute >> t) & 1);
        }
        appliedMute_ = mute;
    }
    engine_.render(level_, (int)frames);
    int peak = peak_.load(std::memory_order_relaxed);
    for (uint32_t i = 0; i < frames; i++) {
        const int16_t v = pcm_.process(level_[i]);
        const int a = v < 0 ? -v : v;
        if (a > peak) peak = a;
        out[2 * i] = v;
        out[2 * i + 1] = v;
    }
    const uint32_t beat = seq_.beatCount();
    if (beat != lastBeat_) {
        lastBeat_ = beat;
        if (metroOn_.load(std::memory_order_relaxed) || recClick_.load(std::memory_order_relaxed)) startClick(seq_.beatAccent());
    }
    if (clickLeft_ > 0) addClick(out, frames);
    peak_.store(peak, std::memory_order_relaxed);
    inFill_.store(false, std::memory_order_seq_cst);
}

}  // namespace atw
