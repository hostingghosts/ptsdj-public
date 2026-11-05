#include "atw/undo.h"

#include <cstring>

namespace atw {

uint32_t Undo::Entry::bytes() const {
    return uint32_t(sizeof(Entry)) + uint32_t(cells.size() * sizeof(Cell)) + (head ? uint32_t(sizeof(Head)) : 0u) +
           uint32_t(pokey.env.size() * sizeof(atari::EnvStep) + pokey.table.size() + tia.steps.size() * sizeof(atari::TiaStep));
}

void Undo::reset() {
    undo_.clear();
    redo_.clear();
    havePending_ = false;
}

bool Undo::capture(Target t, Entry& e) const {
    e = Entry();
    e.t = t;
    switch (t.kind) {
        case Kind::kPattern: {
            if (t.index >= kPatternCount) return false;
            const Pattern& p = song_->pattern(t.index);
            e.rows = p.rows;
            e.cells.resize(size_t(p.rows) * kTracks);
            for (int r = 0; r < p.rows; ++r)
                for (int k = 0; k < kTracks; ++k) e.cells[size_t(r) * kTracks + size_t(k)] = p.cells[r][k];
            return true;
        }
        case Kind::kInstrument:
            if (t.index >= kInstrumentCount) return false;
            if (t.chip == atari::Chip::Pokey) e.pokey = song_->pokey[t.index];
            else e.tia = song_->tia[t.index];
            return true;
        case Kind::kHead: {
            e.head.reset(new Head);
            Head& h = *e.head;
            std::memcpy(h.name, song_->name, sizeof h.name);
            std::memcpy(h.author, song_->author, sizeof h.author);
            h.chip = song_->chip;
            h.region = song_->region;
            h.speed = song_->speed;
            h.releaseFrames = song_->releaseFrames;
            h.tiaTune = song_->tiaTune;
            h.funk = song_->funk;
            h.stepsPerBeat = song_->stepsPerBeat;
            h.engineSpeed = song_->engineSpeed;
            h.sfxCount = song_->sfxCount;
            std::memcpy(h.sfxPattern, song_->sfxPattern, sizeof h.sfxPattern);
            std::memcpy(h.sfxTrack, song_->sfxTrack, sizeof h.sfxTrack);
            h.orderLen = song_->orderLen;
            h.restartPos = song_->restartPos;
            std::memcpy(h.order, song_->order, sizeof h.order);
            std::memcpy(h.trackVolume, song_->trackVolume, sizeof h.trackVolume);
            std::memcpy(h.tiaTrack, song_->tiaTrack, sizeof h.tiaTrack);
            return true;
        }
        default: return false;
    }
}

void Undo::apply(const Entry& e) {
    switch (e.t.kind) {
        case Kind::kPattern: {
            Pattern& p = song_->pattern(e.t.index);
            p.clear(e.rows);
            for (int r = 0; r < e.rows; ++r)
                for (int k = 0; k < kTracks; ++k) p.cells[r][k] = e.cells[size_t(r) * kTracks + size_t(k)];
            break;
        }
        case Kind::kInstrument:
            if (e.t.chip == atari::Chip::Pokey) song_->pokey[e.t.index] = e.pokey;
            else song_->tia[e.t.index] = e.tia;
            break;
        case Kind::kHead: {
            const Head& h = *e.head;
            std::memcpy(song_->name, h.name, sizeof h.name);
            std::memcpy(song_->author, h.author, sizeof h.author);
            song_->chip = h.chip;
            song_->region = h.region;
            song_->speed = h.speed;
            song_->releaseFrames = h.releaseFrames;
            song_->tiaTune = h.tiaTune;
            song_->funk = h.funk;
            song_->stepsPerBeat = h.stepsPerBeat;
            song_->engineSpeed = h.engineSpeed;
            song_->sfxCount = h.sfxCount;
            std::memcpy(song_->sfxPattern, h.sfxPattern, sizeof h.sfxPattern);
            std::memcpy(song_->sfxTrack, h.sfxTrack, sizeof h.sfxTrack);
            song_->orderLen = h.orderLen;
            song_->restartPos = h.restartPos;
            std::memcpy(song_->order, h.order, sizeof h.order);
            std::memcpy(song_->trackVolume, h.trackVolume, sizeof h.trackVolume);
            std::memcpy(song_->tiaTrack, h.tiaTrack, sizeof h.tiaTrack);
            break;
        }
        default: break;
    }
}

void Undo::begin(Target t) {
    havePending_ = song_ && t.kind != Kind::kNone && capture(t, pending_);
}

void Undo::trim() {
    uint32_t total = 0;
    for (const Entry& e : undo_) total += e.bytes();
    size_t drop = 0;
    while ((undo_.size() - drop > size_t(kMaxSteps) || total > kMaxBytes) && drop + 1 < undo_.size()) total -= undo_[drop++].bytes();
    if (drop) undo_.erase(undo_.begin(), undo_.begin() + long(drop));
}

void Undo::end(bool edited, uint32_t nowMs) {
    if (!havePending_) return;
    havePending_ = false;
    if (!edited) return;
    const bool merge = !undo_.empty() && redo_.empty() && nowMs - lastMs_ < kMergeMs && undo_.back().t.kind == pending_.t.kind &&
                       undo_.back().t.index == pending_.t.index && undo_.back().t.chip == pending_.t.chip;
    lastMs_ = nowMs;
    redo_.clear();
    if (merge) return;                    // the older copy is the one to go back to
    undo_.push_back(std::move(pending_));
    trim();
}

bool Undo::swapWith(std::vector<Entry>& from, std::vector<Entry>& to, Target* applied) {
    if (from.empty() || !song_) return false;
    Entry e = std::move(from.back());
    from.pop_back();
    Entry now;
    if (!capture(e.t, now)) return false;
    apply(e);
    to.push_back(std::move(now));
    if (applied) *applied = e.t;
    lastMs_ = 0;                          // the next edit is a new step
    return true;
}

bool Undo::undo(Target* applied) { return swapWith(undo_, redo_, applied); }
bool Undo::redo(Target* applied) { return swapWith(redo_, undo_, applied); }

}  // namespace atw
