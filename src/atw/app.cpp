#include "atw/app.h"

#include <cstring>

namespace atw {

namespace {
// The audio callback must fill every block it is asked for; until there is a sound engine that is silence.
void fillSilence(int16_t* out, uint32_t frames, void*) {
    std::memset(out, 0, size_t(frames) * ot::IAudioOut::kChannels * sizeof(int16_t));
}
}  // namespace

bool App::init(const Platform& p) {
    p_ = p;
    if (!p_.display || !p_.input || !p_.clock) return false;
    if (p_.audio) {
        p_.audio->setCallback(&fillSilence, nullptr);
        p_.audio->start();
    }
    ui.init(*this);
    return true;
}

void App::boot() {}

void App::quiesce() {
    if (p_.audio) p_.audio->setCallback(&fillSilence, nullptr);
}

void App::loop() {
    if (p_.service) p_.service();
    ot::InputEvent ev;
    for (int i = 0; i < 32 && p_.input->poll(ev); ++i) ui.handle(ev);
    ui.update(p_.clock->millis());
}

void App::run() {
    for (;;) loop();
}

}  // namespace atw
