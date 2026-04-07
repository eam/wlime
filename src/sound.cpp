#include "sound.h"
#include <cstdio>
#include <cstdlib>
#include <string>
#include <unistd.h>

static bool enabled = false;
static std::string sound_dir;

static const char *event_filenames[] = {
    "activate.wav",
    "deactivate.wav",
    "keystroke.wav",
    "commit.wav",
    "cancel.wav",
    "select.wav",
};

void sound_init(const Config *cfg) {
    enabled = cfg->sounds_enabled;
    sound_dir = cfg->sound_dir;

    if (!enabled) {
        fprintf(stderr, "[wlime] sounds disabled\n");
        return;
    }

    // Check if sound directory exists
    if (access(sound_dir.c_str(), R_OK) != 0) {
        fprintf(stderr, "[wlime] sound dir %s not found, generating sounds...\n",
                sound_dir.c_str());

        // Create directory
        std::string cmd = "mkdir -p '" + sound_dir + "'";
        system(cmd.c_str());

        // Generate simple sounds using ffmpeg if available, otherwise disable
        // These are short sine wave blips at different frequencies
        struct { const char *name; int freq; int dur_ms; const char *shape; } defs[] = {
            {"activate.wav",   880,  80, "sine"},
            {"deactivate.wav", 440,  80, "sine"},
            {"keystroke.wav",  1200, 25, "sine"},
            {"commit.wav",     660,  120, "sine"},
            {"cancel.wav",     330,  100, "sine"},
            {"select.wav",     990,  60,  "sine"},
        };

        bool have_ffmpeg = (system("which ffmpeg >/dev/null 2>&1") == 0);
        if (have_ffmpeg) {
            for (auto &d : defs) {
                char gen[512];
                snprintf(gen, sizeof(gen),
                    "ffmpeg -y -f lavfi -i \"sine=frequency=%d:duration=0.%03d\" "
                    "-af \"afade=t=out:st=0:d=0.%03d\" "
                    "'%s/%s' >/dev/null 2>&1",
                    d.freq, d.dur_ms, d.dur_ms, sound_dir.c_str(), d.name);
                system(gen);
            }
            fprintf(stderr, "[wlime] generated %zu sound effects\n",
                    sizeof(defs) / sizeof(defs[0]));
        } else {
            fprintf(stderr, "[wlime] ffmpeg not found, sounds disabled\n");
            enabled = false;
        }
    } else {
        fprintf(stderr, "[wlime] using sounds from %s\n", sound_dir.c_str());
    }
}

void sound_play(SoundEvent ev) {
    if (!enabled) return;

    std::string path = sound_dir + "/" + event_filenames[ev];
    if (access(path.c_str(), R_OK) != 0) return;

    // Fork to avoid blocking — pw-play is fast but we don't want to wait
    pid_t pid = fork();
    if (pid == 0) {
        // Child: redirect stdout/stderr to /dev/null
        freopen("/dev/null", "w", stdout);
        freopen("/dev/null", "w", stderr);
        execlp("pw-play", "pw-play", path.c_str(), nullptr);
        _exit(1);
    }
    // Parent: don't wait, let it play async
    // Reap zombies periodically (not critical for short-lived processes)
}
