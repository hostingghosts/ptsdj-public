// Atari tracker -- the audio engine: the POKEY / TIA engine of core/src, the sequencer, a command ring from the main loop and the live
// pads. fill() is the IAudioOut callback (DMA ISR on the device, SDL thread in the simulator): no allocation, no file access, no locks.
// Main loop -> audio: a single-producer command ring (transport, notes). Structural changes (project load, chip or region switch)
// bracket themselves with hold() / release() and call rebuild().
#pragma once

#include <atomic>
#include <cstdint>

#include "atw/atsong.h"
#include "atari/engine.h"
#include "atw/sequencer.h"

namespace atw {

class AudioEngine {
public:
    static constexpr uint32_t kBlock = 128;

    // sampleRateMilliHz: 44,117,647 on the device (MCLK/256), 44,100,000 on the host. The song is read live and must outlive the engine.
    void init(Song* song, uint32_t sampleRateMilliHz);
    // New chip / region / instrument arrays: between hold() and release(), main loop.
    void rebuild();
    void hold();
    void release();

    // IAudioOut::FillFn: `frames` stereo int16 frames (the mono chip on both sides).
    static void fillThunk(int16_t* out, uint32_t frames, void* ctx) { static_cast<AudioEngine*>(ctx)->fill(out, frames); }
    void fill(int16_t* out, uint32_t frames);

    // --- main loop API ---
    void playSong(uint8_t orderPos, uint8_t row = 0) { push(kCmdPlaySong, orderPos, row); }
    void playPattern(uint8_t pattern, uint8_t row = 0) { push(kCmdPlayPattern, pattern, row); }
    // Loops rows row0..row1 of a pattern on the tracks in trackMask (bit t = track t) until stop(); nothing else plays.
    void playPreview(uint8_t pattern, uint8_t row0, uint8_t row1, uint8_t trackMask) { push(kCmdPreview, pattern, row0, row1, trackMask); }
    void stop() { push(kCmdStop); }
    // A pad: `note` is a cell note (1..62), `inst` an instrument number (1..48) on `track`.
    void noteOn(int track, uint8_t note, uint8_t inst, uint8_t vol15 = 15) { push(kCmdNoteOn, (uint8_t)((track & 3) | ((vol15 & 15) << 2)), note, inst); }
    void noteOff(int track) { push(kCmdNoteOff, (uint8_t)track); }
    void panic() { push(kCmdPanic); }
    void resetVoices() { push(kCmdReset); }   // every voice off at once (an instrument was reshaped)
    void setMuted(int track, bool on);
    void setSolo(int track, bool on);       // while any track is soloed only the soloed tracks sound
    // Live track volume 0..15 for the note sounding on `track` (what a V effect does).
    void setTrackVolume(int track, uint8_t vol15) { push(kCmdVolume, (uint8_t)(track & 3), vol15 & 15); }
    void setMasterVolume(uint8_t percent);
    void setTrackMix(int track, uint8_t percent) { mix_[track & 3].store(percent > 100 ? 100 : percent, std::memory_order_relaxed); }
    void enableMixer() { seq_.setMixer(mix_); }   // the live engine follows the Master screen; the export engines play at full volume
    void setLiveSpeed(int frames) { seq_.setLiveSpeed(frames); }   // the tempo changed while playing
    // Metronome: a click on every beat (every fourth row) while playing. `percent` 0..100.
    // Rec + Play with the pre-roll on keeps the click going while recording, even when the metronome itself is off.
    void setRecordClick(bool on) { recClick_.store(on, std::memory_order_relaxed); }
    void setMetronome(bool on, uint8_t percent) { metroOn_.store(on); metroVol_.store(percent > 100 ? 100 : percent); }
    // Count-in: one bar of clicks (16 rows), then the song / pattern starts (record + play with a pre-roll).
    void countInSong(uint8_t orderPos, uint8_t row = 0) { push(kCmdCountIn, 0, orderPos, row); }
    void countInPattern(uint8_t pattern, uint8_t row = 0) { push(kCmdCountIn, 1, pattern, row); }
    bool countingIn() const { return countIn_.load(std::memory_order_relaxed); }

    // Read-outs
    bool playing() const { return seq_.playing(); }
    int orderPos() const { return seq_.orderPos(); }
    int pattern() const { return seq_.pattern(); }
    // Live pattern change (see Sequencer::queuePattern): -1 cancels; queuedPattern() is -1 when nothing waits.
    void queuePattern(int p) { seq_.queuePattern(p); }
    int queuedPattern() const { return seq_.queuedPattern(); }
    int row() const { return seq_.row(); }
    Sequencer::Mode sequencerMode() const { return seq_.mode(); }
    bool muted(int track) const { return (muteMask_.load() >> (track & 3)) & 1; }
    bool solo(int track) const { return (soloMask_.load() >> (track & 3)) & 1; }
    int trackLevel(int track) const { const int slot = song_ ? song_->trackSlot(track & 3) : -1; return slot < 0 ? 0 : engine_.trackLevel(slot); }   // meters, by pattern track
    int takePeak() { return peak_.exchange(0); }                            // loudest sample since the last call, 0..32767
    uint32_t sampleRateMilliHz() const { return fs_; }
    uint32_t underflows() const { return 0; }
    Sequencer& sequencer() { return seq_; }
    const Sequencer& sequencer() const { return seq_; }
    atari::Engine& chip() { return engine_; }

private:
    enum : uint8_t { kCmdNone, kCmdPlaySong, kCmdPlayPattern, kCmdStop, kCmdNoteOn, kCmdNoteOff, kCmdPanic, kCmdReset, kCmdVolume, kCmdCountIn, kCmdPreview };
    struct Cmd { uint8_t type, a, b, c, d; };
    static constexpr uint32_t kRing = 64;
    void push(uint8_t type, uint8_t a = 0, uint8_t b = 0, uint8_t c = 0, uint8_t d = 0);
    void drain();
    void onFrame(atari::Engine& e);
    static void frameHookThunk(void* self, atari::Engine& e) { static_cast<AudioEngine*>(self)->onFrame(e); }
    void addClick(int16_t* out, uint32_t frames);
    void startClick(bool accent) { clickLeft_ = kClickLen; clickAccent_ = accent; clickPhase_ = 0; }
    static constexpr int kClickLen = 1100;   // 25 ms

    Song* song_ = nullptr;
    uint32_t fs_ = 44100000;
    atari::Engine engine_;
    atari::PcmConverter pcm_;
    Sequencer seq_;
    int32_t level_[kBlock];
    Cmd ring_[kRing];
    std::atomic<uint32_t> head_{0}, tail_{0};
    std::atomic<bool> hold_{false}, inFill_{false};
    std::atomic<uint16_t> muteMask_{0}, soloMask_{0};
    std::atomic<int> peak_{0};
    uint16_t appliedMute_ = 0;
    float masterGain_ = 1.0f;
    std::atomic<bool> metroOn_{false}, countIn_{false}, recClick_{false};
    std::atomic<uint8_t> metroVol_{60};
    std::atomic<uint8_t> mix_[4] = {{100}, {100}, {100}, {100}};   // per pattern track, from the Master screen
    uint32_t lastBeat_ = 0;
    int clickLeft_ = 0, countFrame_ = 0, countSpeed_ = 6;
    bool clickAccent_ = false;
    float clickPhase_ = 0;
    uint8_t countKind_ = 0, countArg_ = 0, countRow_ = 0;
};

}  // namespace atw
