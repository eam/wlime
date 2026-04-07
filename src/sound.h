#pragma once

#include "config.h"

enum SoundEvent {
    SND_ACTIVATE,       // composition mode on
    SND_DEACTIVATE,     // composition mode off
    SND_KEYSTROKE,      // typing a pinyin letter
    SND_COMMIT,         // candidate committed
    SND_CANCEL,         // escape pressed
    SND_SELECT,         // number key candidate selection
};

void sound_init(const Config *cfg);
void sound_play(SoundEvent ev);
