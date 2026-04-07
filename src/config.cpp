#include "config.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>

static std::string get_config_path() {
    const char *home = getenv("HOME");
    if (!home) return "";
    return std::string(home) + "/.config/wlime/config";
}

static std::string trim(const std::string &s) {
    size_t start = s.find_first_not_of(" \t");
    if (start == std::string::npos) return "";
    size_t end = s.find_last_not_of(" \t\n\r");
    return s.substr(start, end - start + 1);
}

void config_init(Config *cfg) {
    // Defaults
    cfg->language = "pinyin";
    cfg->clipboard_always = true;
    cfg->sounds_enabled = true;
    cfg->animate_enabled = true;
    cfg->fade_duration_ms = 150;
    cfg->sparkle_enabled = true;

    // Default sound dir
    const char *home = getenv("HOME");
    if (home)
        cfg->sound_dir = std::string(home) + "/.config/wlime/sounds";

    // Try to read config file
    std::string path = get_config_path();
    if (path.empty()) return;

    std::ifstream f(path);
    if (!f.is_open()) {
        fprintf(stderr, "[wlime] no config at %s, using defaults\n", path.c_str());
        return;
    }

    std::string line;
    while (std::getline(f, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#') continue;

        auto eq = line.find('=');
        if (eq == std::string::npos) continue;

        std::string key = trim(line.substr(0, eq));
        std::string val = trim(line.substr(eq + 1));

        if (key == "language")
            cfg->language = val;
        else if (key == "clipboard_always")
            cfg->clipboard_always = (val == "true" || val == "1");
        else if (key == "sounds")
            cfg->sounds_enabled = (val == "true" || val == "1");
        else if (key == "sound_dir")
            cfg->sound_dir = val;
        else if (key == "animate")
            cfg->animate_enabled = (val == "true" || val == "1");
        else if (key == "fade_duration_ms")
            cfg->fade_duration_ms = atoi(val.c_str());
        else if (key == "sparkle")
            cfg->sparkle_enabled = (val == "true" || val == "1");
    }

    fprintf(stderr, "[wlime] config loaded: clipboard=%d sounds=%d animate=%d fade=%dms\n",
            cfg->clipboard_always, cfg->sounds_enabled, cfg->animate_enabled,
            cfg->fade_duration_ms);
}
