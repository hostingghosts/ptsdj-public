// Atari tracker -- row sequencer: one video frame at a time (50 Hz PAL, 60 Hz NTSC), the way the exported players run.
//
// A row lasts `speed` frames; effect T changes the speed, B ends the pattern after the row. The order list plays each entry `repeat`
// times and loops back to `restartPos`. The steps go to the voice bank of the core engine (core/src/voice.h); the song is read live,
// so edits are heard on the next row. All transport calls come from the audio context (AudioEngine drains its command ring there).
#pragma once

#include <atomic>
#include <cstdint>

#include "atw/atsong.h"
#include "atari/engine.h"
#include "atari/rowtime.h"

namespace atw {

class Sequencer {
public:
    enum Mode : uint8_t { kStopped = 0, kSong = 1, kPattern = 2 };

    void init(const Song* song) { song_ = song; stop(); }
    // Start at an order position (song mode) or loop one pattern. Takes effect on the next frame.
    void playSong(int orderPos, int row = 0);
    void playPattern(int pattern, int row = 0);
    // Pattern preview: rows row0..row1 of `pattern` loop, and only the pattern tracks in `trackMask` (bit t = track t) play.
    void playPreview(int pattern, int row0, int row1, uint8_t trackMask);
    void stop();   // the voices are released on the next frame
    // Song mode only: stop (voices released) where the order list would loop instead of looping. Set before playSong().
    void setStopAtEnd(bool on) { stopAtEnd_ = on; }

    // The engine's frame hook: end the previous frame (next row / pattern), then deliver the row that starts now.
    void onFrame(atari::VoiceBank& bank);
    static void frameHook(void* self, atari::Engine& engine) { static_cast<Sequencer*>(self)->onFrame(engine.bank()); }

    // Read-outs for the screens (written in the audio context, read in the main loop).
    Mode mode() const { return static_cast<Mode>(mode_.load(std::memory_order_relaxed)); }
    bool playing() const { return mode() != kStopped; }
    int orderPos() const { return orderPos_.load(std::memory_order_relaxed); }
    int pattern() const { return pattern_.load(std::memory_order_relaxed); }
    int row() const { return row_.load(std::memory_order_relaxed); }
    // The Master screen's track volumes (percent per pattern track), scaling the volume of the notes this sequencer plays. Null = full volume (exports).
    void setMixer(const std::atomic<uint8_t>* percent) { mix_ = percent; }
    int speed() const { return speed_; }
    // Live pattern change: the pattern starts from its top when the current one has run to its end (pattern mode: it then loops; song mode: it is
    // played once and the order list carries on after it). -1 clears. Any thread.
    void queuePattern(int pattern) { queued_.store(pattern < 0 ? -1 : pattern % kPatternCount, std::memory_order_relaxed); }
    int queuedPattern() const { return queued_.load(std::memory_order_relaxed); }
    // A tempo change while playing: takes effect from the next row (any thread).
    void setLiveSpeed(int frames) { liveSpeed_.store(frames < 1 ? 1 : frames, std::memory_order_relaxed); }
    // Beat clicks: counts every fourth row that starts (and whether it is the first beat of a 16-row bar).
    uint32_t beatCount() const { return beat_.load(std::memory_order_relaxed); }
    bool beatAccent() const { return beatAccent_.load(std::memory_order_relaxed); }
    // Position inside the row, in video frames (0 .. speed-1): recording quantises to the nearest row with it.
    int frameInRow() const { return rowFrame_; }
    long frames() const { return frames_; }
    // Video frame at which the order list first reached its restart entry (-1 until it has): where a looped playback comes back to.
    long restartFrame() const { return restartFrame_; }
    // How many times song mode has run off the end of the order list and back to the restart position (WAV export stops at the first).
    int wraps() const { return wraps_.load(std::memory_order_relaxed); }

private:
    enum Pending : uint8_t { kNone, kPlaySong, kPlayPattern, kPlayPreview, kStop };
    void startPending(atari::VoiceBank& bank);
    void loadEntry();
    void advanceRow();
    int trackPattern(int t) const;            // the pattern track t plays right now, -1 = taken out
    int rowsNow() const;                      // rows the current pattern / grid row lasts
    void silence(atari::VoiceBank& bank);

    const Song* song_ = nullptr;
    Pending pending_ = kNone;
    int pendArg_ = 0, pendRow_ = 0, pendTo_ = 0;
    uint8_t pendMask_ = 15;
    bool preview_ = false;                    // a block of one pattern loops (playPreview): rows pvFrom_..pvTo_, tracks pvMask_
    int pvFrom_ = 0, pvTo_ = 0;
    uint8_t pvMask_ = 15;
    std::atomic<uint8_t> mode_{kStopped};
    std::atomic<int> orderPos_{0}, pattern_{0}, row_{0}, wraps_{0};
    std::atomic<int> liveSpeed_{0}, queued_{-1};
    const std::atomic<uint8_t>* mix_ = nullptr;
    std::atomic<uint32_t> beat_{0};
    std::atomic<bool> beatAccent_{false};
    int repeatLeft_ = 1, rowFrame_ = 0, speed_ = 6;
    int len_ = 0, acc_ = 0;                   // this row's length in ticks (0 = not worked out yet) and the remainder carried to the next row (atari::rowTicks)
    bool inserted_ = false;                   // song mode: the pattern now playing was queued, not in the order list
    int resume_ = 0;                          // ... and the pattern the order list carries on with afterwards
    bool brk_ = false, started_ = false, stopAtEnd_ = false, endNow_ = false;
    long frames_ = 0, restartFrame_ = -1;
};

}  // namespace atw
