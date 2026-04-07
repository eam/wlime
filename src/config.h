#pragma once

#include <string>

struct Config {
    // Language / input method
    // Supported: "pinyin" (Mandarin)
    // Planned: "jyutping" (Cantonese), "japanese", "korean"
    std::string language;

    // Clipboard behavior: always copy commits to clipboard
    bool clipboard_always;

    // Sound effects
    bool sounds_enabled;
    std::string sound_dir;      // directory containing .wav/.ogg files

    // Animation
    bool animate_enabled;
    int fade_duration_ms;       // overlay fade in/out time

    // Sparkle mode
    bool sparkle_enabled;
};

// Load config with defaults, optionally from ~/.config/wlime/config
void config_init(Config *cfg);
