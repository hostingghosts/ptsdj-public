// Atari tracker -- MIDI input parser. Running status; real-time bytes (F8-FF) are skipped wherever they come; SysEx is skipped.
// Only what the tracker uses is reported: note on / off and "all notes off" (CC 123), on any channel.
#pragma once

#include <cstdint>

namespace atw {

class MidiIn {
public:
    enum class Type : uint8_t { kNone, kNoteOn, kNoteOff, kAllOff };
    struct Msg {
        Type type = Type::kNone;
        uint8_t note = 0, velocity = 0;
    };
    // MIDI note 24 (C1) is the tracker's C-1, so middle C (60) is C-4.
    static int cellNote(uint8_t midi) { return int(midi) - 23; }

    // One byte in; true when a message that matters is complete (in `m`).
    bool feed(uint8_t b, Msg& m) {
        if (b >= 0xF8) return false;                     // real time: does not disturb anything
        if (b == 0xF0) { sysex_ = true; status_ = 0; have_ = 0; return false; }
        if (b >= 0x80) {
            sysex_ = false;
            status_ = (b >= 0xF0) ? 0 : b;               // other system messages cancel running status
            have_ = 0;
            return false;
        }
        if (sysex_ || !status_) return false;
        data_[have_++] = b;
        const uint8_t kind = status_ & 0xF0;
        const int need = (kind == 0xC0 || kind == 0xD0) ? 1 : 2;
        if (have_ < need) return false;
        have_ = 0;
        switch (kind) {
            case 0x90:
                m = {data_[1] ? Type::kNoteOn : Type::kNoteOff, data_[0], data_[1]};
                return true;
            case 0x80:
                m = {Type::kNoteOff, data_[0], data_[1]};
                return true;
            case 0xB0:
                if (data_[0] == 123 || data_[0] == 120) { m = {Type::kAllOff, 0, 0}; return true; }
                return false;
            default: return false;
        }
    }

private:
    uint8_t status_ = 0, data_[2] = {}, have_ = 0;
    bool sysex_ = false;
};

}  // namespace atw
