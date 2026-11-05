#include "atw/sequencer.h"

namespace atw {

void Sequencer::playSong(int orderPos, int row) {
    pending_ = kPlaySong;
    pendArg_ = orderPos;
    pendRow_ = row;
}
void Sequencer::playPattern(int pattern, int row) {
    pending_ = kPlayPattern;
    pendArg_ = pattern;
    pendRow_ = row;
}
void Sequencer::playPreview(int pattern, int row0, int row1, uint8_t trackMask) {
    pending_ = kPlayPreview;
    pendArg_ = pattern;
    pendRow_ = row0;
    pendTo_ = row1;
    pendMask_ = trackMask;
}
void Sequencer::stop() { pending_ = kStop; }

void Sequencer::silence(atari::VoiceBank& bank) {
    atari::Step off;
    off.note = atari::kNoteOff;
    for (int t = 0; t < 4; t++) bank.step(t, off);
}

void Sequencer::loadEntry() {
    const OrderEntry& e = song_->order[orderPos_.load() % kOrderLen];
    pattern_.store(e.pattern % kPatternCount);
    repeatLeft_ = e.repeat < 1 ? 1 : e.repeat;
}

// In song mode a grid row can give each track its own pattern (or take it out); a pattern queued while playing and pattern mode play one
// pattern on every track.
int Sequencer::trackPattern(int t) const {
    if (mode() != kSong || inserted_) return pattern_.load();
    return song_->order[orderPos_.load() % kOrderLen].trackPattern(t);
}

int Sequencer::rowsNow() const {
    if (mode() != kSong || inserted_) return song_->pattern(pattern_.load()).rows;
    return song_->entryRows(song_->order[orderPos_.load() % kOrderLen]);
}

void Sequencer::startPending(atari::VoiceBank& bank) {
    const Pending p = pending_;
    pending_ = kNone;
    if (p == kStop) {
        if (mode() != kStopped) silence(bank);
        mode_.store(kStopped);
        return;
    }
    if (!song_) return;
    silence(bank);
    speed_ = song_->speed < 1 ? 1 : song_->speed;
    rowFrame_ = 0;
    len_ = acc_ = 0;
    brk_ = false;
    started_ = false;
    inserted_ = false;
    preview_ = p == kPlayPreview;
    queued_.store(-1, std::memory_order_relaxed);
    if (p == kPlayPreview) {
        const int rows = song_->pattern(pendArg_ % kPatternCount).rows;
        pvFrom_ = pendRow_ < 0 ? 0 : (pendRow_ >= rows ? rows - 1 : pendRow_);
        pvTo_ = pendTo_ < pvFrom_ ? pvFrom_ : (pendTo_ >= rows ? rows - 1 : pendTo_);
        pvMask_ = pendMask_;
        pattern_.store(pendArg_ % kPatternCount);
        repeatLeft_ = 1;
        restartFrame_ = frames_;
        mode_.store(kPattern);
        row_.store(pvFrom_);
        return;
    }
    if (p == kPlaySong) {
        wraps_.store(0, std::memory_order_relaxed);
        int pos = pendArg_;
        if (pos < 0 || pos >= song_->orderLen) pos = 0;
        restartFrame_ = pos == song_->restartPos ? frames_ : -1;
        orderPos_.store(pos);
        loadEntry();
        mode_.store(kSong);
    } else {
        pattern_.store(pendArg_ % kPatternCount);
        repeatLeft_ = 1;
        restartFrame_ = frames_;             // a pattern loops onto its own start
        mode_.store(kPattern);
    }
    const int rows = rowsNow();
    row_.store(pendRow_ >= 0 && pendRow_ < rows ? pendRow_ : 0);
}

void Sequencer::advanceRow() {
    if (preview_) {                                      // the block loops on itself
        int row = row_.load() + 1;
        if (brk_ || row > pvTo_ || row >= rowsNow()) {
            brk_ = false;
            row = pvFrom_;
        }
        row_.store(row);
        return;
    }
    const int rows = rowsNow();
    int row = row_.load() + 1;
    if (brk_ || row >= rows) {
        brk_ = false;
        row = 0;
        if (mode() == kPattern && stopAtEnd_) endNow_ = true;   // export: the pattern has been played once
        if (mode() == kSong && inserted_) {                     // a queued pattern has run: the order list goes on where it was going
            inserted_ = false;
            pattern_.store(resume_);
        } else if (mode() == kSong && --repeatLeft_ <= 0) {
            int pos = orderPos_.load() + 1;
            if (pos >= song_->orderLen) {
                pos = song_->restartPos < song_->orderLen ? song_->restartPos : 0;
                wraps_.fetch_add(1, std::memory_order_relaxed);
                if (stopAtEnd_) endNow_ = true;
            }
            orderPos_.store(pos);
            if (restartFrame_ < 0 && pos == song_->restartPos) restartFrame_ = frames_;
            loadEntry();
        }
        const int q = queued_.exchange(-1, std::memory_order_relaxed);
        if (q >= 0 && !stopAtEnd_ && !endNow_) {                // the pattern chosen while playing starts at its top
            if (mode() == kSong) {
                resume_ = pattern_.load();
                inserted_ = true;
            }
            pattern_.store(q);
        }
    }
    row_.store(row);
}

void Sequencer::onFrame(atari::VoiceBank& bank) {
    if (pending_ != kNone) startPending(bank);
    if (const int ls = liveSpeed_.exchange(0, std::memory_order_relaxed)) speed_ = ls;
    if (mode() == kStopped || !song_) return;
    if (started_) {                                    // the end of the previous frame
        if (!len_) len_ = atari::rowTicks((row_.load() & 1) && song_->funk > 0 ? song_->funk : speed_, atari::log2Rows(song_->stepsPerBeat), acc_);
        if (++rowFrame_ >= len_) {
            rowFrame_ = 0;
            len_ = 0;
            advanceRow();
            if (endNow_) {                             // export: the song has been played once
                endNow_ = false;
                silence(bank);
                mode_.store(kStopped);
                return;
            }
        }
    }
    started_ = true;
    frames_++;
    if (rowFrame_ != 0) return;                        // a row is under way
    const int r = row_.load();
    const int spb = song_->stepsPerBeat < 1 ? 4 : song_->stepsPerBeat;
    if (r % spb == 0) {
        beatAccent_.store(r % (spb * 4) == 0, std::memory_order_relaxed);
        beat_.fetch_add(1, std::memory_order_relaxed);
    }
    for (int t = 0; t < kTracks; t++) {
        const int slot = song_->trackSlot(t);            // TIA plays two of the four pattern tracks
        if (slot < 0) continue;
        if (preview_ && !((pvMask_ >> t) & 1)) continue; // a preview plays just the selected tracks
        const int p = trackPattern(t);
        if (p < 0) {                                     // taken out of this grid row: what it played stops as the row starts
            if (r == 0) {
                atari::Step off;
                off.note = atari::kNoteOff;
                bank.step(slot, off);
            }
            continue;
        }
        const Pattern& pat = song_->pattern(p);
        const Cell& c = pat.cells[r % pat.rows][t];      // a pattern shorter than the row loops
        if (c.empty()) continue;
        atari::Step st = toStep(c);
        if (mix_) st = withTrackVolume(st, mix_[t].load(std::memory_order_relaxed));
        bank.step(slot, st, &speed_, &brk_);
    }
}

}  // namespace atw
