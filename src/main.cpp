#include "wlime.h"
#include "engine.h"
#include <cstdio>
#include <csignal>

int main(int argc, char *argv[]) {
    // Ignore SIGUSR1 until the real handler is installed in ime_init.
    // Without this, a SIGUSR1 during engine init kills the process.
    signal(SIGUSR1, SIG_IGN);

    gtk_init(&argc, &argv);

    fprintf(stderr, "[wlime] starting up...\n");

    Config config = {};
    config_init(&config);

    sound_init(&config);

    InputEngine *engine = create_engine(config.language);
    if (!engine) {
        fprintf(stderr, "[wlime] failed to initialize engine for '%s', exiting\n",
                config.language.c_str());
        return 1;
    }

    Overlay overlay = {};
    overlay_init(&overlay, &config);
    overlay.language_name = engine->display_name();
    overlay.cjk_font_name = engine->cjk_font();

    IME ime = {};
    ime.engine = engine;
    ime_init(&ime, &overlay, &config);

    gtk_main();

    ime_destroy(&ime);
    delete engine;
    return 0;
}
